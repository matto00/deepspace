#!/usr/bin/env bash
#
# Full, clean rebuild of the DeepSpace editor module.
#
# Why this exists rather than just ./build.sh:
#
# While the editor is running it holds libUnrealEditor-DeepSpace.so mapped, so
# UBT cannot replace it. Instead it emits numbered hot-reload copies
# (-0001, -0002, ...) and leaves Binaries/Linux/UnrealEditor.modules pointing at
# the original. The editor loads what the manifest names, decides the module is
# out of date, and tells you to rebuild manually -- which then produces yet
# another numbered library. Closing the editor first is the whole fix.
#
# There is no way around the restart for most C++ changes on Linux. Live Coding,
# Unreal's in-place patcher, is Windows-only (its build rule is gated on Win64).
# Linux has only the older Hot Reload, which copes with changes inside function
# bodies but not with reflection changes -- new UPROPERTYs, UFUNCTIONs,
# components or classes -- since Blueprints are built against the old layout.
# So the goal here is making the restart one command, not avoiding it.
#
#   ./rebuild.sh                   refuse to run if the editor is open
#   ./rebuild.sh --force           close any running editor first
#   ./rebuild.sh --force --launch  close, rebuild, and reopen the editor
#
set -euo pipefail

cd "$(dirname "$0")"

FORCE=0
LAUNCH=0
for arg in "$@"; do
    case "$arg" in
        --force)  FORCE=1 ;;
        --launch) LAUNCH=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

mapfile -t EDITORS < <(pgrep -f "Binaries/Linux/UnrealEditor.*DeepSpace.uproject" || true)

if (( ${#EDITORS[@]} > 0 )); then
    if (( FORCE )); then
        echo "==> Closing running editor (${EDITORS[*]})"
        kill "${EDITORS[@]}" 2>/dev/null || true
        for _ in {1..30}; do
            pgrep -f "Binaries/Linux/UnrealEditor.*DeepSpace.uproject" >/dev/null || break
            sleep 1
        done
        if pgrep -f "Binaries/Linux/UnrealEditor.*DeepSpace.uproject" >/dev/null; then
            echo "!!! Editor did not exit. Close it and try again." >&2
            exit 1
        fi
    else
        echo "!!! The Unreal editor is running (PID ${EDITORS[*]})." >&2
        echo "    A build now only produces hot-reload libraries the editor will" >&2
        echo "    not pick up. Close it, or re-run with --force." >&2
        exit 1
    fi
fi

echo "==> Removing stale hot-reload libraries"
rm -fv Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.so \
       Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.debug \
       Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.sym 2>/dev/null || true

echo "==> Building"
./build.sh

echo
echo "==> Verifying the manifest points at a library newer than the sources"
MANIFEST=Binaries/Linux/UnrealEditor.modules
LIB="Binaries/Linux/$(python3 -c "import json,sys;print(json.load(open('$MANIFEST'))['Modules']['DeepSpace'])")"

if [[ ! -f "$LIB" ]]; then
    echo "!!! Manifest names $LIB, which does not exist." >&2
    exit 1
fi

NEWEST_SOURCE=$(find Source -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' \) \
                -printf '%T@\n' | sort -n | tail -1)
LIB_TIME=$(stat -c %Y "$LIB")

if (( $(echo "$LIB_TIME < $NEWEST_SOURCE" | bc -l) )); then
    echo "!!! $LIB is older than the newest source file." >&2
    exit 1
fi

STRAY=$(ls Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.so 2>/dev/null | wc -l)
if (( STRAY > 0 )); then
    echo "!!! $STRAY hot-reload libraries reappeared; something held the module open." >&2
    exit 1
fi

echo "    OK: $LIB, no stray hot-reload libraries."

if (( LAUNCH )); then
    echo
    echo "==> Launching the editor"
    # Detached, so this script returns and the editor outlives the terminal.
    # unreal-editor, not the raw binary: it forces XWayland for HiDPI.
    setsid unreal-editor "$PWD/DeepSpace.uproject" >/dev/null 2>&1 < /dev/null &
    echo "    Started. First load takes a while; the window appears when ready."
else
    echo
    echo "Ready. Launch with: unreal-editor DeepSpace.uproject"
fi
