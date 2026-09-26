#!/usr/bin/env bash
# Run the DeepSpace automation tests headlessly and print the verdict.
#
#   ./test.sh                     every DeepSpace test
#   ./test.sh DeepSpace.Universe  one group
#
# Waits its turn behind any other build or test run in any worktree (see
# Tools/ue_lock.sh), so it is safe to call from parallel agents. The verdict
# is read from this tree's own log -- Unreal writes it there, not to stdout.
# Exits non-zero unless at least one test ran and none failed.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
FILTER="${1:-DeepSpace}"
LOG="$ROOT/Saved/Logs/DeepSpace.log"

# shellcheck source=Tools/ue_lock.sh
. "$ROOT/Tools/ue_lock.sh"

ue_locked "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests $FILTER" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding >/dev/null 2>&1

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

# Green is not enough: each of these has been left in the log by a run whose
# every test passed. A world torn down while still playing skips every
# actor's and subsystem's EndPlay, and a console variable found by name on
# every tick is a lookup the engine itself calls a performance problem.
dirty=$(grep -E "missing call to EndPlay|Performance warning: Console object" "$LOG" \
        | grep -v "LogAutomationController" || true)
if [[ -n $dirty ]]; then
    echo "LOG NOT CLEAN:"
    echo "$dirty"
    exit 1
fi
