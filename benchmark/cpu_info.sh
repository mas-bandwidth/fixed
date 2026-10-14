#!/bin/sh
# CPU model, ISA flags, affinity, load average, compiler: the header every finding pastes.
set -u

echo "== CPU =="
model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | tr -s ' ' | sed 's/^ //')
[ -z "$model" ] && model="(unknown: /proc/cpuinfo not readable)"
echo "model: $model"
echo "arch: $(uname -m)"
echo "cores: $(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo '?')"

echo
echo "== ISA flags =="
flags=$(grep -m1 '^flags' /proc/cpuinfo 2>/dev/null | cut -d: -f2)
for f in avx2 avx512f avx512dq avx512vl avx512ifma bmi2 adx sse4_2 sse4a; do
	if echo "$flags" | grep -qw "$f"; then
		echo "  $f: yes"
	else
		echo "  $f: no"
	fi
done
# arm64
if grep -qi 'Features' /proc/cpuinfo 2>/dev/null; then
	feats=$(grep -m1 'Features' /proc/cpuinfo 2>/dev/null | cut -d: -f2)
	for f in neon sme; do
		if echo "$feats" | grep -qw "$f"; then echo "  $f: yes"; else echo "  $f: no"; fi
	done
fi

echo
echo "== affinity =="
if command -v taskset >/dev/null 2>&1; then
	taskset -pc $$ 2>/dev/null || echo "  (taskset unavailable)"
elif command -v cpuset >/dev/null 2>&1; then
	cpuset -p $$ 2>/dev/null || echo "  (cpuset unavailable)"
else
	echo "  (no affinity tool)"
fi

echo
echo "== load average =="
cat /proc/loadavg 2>/dev/null || echo "  (unavailable)"

echo
echo "== compiler =="
if command -v clang >/dev/null 2>&1; then clang --version | head -1; fi
if command -v gcc >/dev/null 2>&1; then gcc --version | head -1; fi
