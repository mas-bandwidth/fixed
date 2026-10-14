#!/bin/sh
# cpu_info.sh -- the header every finding pastes.
#
# A benchmark number with no machine attached to it is not a measurement. This prints the
# machine, the ISA the compiler could have used, the scheduling environment and the
# compiler, so that a table in a pull request can be read: it says what it ran on, whether
# the box was idle, and whether the vector ISA a spike claims to use is even present.
# Nothing here is killed, adjusted or pinned; this script only reads.
set -u

echo "# cpu_info $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname: $(uname -srm)"

if [ -r /proc/cpuinfo ]; then
    model=$(grep -m1 -E 'model name|Model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')
    echo "cpu_model: ${model:-unknown}"
    flags=$(grep -m1 -E '^flags' /proc/cpuinfo | cut -d: -f2-)
    for f in avx2 avx512f avx512dq avx512vl avx512ifma bmi2 adx fma sse4_2 neon sme; do
        case " $flags " in
            *" $f "*) echo "isa_$f: yes" ;;
            *) echo "isa_$f: no" ;;
        esac
    done
    echo "logical_cpus: $(grep -c ^processor /proc/cpuinfo)"
elif [ "$(uname -s)" = "Darwin" ]; then
    echo "cpu_model: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
    echo "logical_cpus: $(sysctl -n hw.logicalcpu 2>/dev/null || echo unknown)"
    for f in neon fp16 i8mm sme sme2; do
        if sysctl -n "hw.optional.arm.FEAT_$f" >/dev/null 2>&1; then
            echo "isa_$f: $(sysctl -n "hw.optional.arm.FEAT_$f")"
        else
            echo "isa_$f: unknown"
        fi
    done
fi

# Affinity: a benchmark that can migrate between heterogeneous cores is not comparable to
# one that cannot. Reported, never set.
if command -v taskset >/dev/null 2>&1; then
    echo "affinity: $(taskset -pc $$ 2>/dev/null | sed 's/.*: //')"
else
    echo "affinity: taskset unavailable"
fi

echo "load_average:$(cat /proc/loadavg 2>/dev/null || sysctl -n vm.loadavg 2>/dev/null || echo unknown)"

# Benchmark processes already on the box. This script NEVER kills anything; it reports.
if command -v pgrep >/dev/null 2>&1; then
    running=$(pgrep -af 'op_bench|numerical_bench|oracle_check' 2>/dev/null | grep -v "pgrep" || true)
    if [ -n "$running" ]; then
        echo "benchmarks_running: YES"
        echo "$running" | sed 's/^/  /'
    else
        echo "benchmarks_running: no"
    fi
fi

for cc in "${CC:-cc}" clang gcc; do
    if command -v "$cc" >/dev/null 2>&1; then
        echo "compiler_$cc: $("$cc" --version 2>/dev/null | head -1)"
    fi
done
command -v cmake >/dev/null 2>&1 && echo "cmake: $(cmake --version | head -1)"
command -v make >/dev/null 2>&1 && echo "make: $(make --version | head -1)"
