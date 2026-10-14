#!/bin/sh
# SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
# SPDX-License-Identifier: MIT
#
# A/B: the same benchmark source against two library trees, interleaved, then the oracle on
# the candidate.
#
#   benchmark/ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]
#
# Compiles THIS tree's benchmark/op_bench.c against each tree's include/ and src/ with the
# flags of benchmark/README.md, runs the two binaries INTERLEAVED (A B, B A, A B, ...) for
# AB_REPEATS rounds (default 7), and prints ns/op min, median and spread per operation and
# mode for each side, the speedup (baseline median / candidate median) and whether it is
# outside the spread. Then builds benchmark/oracle_check against the candidate, native and
# emulated 128-bit, and diffs it against benchmark/oracle-baseline.txt, with the baseline
# tree's build as --against so a difference is located to its exact input.
#
# Environment: CC (default cc), AB_CFLAGS (added to BOTH builds), AB_REPEATS (7),
# AB_MIN_MS (per-sample floor, 20), AB_ORACLE=0 to skip the oracle, AB_OUT (work directory,
# default a new one under TMPDIR).
#
# It refuses to start while another benchmark process is running on the machine, and it
# never stops one: two benchmarks on one box measure each other.
set -eu

if [ $# -lt 2 ]; then
	echo "usage: $0 <baseline-src-dir> <candidate-src-dir> [op ...]" >&2
	exit 2
fi
A=$(cd "$1" && pwd)
B=$(cd "$2" && pwd)
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
CC=${CC:-cc}
FLAGS="-O3 -DNDEBUG -ffp-contract=off ${AB_CFLAGS:-}"
ROUNDS=${AB_REPEATS:-7}
MINMS=${AB_MIN_MS:-20}
# Refuse while another benchmark runs. pgrep only looks; nothing is ever killed here.
busy=$(pgrep -af 'op_bench|numerical_bench|oracle_check|bench-before|bench-after' 2>/dev/null | grep -v "^$$ " || true)
if [ -n "$busy" ]; then
	echo "ab.sh: another benchmark is running on this machine; not starting:" >&2
	echo "$busy" >&2
	exit 3
fi

OUT=${AB_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/fixed-ab.XXXXXX")}
mkdir -p "$OUT"

sh "$HERE/cpu_info.sh"
echo "flags:    $FLAGS"
echo "baseline: $A ($(git -C "$A" rev-parse --short HEAD 2>/dev/null || echo 'not a git tree'))"
echo "candidate: $B ($(git -C "$B" rev-parse --short HEAD 2>/dev/null || echo 'not a git tree'))"
echo "rounds:   $ROUNDS, interleaved; work directory $OUT"
echo

build() { # <tree> <source> <output> [extra flags]
	tree=$1 src=$2 out=$3
	shift 3
	# shellcheck disable=SC2086
	$CC $FLAGS "$@" -I "$tree/include" "$src" "$tree/src/fixed_math.c" "$tree/src/fixed_vec.c" -lm -o "$out"
}

build "$A" "$HERE/op_bench.c" "$OUT/op_bench-a"
build "$B" "$HERE/op_bench.c" "$OUT/op_bench-b"

: >"$OUT/samples.csv"
r=1
while [ "$r" -le "$ROUNDS" ]; do
	if [ $((r % 2)) -eq 1 ]; then order="a b"; else order="b a"; fi
	for side in $order; do
		"$OUT/op_bench-$side" --repeats 1 --min-ms "$MINMS" "$@" | sed -n "2,\$s/^/$side,/p" >>"$OUT/samples.csv"
	done
	r=$((r + 1))
done

# side,op,mode,ns_min,ns_median,ns_max,checksum,reps -> one row per op and mode.
awk -F, '
function sortv(v, n,   i, j, t) { for (i = 2; i <= n; i++) { t = v[i]; for (j = i - 1; j >= 1 && v[j] > t; j--) v[j + 1] = v[j]; v[j + 1] = t } }
{
	key = $2 "," $3
	if (!(key in seen)) { seen[key] = 1; order[++nk] = key }
	n = ++cnt[$1 "," key]; val[$1 "," key "," n] = $5
	sum[$1 "," key] = $7
}
END {
	printf "%-24s %-6s %10s %10s %7s   %10s %10s %7s   %8s  %s\n", "op", "mode", "A min", "A median", "A spr%", "B min", "B median", "B spr%", "speedup", "verdict"
	for (k = 1; k <= nk; k++) {
		key = order[k]
		for (s = 0; s < 2; s++) {
			side = s ? "b" : "a"; n = cnt[side "," key]
			for (i = 1; i <= n; i++) v[i] = val[side "," key "," i]
			sortv(v, n)
			mn[s] = v[1]; md[s] = (n % 2) ? v[(n + 1) / 2] : (v[n / 2] + v[n / 2 + 1]) / 2
			sp[s] = md[s] > 0 ? 100 * (v[n] - v[1]) / md[s] : 0
		}
		speed = md[1] > 0 ? md[0] / md[1] : 0
		spread = sp[0] > sp[1] ? sp[0] : sp[1]
		change = 100 * (speed - 1); if (change < 0) change = -change
		verdict = change <= spread ? "within spread" : (speed > 1 ? "FASTER" : "SLOWER")
		if (sum["a," key] != sum["b," key]) verdict = verdict ", CHECKSUM DIFFERS"
		split(key, kp, ",")
		printf "%-24s %-6s %10.3f %10.3f %6.1f%%   %10.3f %10.3f %6.1f%%   %7.3fx  %s\n", kp[1], kp[2], mn[0], md[0], sp[0], mn[1], md[1], sp[1], speed, verdict
	}
}' "$OUT/samples.csv" | tee "$OUT/table.txt"
echo
echo "ns per operation; A = baseline, B = candidate; spread = (max - min) / median over $ROUNDS interleaved runs;"
echo "speedup = A median / B median; a change no larger than the wider spread is reported as within it."

if [ "${AB_ORACLE:-1}" = 0 ]; then
	echo "oracle: skipped (AB_ORACLE=0)"
	exit 0
fi

echo
status=0
for arm in native emulated; do
	extra=""
	[ "$arm" = emulated ] && extra="-DFIX_FORCE_EMULATED_INT128"
	# shellcheck disable=SC2086
	build "$A" "$HERE/oracle_check.c" "$OUT/oracle-a-$arm" $extra
	# shellcheck disable=SC2086
	build "$B" "$HERE/oracle_check.c" "$OUT/oracle-b-$arm" $extra
	echo "oracle ($arm) on the candidate:"
	if ! "$OUT/oracle-b-$arm" --baseline "$HERE/oracle-baseline.txt" --against "$OUT/oracle-a-$arm"; then
		status=1
	fi
done
exit $status
