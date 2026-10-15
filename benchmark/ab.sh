#!/bin/sh
# ab.sh -- interleaved A/B of the SAME benchmark source against two trees.
#
#   benchmark/ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]
#
# The same benchmark/op_bench.c is compiled against each tree's include/ and src/ with the
# flags benchmark/README.md names, so a difference in the table is a difference in the
# library and never in the harness. The two binaries are run INTERLEAVED (A, B, A, B, ...)
# for N repeats rather than all of A then all of B: drift in clock, thermals or background
# load then lands on both trees instead of on one of them.
#
# The output oracle is compiled the same way against the CANDIDATE, not taken from a
# prebuilt build/fixed_oracle_check (that binary is the tree this script was launched
# from). benchmark/oracle_check.c is linked with $CAND_DIR/benchmark/oracle.c and the
# candidate's include/ and src/. If that compile fails, ab.sh fails.
#
# REFUSES TO RUN WHILE ANOTHER BENCHMARK IS ON THE BOX. It looks with pgrep -af and exits
# non-zero. It never kills anything: a number measured next to another benchmark is not a
# number, and the process belongs to whoever started it.
#
# Environment:
#   AB_REPEATS   repeats per tree (default 7)
#   AB_ITERS     iterations per repeat (default 200000)
#   CC           compiler (default cc)
set -eu

usage() {
    echo "usage: $0 <baseline-src-dir> <candidate-src-dir> [op ...]" >&2
    exit 2
}

