#!/bin/sh
# ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]
#
# Compiles the SAME benchmark/op_bench.c against both trees with the flags of
# benchmark/README.md, runs them INTERLEAVED for N repeats (default 7), refuses to run
# while another benchmark process is on the box (pgrep -af, never kills anything), prints
# a table of ns/op min/median/spread and speedup per operation and mode, and runs
# oracle_check on the candidate.
set -u

BASE_SRC=${1:?usage: ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]}
CAND_SRC=${2:?usage: ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]}
shift 2
OPS="$@"

N=${AB_REPEATS:-7}
ITER=${AB_ITERS:-5000000}
SEED=${AB_SEED:-42}
FLAGS="-O3 -g -DNDEBUG -ffp-contract=off -std=c11"
BENCH="benchmark/op_bench.c"
SRC="src/fixed_math.c src/fixed_vec.c"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/fixed-ab.XXXXXX")

# Refuse to run while another benchmark process is on the box.
if pgrep -af 'op_bench|numerical_bench' 2>/dev/null | grep -v "$$" >/dev/null; then
	echo "ab.sh: another benchmark process is running:" >&2
	pgrep -af 'op_bench|numerical_bench' 2>/dev/null >&2
	rm -rf "$OUT"
	exit 2
fi

echo "== cpu_info =="
sh "$CAND_SRC/benchmark/cpu_info.sh" 2>/dev/null || sh "$BASE_SRC/benchmark/cpu_info.sh" 2>/dev/null || true
echo

echo "compiling op_bench.c against baseline ($BASE_SRC) and candidate ($CAND_SRC)..."
cc $FLAGS -I "$BASE_SRC/include" "$BASE_SRC/$BENCH" "$BASE_SRC/$SRC" -lm -o "$OUT/bench-before" \
	|| { echo "ab.sh: baseline compile failed" >&2; rm -rf "$OUT"; exit 1; }
cc $FLAGS -I "$CAND_SRC/include" "$CAND_SRC/$BENCH" "$CAND_SRC/$SRC" -lm -o "$OUT/bench-after" \
	|| { echo "ab.sh: candidate compile failed" >&2; rm -rf "$OUT"; exit 1; }

echo "running $N interleaved repeats (iterations=$ITER seed=$SEED)..."
mkdir -p "$OUT/before" "$OUT/after"
for t in $(seq 1 "$N"); do
	"$OUT/bench-before" "$ITER" "$SEED" "$N" $OPS > "$OUT/before/$t.csv" &
	p1=$!
	"$OUT/bench-after" "$ITER" "$SEED" "$N" $OPS > "$OUT/after/$t.csv" &
	p2=$!
	wait $p1 $p2
done

# Merge per-op/mode medians across repeats with awk, print speedup.
echo
echo "op,mode,ns_before,ns_after,speedup"
# Gather the union of (op,mode) from the candidate's first run.
cut -d, -f1,2 "$OUT/after/1.csv" | tail -n +2 | sort -u | while IFS=, read -r op mode; do
	med() { # median of ns_median column over all repeat files for a given tree+op+mode
		awk -F, -v o="$op" -v m="$mode" '$1==o && $2==m { print $4 }' "$1"/*.csv | sort -n \
			| awk '{ a[NR]=$1 } END { if (NR==0) { print "nan"; exit } if (NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'
	}
	b=$(med "$OUT/before")
	a=$(med "$OUT/after")
	speedup=$(awk -v b="$b" -v a="$a" 'BEGIN { if (b=="nan"||a=="nan"||a==0) { print "nan" } else { printf "%.3f", b/a } }')
	printf "%s,%s,%s,%s,%s\n" "$op" "$mode" "$b" "$a" "$speedup"
done

echo
echo "oracle_check on candidate..."
( cd "$CAND_SRC" && cc $FLAGS -I include benchmark/oracle.c -o "$OUT/oracle" && \
	cc $FLAGS -I include benchmark/oracle_check.c -o "$OUT/oracle_check" && \
	"$OUT/oracle_check" benchmark/oracle-baseline.txt ) \
	|| echo "oracle_check: FAILED (candidate output differs from baseline)"

rm -rf "$OUT"
