#!/bin/sh
# Check Frama-C WP's per-goal JSON verdicts, not just its process exit or totals.
# The WP manual's §2.7 defines verdict as valid/failed/unknown/timeout/etc.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: strict_check.sh WP-REPORT.json" >&2
    exit 2
fi
python3 - "$1" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    with open(path, encoding="utf-8") as stream:
        goals = json.load(stream)
except (OSError, ValueError) as exc:
    print(f"WP report unavailable or invalid: {exc}", file=sys.stderr)
    sys.exit(2)
if not isinstance(goals, list) or not goals:
    print("WP report has no goals", file=sys.stderr)
    sys.exit(2)
failed = 0
for row in goals:
    if not isinstance(row, dict) or not isinstance(row.get("goal"), str):
        print("WP report has an unnamed goal", file=sys.stderr)
        sys.exit(2)
    verdict = row.get("verdict")
    if verdict != "valid" or row.get("passed") is not True:
        failed += 1
        label = str(verdict).capitalize() if verdict is not None else "Unknown"
        print(f"{row['goal']}: {label}")
print(f"WP strict: {len(goals) - failed}/{len(goals)} Valid")
sys.exit(1 if failed else 0)
PY
