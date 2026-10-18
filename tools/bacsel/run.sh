#!/bin/bash
# Regenerate test/data/hardcases/*.txt.  BaCSeL is GPL-2 and is used only as a
# separate tool; nothing here is committed from it.
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
build() { # $1 = DEFSAL
  make -C "$BAC" clean >/dev/null 2>&1 || true
  make -C "$BAC" -j3 MPFR=/usr GMP=/usr FPLLL="$INC" INC="$INC" LIB="$LIB" DEFSAL="$1" >/dev/null
}
build "-DSINBIG -DBASIS=2 -DAUTOMATIC -DMONOTONIC" || true
# The committed sin/cos/tan data came from the ordinary SIN/COS/TAN builds;
# SINBIG produced no cases with FPLLL 5.5.0 (see NOTES.md).
build "-DSIN -DBASIS=2 -DVBSE -DAUTOMATIC -DMONOTONIC"
python3 - "$BAC" "$CACHE/check_hard" "$ROOT" <<'PY'
import os, subprocess, sys, datetime
bac, ch, root = sys.argv[1:4]
sha = "e372414bc6af4a2911e07abe03de9854f32561fd"
outdir = os.path.join(root, "test/data/hardcases")
builds = {"ln":("-DLOG",True),"log2":("-DLOG2",True),"log10":("-DLOG10",True),
          "rsqrt":("-DRSQRT",True),"sin":("-DSIN",False),"cos":("-DCOS",False),
          "tan":("-DTAN",False)}
def build(fn):
    d,_=builds[fn]
    subprocess.run(["make","-C",bac,"clean"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    subprocess.run(["make","-C",bac,"-j3","MPFR=/usr","GMP=/usr",
      "FPLLL="+os.environ.get("FPLLL_INC",""),"INC="+os.environ.get("FPLLL_INC",""),
      "LIB="+os.environ.get("FPLLL_LIB","-lfplll -lmpfr -lgmp -lm -fopenmp"),
      "DEFSAL="+d+" -DBASIS=2 -DVBSE -DAUTOMATIC -DMONOTONIC"],
      stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,check=True)
def getexp(fn,raw):
    return int(subprocess.check_output([ch,"--getexp",fn,str(raw)],text=True).split()[0])
def run(fn,e,k):
    mono=builds[fn][1]; n=e+17; t0=1<<(e+16)
    if mono:
        t1=1<<(e+17); t=12; nn=getexp(fn,t0)+16
    else:
        t1=t0+(1<<16); t=7; nn=16
    cmd=[bac+"/bacsel","-rnd_mode","nearest","-prec",str(max(160,2*(n+abs(nn)))),
         "-n",str(n),"-nn",str(nn),"-m",str(k),"-t",str(t),"-t0",str(t0),
         "-t1",str(t1),"-d","2","-alpha","2","-e_in",str(e+1),"-nthreads","3","-q"]
    p=subprocess.run(cmd,capture_output=True,text=True)
    c=subprocess.run([ch,"--convert",fn,str(e),str(k)],input=p.stdout,capture_output=True,text=True)
    rows=[]
    for line in c.stdout.splitlines():
        a=line.split()
        if len(a)==3: rows.append((int(a[0]),int(a[1]),float(a[2])))
    return rows
for fn,(_,mono) in builds.items():
    build(fn); rows=[]; meta=[]
    for e in range(-13,21) if mono else range(16,21):
        k=e+11 if mono else 10
        r=run(fn,e,k)
        if len(r)>256: r=sorted(r,key=lambda x:x[2])[:256]
        r.sort()
        if r: rows+=r; meta.append((e,k))
    name = fn + ("_big" if fn in ("sin","cos","tan") else "")
    with open(os.path.join(outdir,name+".txt"),"w") as f:
        f.write("# BaCSeL sha: %s\n"%sha)
        f.write("# command: see tools/bacsel/run.sh for the exact per-binade command\n")
        f.write("# k = %s\n"%("e + 11" if mono else "10"))
        f.write("# k per binade: "+" ".join("%d:%d"%m for m in meta)+"\n")
        f.write("# date: %s\n"%datetime.date.today().isoformat())
        for raw,e,d in rows: f.write("%d %d %.17g\n"%(raw,e,d))
    print(fn, len(rows))
PY
