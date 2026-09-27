#!/usr/bin/env bash
# Run one rendered check -- an Eyes.* test -- and print the verdict.
#
#   Tools/eyes.sh Eyes.WorldReliefParity
#
# Eyes.* tests render, so they are named outside DeepSpace. and ./test.sh
# never runs them: under -nullrhi there is no GPU. This runs one the way it
# must be run -- through the machine-wide lock (Tools/ue_lock.sh),
# -RenderOffScreen, never -nullrhi -- and reads the verdict from the log as
# ./test.sh does. Exits non-zero unless at least one test ran and none failed.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
FILTER="${1:?usage: Tools/eyes.sh Eyes.<Name>}"
LOG="$ROOT/Saved/Logs/DeepSpace.log"
[[ $FILTER == Eyes.* ]] || { echo "Tools/eyes.sh runs Eyes.* checks; the DeepSpace suite is ./test.sh"; exit 2; }

# shellcheck source=Tools/ue_lock.sh
. "$ROOT/Tools/ue_lock.sh"

ue_locked "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests $FILTER" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nosplash -NoLiveCoding -RenderOffScreen -FORCELOGFLUSH >/dev/null 2>&1

pass=$(grep -c "Test Completed. Result={Success}" "$LOG" 2>/dev/null || true)
fail=$(grep -E "Test Completed. Result=\{(Fail|Error)" "$LOG" 2>/dev/null || true)
echo "passed: ${pass:-0}"
if [[ -n $fail ]]; then
    echo "FAILED:"
    echo "$fail"
    grep -E "Error: |LogAutomationController: Error" "$LOG" | head -40
    exit 1
fi
[[ ${pass:-0} -gt 0 ]] || { echo "no tests ran -- check $LOG"; exit 1; }