[ $# -ge 2 ] || usage
BASE_DIR=$1
CAND_DIR=$2
shift 2
OPS=$*

REPEATS=${AB_REPEATS:-7}
ITERS=${AB_ITERS:-200000}
CC=${CC:-cc}
FLAGS="-O3 -g -DNDEBUG -ffp-contract=off"

for d in "$BASE_DIR" "$CAND_DIR"; do
    [ -d "$d/include/fixed" ] || { echo "ab.sh: $d is not a fixed source tree (no include/fixed)" >&2; exit 2; }
    [ -f benchmark/op_bench.c ] || { echo "ab.sh: run this from a fixed checkout (no benchmark/op_bench.c)" >&2; exit 2; }
done

# Another benchmark on the box invalidates both trees' numbers. Report and refuse.
if command -v pgrep >/dev/null 2>&1; then
    running=$(pgrep -af 'op_bench|numerical_bench|oracle_check' 2>/dev/null | grep -v "ab.sh" | grep -v "pgrep" || true)
    if [ -n "$running" ]; then
        echo "ab.sh: refusing to run, benchmark processes are already on this box:" >&2
        echo "$running" | sed 's/^/  /' >&2
        echo "ab.sh: it will not kill them. Wait for them, or stop them yourself." >&2
        exit 3
    fi
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/fixed-ab.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

echo "# ab.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# compiler: $($CC --version | head -1)"
echo "# flags: $FLAGS"
echo "# repeats: $REPEATS  iterations: $ITERS"
sh benchmark/cpu_info.sh 2>/dev/null | sed 's/^/# /' || true

build() {
    dir=$1
    out=$2
    # shellcheck disable=SC2086
    $CC $FLAGS -I "$dir/include" benchmark/op_bench.c "$dir/src/fixed_math.c" "$dir/src/fixed_vec.c" \
        -lm -o "$out"
}

build "$BASE_DIR" "$WORK/bench-a"
build "$CAND_DIR" "$WORK/bench-b"

# Same shape as build(), but the engine is the candidate's oracle.c and the driver is
# this tree's oracle_check.c. Fail loud if the candidate cannot be checked.
build_oracle() {
    dir=$1
    out=$2
    # shellcheck disable=SC2086
    $CC $FLAGS -I "$dir/include" benchmark/oracle_check.c "$dir/benchmark/oracle.c" \
        "$dir/src/fixed_math.c" "$dir/src/fixed_vec.c" -lm -o "$out" || return 1
}

i=1
while [ "$i" -le "$REPEATS" ]; do
    # shellcheck disable=SC2086
    "$WORK/bench-a" 1 "$ITERS" $OPS > "$WORK/a-$i.csv"
    # shellcheck disable=SC2086
    "$WORK/bench-b" 1 "$ITERS" $OPS > "$WORK/b-$i.csv"
    i=$((i + 1))
done

# The candidate must also be bit-identical: a faster tree that moved an output is a FAIL,
# not a speedup. This binary is the one just compiled against $CAND_DIR.
echo "# oracle_check compiled against $CAND_DIR:"
if ! build_oracle "$CAND_DIR" "$WORK/oracle_check"; then
    echo "ab.sh: cannot build oracle_check against $CAND_DIR" >&2
    exit 1
fi
"$WORK/oracle_check" || { echo "ab.sh: candidate FAILED the output oracle; the table below is moot" >&2; exit 1; }

awk -v reps="$REPEATS" -v work="$WORK" '
function median(a, n,   b, i, j, t) {
    for (i = 1; i <= n; i++) b[i] = a[i]
    for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (b[j] < b[i]) { t = b[i]; b[i] = b[j]; b[j] = t }
    return (n % 2) ? b[(n + 1) / 2] : (b[n / 2] + b[n / 2 + 1]) / 2
}
BEGIN {
    FS = ","
    for (r = 1; r <= reps; r++) {
        while ((getline line < (work "/a-" r ".csv")) > 0) {
            n = split(line, f, ",")
            if (f[1] == "op") continue
            key = f[1] "," f[2]
            an[key]++
            av[key "," an[key]] = f[4] + 0
            cs[key] = f[6]
            if (!(key in seen)) { seen[key] = 1; order[++count] = key }
        }
        close(work "/a-" r ".csv")
        while ((getline line < (work "/b-" r ".csv")) > 0) {
            n = split(line, f, ",")
            if (f[1] == "op") continue
            key = f[1] "," f[2]
            bn[key]++
            bv[key "," bn[key]] = f[4] + 0
            if (cs[key] != "" && cs[key] != f[6]) mismatch[key] = 1
            if (!(key in seen)) { seen[key] = 1; order[++count] = key }
        }
        close(work "/b-" r ".csv")
    }
    printf "%-30s %-7s %10s %10s %8s %10s %10s %8s %8s\n", \
        "op", "mode", "a_min", "a_med", "a_spread", "b_min", "b_med", "b_spread", "speedup"
    for (i = 1; i <= count; i++) {
        key = order[i]
        split(key, kp, ",")
        na = an[key]; nb = bn[key]
        if (na == 0 || nb == 0) continue
        amin = av[key ",1"]; amax = amin
        for (j = 2; j <= na; j++) { if (av[key "," j] < amin) amin = av[key "," j]; if (av[key "," j] > amax) amax = av[key "," j] }
        bmin = bv[key ",1"]; bmax = bmin
        for (j = 2; j <= nb; j++) { if (bv[key "," j] < bmin) bmin = bv[key "," j]; if (bv[key "," j] > bmax) bmax = bv[key "," j] }
        for (j = 1; j <= na; j++) aa[j] = av[key "," j]
        for (j = 1; j <= nb; j++) bb[j] = bv[key "," j]
        amed = median(aa, na); bmed = median(bb, nb)
        asp = (amed > 0) ? 100 * (amax - amin) / amed : 0
        bsp = (bmed > 0) ? 100 * (bmax - bmin) / bmed : 0
        sp = (bmed > 0) ? amed / bmed : 0
        flag = (key in mismatch) ? "  <-- CHECKSUM DIFFERS: OUTPUT BITS MOVED" : ""
        printf "%-30s %-7s %10.4f %10.4f %7.1f%% %10.4f %10.4f %7.1f%% %8.3f%s\n", \
            kp[1], kp[2], amin, amed, asp, bmin, bmed, bsp, sp, flag
    }
    print "# speedup = a_median / b_median; > 1 means the candidate is faster."
    print "# spread = (max - min) / median over the interleaved repeats: the floor a claim must beat."
}'
