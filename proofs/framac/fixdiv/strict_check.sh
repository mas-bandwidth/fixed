#!/bin/sh
# Check Frama-C WP's per-goal JSON verdicts, not just its process exit or totals.
# The WP manual's §2.7 defines verdict as valid/failed/unknown/timeout/etc.
# usage: strict_check.sh [--quiet] FIRST.json [SECOND.json]
# FIRST is the main WP pass. SECOND, if given, is the Z3qi rerun of the same wrapper
# (Makefile); each goal FIRST leaves open is then reported with SECOND's verdict too.
# A goal counts as proved only with a valid, passed verdict. --quiet prints nothing.
set -eu
quiet=0
if [ "$#" -ge 1 ] && [ "$1" = "--quiet" ]; then quiet=1; shift; fi
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: strict_check.sh [--quiet] WP-REPORT.json [SECOND-PASS.json]" >&2
    exit 2
fi
python3 - "$quiet" "$@" <<'PY'
import json
import sys

quiet = sys.argv[1] == "1"


def load(path):
    try:
        with open(path, encoding="utf-8") as stream:
            goals = json.load(stream)
    except (OSError, ValueError) as exc:
        print(f"WP report unavailable or invalid: {exc}", file=sys.stderr)
        sys.exit(2)
    if not isinstance(goals, list) or not goals:
        print(f"WP report {path} has no goals", file=sys.stderr)
        sys.exit(2)
    for row in goals:
        if not isinstance(row, dict) or not isinstance(row.get("goal"), str):
            print(f"WP report {path} has an unnamed goal", file=sys.stderr)
            sys.exit(2)
    return goals


def proved(row):
    return row.get("verdict") == "valid" and row.get("passed") is True


def label(row):
    verdict = row.get("verdict")
    return str(verdict).capitalize() if verdict is not None else "Unknown"


goals = load(sys.argv[2])
second = None
if len(sys.argv) > 3:
    second = {row["goal"]: row for row in load(sys.argv[3])}
failed = 0
for row in goals:
    if proved(row):
        continue
    first = label(row)
    if second is None:
        failed += 1
        if not quiet:
            print(f"{row['goal']}: {first}")
        continue
    again = second.get(row["goal"])
    if again is None:
        failed += 1
        if not quiet:
            print(f"{row['goal']}: {first} (not in the Z3qi pass)")
    elif proved(again):
        if not quiet:
            print(f"{row['goal']}: Valid by Z3qi (first pass {first})")
    else:
        failed += 1
        if not quiet:
            print(f"{row['goal']}: {label(again)} (Z3qi; first pass {first})")
if not quiet:
    print(f"WP strict: {len(goals) - failed}/{len(goals)} Valid")
sys.exit(1 if failed else 0)
PY
