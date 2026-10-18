#!/usr/bin/env bash
# Build BaCSeL and regenerate test/data/hardcases/*.txt for Q48.16.
#
# Usage:
#   bash tools/bacsel/run.sh                # build BaCSeL, generate & verify all 7 lists
#   bash tools/bacsel/run.sh --check-only   # compile check_hard and verify all 7 lists
#
# All seven lists cover their full Q48.16 binade range with at least 8 cases
# per binade (or all 2^(e+16) inputs when e < -13) and at most 256 cases per
# binade:
#   - ln, log2, log10, rsqrt : e = -16 .. 46  (BaCSeL sha e372414)
#   - cos_big, tan_big       : e =  16 .. 46  (BaCSeL sha e372414)
#   - sin_big                : e =  16 .. 46  (BaCSeL sha e17f67c, -DSINBIG -Q 2470649)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${BAC_DIR:-"$ROOT/.bacsel"}
CACHE=${BAC_CACHE:-"$WORK/cache"}
mkdir -p "$CACHE"

CH="$CACHE/check_hard"
gcc -O2 -Wall -Wextra -Werror "$ROOT/tools/bacsel/check_hard.c" -o "$CH" -lmpfr -lgmp

check_all() {
  "$CH" --check ln    "$ROOT/test/data/hardcases/ln.txt"
  "$CH" --check log2  "$ROOT/test/data/hardcases/log2.txt"
  "$CH" --check log10 "$ROOT/test/data/hardcases/log10.txt"
  "$CH" --check rsqrt "$ROOT/test/data/hardcases/rsqrt.txt"
  "$CH" --check sin   "$ROOT/test/data/hardcases/sin_big.txt"
  "$CH" --check cos   "$ROOT/test/data/hardcases/cos_big.txt"
  "$CH" --check tan   "$ROOT/test/data/hardcases/tan_big.txt"
}

if [ "${1:-}" = "--check-only" ]; then
  check_all
  exit 0
fi

SHA_MAIN="e372414bc6af4a2911e07abe03de9854f32561fd"
SHA_SINBIG="e17f67c"

if [ ! -d "$CACHE/bacsel/.git" ]; then
  git clone https://gitlab.inria.fr/zimmerma/bacsel.git "$CACHE/bacsel"
fi
BAC="$CACHE/bacsel"

# Locate or build FPLLL (headers + libfplll) if not in system paths.
FPLLL_PREFIX="$CACHE/fplll-install"
if [ -n "${FPLLL_INC:-}" ]; then
  INC="-I$FPLLL_INC/fplll -I$FPLLL_INC"
elif [ -f "/usr/include/fplll/fplll.h" ] || pkg-config --exists fplll 2>/dev/null; then
  INC="$(pkg-config --cflags fplll 2>/dev/null || true) -I/usr/include/fplll"
else
  if [ ! -f "$FPLLL_PREFIX/include/fplll/fplll.h" ]; then
    if [ ! -d "$CACHE/fplll-src/.git" ]; then
      git clone --depth 1 --branch 5.5.0 https://github.com/fplll/fplll.git "$CACHE/fplll-src"
    fi
    (
      cd "$CACHE/fplll-src"
      if [ ! -f configure ]; then ./autogen.sh; fi
      ./configure --prefix="$FPLLL_PREFIX" --disable-static
      make -j4
      make install
    )
  fi
  INC="-I$FPLLL_PREFIX/include/fplll -I$FPLLL_PREFIX/include"
  FPLLL_LIB="-L$FPLLL_PREFIX/lib -Wl,-rpath,$FPLLL_PREFIX/lib -lfplll -lmpfr -lgmp -lm -fopenmp"
fi
LIB=${FPLLL_LIB:-"-lfplll -lmpfr -lgmp -lm -fopenmp"}
export FPLLL_INC="$INC"
export FPLLL_LIB="$LIB"

python3 - "$BAC" "$CH" "$ROOT" "$SHA_MAIN" "$SHA_SINBIG" <<'PY'
import os, subprocess, sys, datetime

