#!/bin/bash
# Regenerate test/data/hardcases/*.txt.  BaCSeL is GPL-2 and is used only as a
# separate tool; nothing here is committed from it.
#
# The committed lists were generated on a member that had FPLLL installed;
# regeneration needs the same toolchain (see NOTES.md).  This script:
#   * clones and pins BaCSeL at the commit in $SHA,
#   * builds the monotonic searches (LOG/LOG2/LOG10/RSQRT),
#   * for the large-argument sin/cos/tan searches, first tries the SINBIG
#     build (SINBIG covers only sin and needs FPLLL plus the -Q period argument
#     at run time).  If it builds and a probe search returns a case, it is used
#     for the sin list (a full per-binade sweep over 16..46); otherwise all
#     three fall back to the ordinary SIN/COS/TAN scan of the first 2^16 raws
#     of each binade over 16..20.
#   * uses the *adaptive* value of k: k is started at e+11 for the monotonic
#     functions (10 for the trig window) and lowered until a binade yields at
#     least 8 cases, and truncated to at most 256 cases, and
#   * records the exact per-binade command and the k chosen for it in the
#     list header, so the lists are reproducible from this script.
set -euo pipefail
SHA=e372414bc6af4a2911e07abe03de9854f32561fd
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${BAC_DIR:-"$ROOT/.bacsel"}
CACHE=${BAC_CACHE:-"$WORK/cache"}
mkdir -p "$CACHE"
if [ ! -d "$CACHE/bacsel/.git" ]; then
  git clone https://gitlab.inria.fr/zimmerma/bacsel.git "$CACHE/bacsel"
fi
git -C "$CACHE/bacsel" fetch -q origin
git -C "$CACHE/bacsel" checkout -q "$SHA"
BAC="$CACHE/bacsel"
# FPLLL headers: set FPLLL_INC to the directory holding fplll/fplll.h if
# pkg-config cannot find them.
if [ -n "${FPLLL_INC:-}" ]; then INC="-I$FPLLL_INC/fplll -I$FPLLL_INC"; else
  INC=$(pkg-config --cflags fplll 2>/dev/null || true)
  INC="$INC -I/usr/include/fplll"
fi
LIB=${FPLLL_LIB:-"-lfplll -lmpfr -lgmp -lm -fopenmp"}
cc -O2 "$ROOT/tools/bacsel/check_hard.c" -lmpfr -lgmp -o "$CACHE/check_hard"
CH="$CACHE/check_hard"

build() { # $1 = DEFSAL
  make -C "$BAC" clean >/dev/null 2>&1 || true
  make -C "$BAC" -j3 MPFR=/usr GMP=/usr FPLLL="$INC" INC="$INC" LIB="$LIB" \
    DEFSAL="$1" >/dev/null
}

# --- SINBIG probe for large-argument sin ---
# SINBIG covers only sin and requires the -Q period argument at run time and
# FPLLL.  Try to build it; if it builds and a probe search (e = 16) returns at
# least one case, mark it usable.  The Python generation below then uses it for
# the sin list; cos/tan have no SINBIG equivalent and always use the ordinary
# COS/TAN window scan.  The -Q value below is the period parameter used in
# BaCSeL's own README example; it must be re-verified on a machine where the
# SINBIG build actually runs (see NOTES.md).
SINBIG_OK=0
if build "-DSINBIG -DBASIS=2 -DAUTOMATIC -DMONOTONIC"; then
  if "$BAC/bacsel" -rnd_mode nearest -prec 200 -n 33 -nn 32 -m 10 -t 7 \
       -Q 15106909301 -t0 $((1<<32)) -t1 $((1<<33)) -d 2 -alpha 2 \
       -e_in 17 -nthreads 3 -q | grep -q '\*\*\*'; then
    SINBIG_OK=1
  fi
fi

python3 - "$BAC" "$CH" "$ROOT" "$SINBIG_OK" <<'PY'
import os, subprocess, sys, datetime
bac, ch, root, sinbig_ok = sys.argv[1:5]
sinbig_ok = sinbig_ok == "1"
sha = "e372414bc6af4a2911e07abe03de9854f32561fd"
outdir = os.path.join(root, "test/data/hardcases")
builds = {"ln":("-DLOG",True),"log2":("-DLOG2",True),"log10":("-DLOG10",True),
          "rsqrt":("-DRSQRT",True),"sin":("-DSIN",False),"cos":("-DCOS",False),
          "tan":("-DTAN",False)}

