#!/bin/sh
# SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
# SPDX-License-Identifier: MIT
#
# The header every benchmark finding pastes: CPU model, the ISA flags the fast paths care
# about, affinity, load and compiler. Read-only; it changes nothing on the machine.
#   benchmark/cpu_info.sh            (CC selects the compiler reported, default cc)

CC=${CC:-cc}
echo "date:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "host:     $(uname -sm)"

case "$(uname -s)" in
Linux)
	model=$(grep -m1 -E '^(model name|Model)[[:space:]]*:' /proc/cpuinfo | sed 's/^[^:]*:[[:space:]]*//')
	[ -z "$model" ] && command -v lscpu >/dev/null 2>&1 && model=$(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1)
	echo "cpu:      ${model:-unknown}"
	flags=$(grep -m1 -E '^(flags|Features)[[:space:]]*:' /proc/cpuinfo | sed 's/^[^:]*:[[:space:]]*//')
	have=""
	for f in avx2 avx512f avx512dq avx512vl avx512ifma bmi2 adx asimd sve sme; do
		case " $flags " in *" $f "*) have="$have $f" ;; esac
	done
	case "$(uname -m)" in aarch64 | arm64) have="$have neon" ;; esac
	echo "isa:      ${have# }"
	[ -z "$have" ] && echo "          (none of avx2 avx512f/dq/vl/ifma bmi2 adx asimd sve sme)"
	echo "cpus:     $(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN)"
	if command -v taskset >/dev/null 2>&1; then
		echo "affinity: $(taskset -cp $$ 2>/dev/null | sed 's/.*: //')"
	else
		echo "affinity: (taskset not installed)"
	fi
	echo "load:     $(cut -d' ' -f1-3 /proc/loadavg)"
	if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
		echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
	fi
	;;
Darwin)
	echo "cpu:      $(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
	have=""
	[ "$(uname -m)" = arm64 ] && have=" neon"
	for f in FEAT_SME FEAT_SME2 FEAT_BF16 FEAT_I8MM; do
		[ "$(sysctl -n hw.optional.arm.$f 2>/dev/null)" = 1 ] && have="$have $f"
	done
	for f in avx2_0 avx512f bmi2 adx; do
		[ "$(sysctl -n hw.optional.$f 2>/dev/null)" = 1 ] && have="$have $f"
	done
	echo "isa:      ${have# }"
	echo "cpus:     $(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null) performance + $(sysctl -n hw.perflevel1.logicalcpu 2>/dev/null) efficiency"
	echo "affinity: (macOS has no user affinity; the scheduler places threads)"
	echo "load:     $(sysctl -n vm.loadavg 2>/dev/null)"
	;;
*)
	echo "cpu:      unknown ($(uname -s))"
	;;
esac
echo "compiler: $($CC --version 2>/dev/null | head -1)"