bac, ch, root, sha_main, sha_sinbig = sys.argv[1:6]
outdir = os.path.join(root, "test/data/hardcases")
inc = os.environ["FPLLL_INC"]
lib = os.environ["FPLLL_LIB"]

builds = {
    "ln":    ("-DLOG",   True),
    "log2":  ("-DLOG2",  True),
    "log10": ("-DLOG10", True),
    "rsqrt": ("-DRSQRT", True),
    "sin":   ("-DSIN",   False),
    "cos":   ("-DCOS",   False),
    "tan":   ("-DTAN",   False),
}

def checkout(sha):
    subprocess.run(["git", "-C", bac, "checkout", "-f", "-q", sha], check=True)

def build_defs(defs):
    subprocess.run(["make", "-C", bac, "clean"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["make", "-C", bac, "-j4", "MPFR=/usr", "GMP=/usr",
                    "FPLLL=" + inc, "INC=" + inc, "LIB=" + lib,
                    "DEFSAL=" + defs],
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, check=True)

def build(fn, extra=""):
    d, _ = builds[fn]
    build_defs(d + " " + extra + " -DBASIS=2 -DVBSE -DAUTOMATIC -DMONOTONOUS")

def get_window(fn, e, log2w=12):
    out = subprocess.check_output([ch, "--window", fn, str(e), str(log2w)], text=True).split()
    return int(out[0]), int(out[1]), int(out[2])

def run_common(fn, e, k, nn, t, t0, t1, d_deg, alpha, extra):
    n = e + 17
    nn_pos = max(2, nn)
    prec = max(160, 2 * (n + abs(nn)) + 64)
    rnd_mode = "all" if d_deg == 0 else "nearest"
    m_arg = 1 if d_deg == 0 else k
    cmd = [bac + "/bacsel", "-rnd_mode", rnd_mode, "-prec", str(prec),
           "-n", str(n), "-nn", str(nn_pos), "-m", str(m_arg), "-t", str(t),
           "-t0", str(t0), "-t1", str(t1), "-d", str(d_deg), "-alpha", str(alpha),
           "-e_in", str(e + 1), "-nthreads", "3", "-q"] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return None
    c = subprocess.run([ch, "--convert", fn, str(e), str(k)],
                       input=p.stdout, capture_output=True, text=True)
    rows = []
    seen = set()
    for line in c.stdout.splitlines():
        a = line.split()
        if len(a) == 3:
            raw = int(a[0])
            if raw not in seen:
                seen.add(raw)
                rows.append((raw, int(a[1]), float(a[2])))
    return rows

def run_mono(fn, e, k):
    t0, t1, ex = get_window(fn, e, 12)
    nn = ex + 16
    span = t1 - t0
    if e <= -4 or span < 4096 or k <= 3:
        # Small binade or coarse k fallback: use BaCSeL naive_search (-d 0 -t 1)
        if span > 4096:
            t1 = t0 + 4096
        return run_common(fn, e, k, nn, 1, t0, t1, 0, 1, [])
    return run_common(fn, e, k, nn, 12, t0, t1, 2, 2, [])

def run_window(fn, e, k):
    t0 = 1 << (e + 16)
    t1 = t0 + (1 << 16)
    nn = 16
    if k <= 3:
        return run_common(fn, e, k, nn, 1, t0, t0 + 4096, 0, 1, [])
    return run_common(fn, e, k, nn, 7, t0, t1, 2, 2, [])

def run_sinbig(fn, e, k):
    Q = 2470649
    t0 = 1 << (e + 16)
    t1 = min(1 << (e + 17), t0 + Q * 256)
    nn = 16
    return run_common(fn, e, k, nn, 4, t0, t1, 1, 1,
                      ["-Q", str(Q), "-nbr", "32"])

def collect(fn, run, es, kinit):
    rows = []
    meta = []
    for e in es:
        min_req = (1 << (e + 16)) if (e + 16 < 3) else 8
        k = max(1, kinit(e))
        r = None
        while True:
            r = run(fn, e, k)
            if r is not None and len(r) >= min_req:
                break
            if k <= 1:
                break
            k -= 1
        if not r or len(r) < min_req:
            raise RuntimeError(f"failed to collect >= {min_req} cases for {fn} e={e}")
        if len(r) > 256:
            r = sorted(r, key=lambda x: (x[2], x[0]))[:256]
        r.sort()
        rows += r
        meta.append((e, k))
    return rows, meta

def write_list(fn, sha, rows, meta, header_line, kline):
    name = fn + ("_big" if fn in ("sin", "cos", "tan") else "")
    with open(os.path.join(outdir, name + ".txt"), "w") as f:
        f.write("# BaCSeL sha: %s\n" % sha)
        f.write("# command: %s\n" % header_line)
        f.write("# %s\n" % kline)
        f.write("# k per binade: " + " ".join("%d:%d" % m for m in meta) + "\n")
        f.write("# date: %s\n" % datetime.date.today().isoformat())
        for raw, e, d in rows:
            f.write("%d %d %.17g\n" % (raw, e, d))
    print(f"{name}: {len(rows)} cases across {len(meta)} binades ({meta[0][0]}..{meta[-1][0]})")

MONO_ES = range(-16, 47)
TRIG_ES = range(16, 47)

# 1. Monotonic functions (ln, log2, log10, rsqrt) over e = -16 .. 46
checkout(sha_main)
for fn in ("ln", "log2", "log10", "rsqrt"):
    build(fn)
    rows, meta = collect(fn, run_mono, MONO_ES, lambda e: min(16, max(1, e + 11)))
    h = ("./bacsel -rnd_mode nearest -prec max(160,2*(n+|nn|)+64) -n max(2,e+17) "
         "-nn (eo+16) -m k -t 12 -t0 <win_t0> -t1 <win_t1> -d 2 -alpha 2 "
         "-e_in e+1 -nthreads 3 -q (e = -16..46; -d 0 -t 1 on e <= -4)")
    write_list(fn, sha_main, rows, meta, h,
               "k = min(16, max(1, e + 11)) (lowered per binade to reach >= min(8, 2^(e+16)) cases)")

# 2. cos_big and tan_big over e = 16 .. 46
for fn in ("cos", "tan"):
    build(fn)
    rows, meta = collect(fn, run_window, TRIG_ES, lambda e: 10)
    h = ("./bacsel -rnd_mode nearest -prec max(160,2*(n+16)+64) -n e+17 -nn 16 "
         "-m k -t 7 -t0 2^(e+16) -t1 2^(e+16)+2^16 -d 2 -alpha 2 "
         "-e_in e+1 -nthreads 3 -q (e = 16..46; first 2^16 raws of each binade)")
    write_list(fn, sha_main, rows, meta, h,
               "k = 10 (lowered per binade to reach >= 8 cases); first 2^16 raw inputs of each binade")

# 3. sin_big over e = 16 .. 46 via SINBIG (pinned sha e17f67c, Q = 2470649)
checkout(sha_sinbig)
build_defs("-DSINBIG -DBASIS=2 -DAUTOMATIC -DMONOTONOUS")
rows, meta = collect("sin", run_sinbig, TRIG_ES, lambda e: 10)
h = ("./bacsel -rnd_mode nearest -prec max(160,2*(n+16)+64) -n e+17 -nn 16 "
     "-m k -t 4 -Q 2470649 -nbr 32 -t0 2^(e+16) -t1 min(2^(e+17),2^(e+16)+2470649*256) "
     "-d 1 -alpha 1 -e_in e+1 -nthreads 3 -q (SINBIG build; e = 16..46)")
write_list("sin", sha_sinbig, rows, meta, h,
           "k = 10 (lowered per binade to reach >= 8 cases); SINBIG -Q 2470649 -nbr 32")
PY

check_all