def build_defs(defs):
    subprocess.run(["make","-C",bac,"clean"],
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    subprocess.run(["make","-C",bac,"-j3","MPFR=/usr","GMP=/usr",
      "FPLLL="+os.environ.get("FPLLL_INC",""),"INC="+os.environ.get("FPLLL_INC",""),
      "LIB="+os.environ.get("FPLLL_LIB","-lfplll -lmpfr -lgmp -lm -fopenmp"),
      "DEFSAL="+defs],
      stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,check=True)

def build(fn, extra=""):
    d,_=builds[fn]
    build_defs(d+" "+extra+" -DBASIS=2 -DVBSE -DAUTOMATIC -DMONOTONIC")

def getexp(fn,raw):
    return int(subprocess.check_output([ch,"--getexp",fn,str(raw)],text=True).split()[0])

def run_common(fn,e,k,nn,t,t0,t1,extra):
    n=e+17
    cmd=[bac+"/bacsel","-rnd_mode","nearest","-prec",str(max(160,2*(n+abs(nn)))),
         "-n",str(n),"-nn",str(nn),"-m",str(k),"-t",str(t),"-t0",str(t0),
         "-t1",str(t1),"-d","2","-alpha","2","-e_in",str(e+1),
         "-nthreads","3","-q"]+extra
    p=subprocess.run(cmd,capture_output=True,text=True)
    if p.returncode != 0:
        sys.stderr.write("bacsel failed for %s e=%d k=%d rc=%d\n"%(fn,e,k,p.returncode))
        return None
    c=subprocess.run([ch,"--convert",fn,str(e),str(k)],input=p.stdout,
                      capture_output=True,text=True)
    rows=[]
    for line in c.stdout.splitlines():
        a=line.split()
        if len(a)==3: rows.append((int(a[0]),int(a[1]),float(a[2])))
    return rows

def run_window(fn,e,k):
    t0=1<<(e+16); t1=t0+(1<<16); nn=16; t=7
    return run_common(fn,e,k,nn,t,t0,t1,[])

def run_mono(fn,e,k):
    t0=1<<(e+16); t1=1<<(e+17); nn=getexp(fn,t0)+16; t=12
    return run_common(fn,e,k,nn,t,t0,t1,[])

def run_sinbig(fn,e,k):
    t0=1<<(e+16); t1=1<<(e+17); nn=getexp(fn,t0)+16; t=12
    return run_common(fn,e,k,nn,t,t0,t1,["-Q","15106909301"])

def collect(fn, run, es, kinit):
    rows=[]; meta=[]
    for e in es:
        k=kinit(e); lo=3; r=None
        while r is None or len(r)<8:
            r=run(fn,e,k)
            if r is None: break
            if k<=lo: break
            if len(r)<8: k-=1
        if r is None: continue
        if len(r)>256: r=sorted(r,key=lambda x:x[2])[:256]
        r.sort()
        if r: rows+=r; meta.append((e,k))
    return rows, meta

def write_list(fn, rows, meta, header_line, kline):
    name = fn + ("_big" if fn in ("sin","cos","tan") else "")
    with open(os.path.join(outdir,name+".txt"),"w") as f:
        f.write("# BaCSeL sha: %s\n"%sha)
        f.write("# command: %s\n"%header_line)
        f.write("# %s\n"%kline)
        f.write("# k per binade: "+" ".join("%d:%d"%m for m in meta)+"\n")
        f.write("# date: %s\n"%datetime.date.today().isoformat())
        for raw,e,d in rows: f.write("%d %d %.17g\n"%(raw,e,d))
    print(fn, len(rows))

MONO_ES = range(-10,21)
TRIG_FB_ES = range(16,21)
TRIG_BIG_ES = range(16,47)

# monotonic builders (full-binade searches over the committed ranges)
for fn,(dflg,mono) in builds.items():
    if fn in ("sin","cos","tan"): continue
    build(fn)
    rows, meta = collect(fn, run_mono, MONO_ES, lambda e: e+11)
    h = "./bacsel -rnd_mode nearest -prec max(160,2*(n+|nn|)) -n e+17 "
    h += "-nn (eo+16) -m k -t 12 -t0 2^(e+16) -t1 2^(e+17) -d 2 "
    h += "-alpha 2 -e_in e+1 -nthreads 3 -q"
    write_list(fn, rows, meta, h, "k = e + 11 (lowered per binade to reach >= 8 cases)")

# cos / tan: ordinary COS/TAN window scan over 16..20 (no SINBIG equivalent)
for fn in ("cos","tan"):
    build(fn)
    rows, meta = collect(fn, run_window, TRIG_FB_ES, lambda e: 10)
    h = "./bacsel -rnd_mode nearest -prec max(160,2*(n+16)) -n e+17 -nn 16 "
    h += "-m k -t 7 -t0 2^(e+16) -t1 2^(e+16)+2^16 -d 2 -alpha 2 "
    h += "-e_in e+1 -nthreads 3 -q (e = input binade, 16..20; k = 10; "
    h += "the first 2^16 raws of each binade)"
    write_list(fn, rows, meta, h, "k = 10; searched the first 2^16 raw inputs of each binade")

# sin: use SINBIG (full per-binade sweep over 16..46) when the probe succeeded,
# else fall back to the ordinary SIN window scan over 16..20.
if sinbig_ok:
    build_defs("-DSINBIG -DBASIS=2 -DAUTOMATIC -DMONOTONIC")
    probe = run_sinbig("sin",16,10)
    if probe is not None and len(probe) >= 8:
        rows, meta = collect("sin", run_sinbig, TRIG_BIG_ES, lambda e: 10)
        h = "./bacsel -rnd_mode nearest -prec max(160,2*(n+|nn|)) -n e+17 "
        h += "-nn (eo+16) -m k -t 12 -Q 15106909301 -t0 2^(e+16) "
        h += "-t1 2^(e+17) -d 2 -alpha 2 -e_in e+1 -nthreads 3 -q "
        h += "(SINBIG build; e = 16..46; k lowered per binade to >= 8 cases)"
        write_list("sin", rows, meta, h, "k = 10 (lowered per binade to reach >= 8 cases); SINBIG -Q 15106909301")
        sys.exit(0)
build("sin")
rows, meta = collect("sin", run_window, TRIG_FB_ES, lambda e: 10)
h = "./bacsel -rnd_mode nearest -prec max(160,2*(n+16)) -n e+17 -nn 16 "
h += "-m k -t 7 -t0 2^(e+16) -t1 2^(e+16)+2^16 -d 2 -alpha 2 "
h += "-e_in e+1 -nthreads 3 -q (e = input binade, 16..20; k = 10; "
h += "the first 2^16 raws of each binade)"
write_list("sin", rows, meta, h, "k = 10; searched the first 2^16 raw inputs of each binade")
PY
