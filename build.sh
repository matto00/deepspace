#!/usr/bin/env bash
# Canonical compile check for DeepSpace. Run after every C++ change.
set -euo pipefail

UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
PROJECT="$(cd "$(dirname "$0")" && pwd)/DeepSpace.uproject"

# Refuse to build under a running editor. UBT will happily succeed here, but
# it cannot replace the library the editor holds mapped: it writes
# libUnrealEditor-DeepSpace-NNNN.so and leaves the manifest naming the
# original. The editor then runs a module that no longer matches the source
# -- and the symptom is not a build error but something inexplicable in play.
# An hour went into "the camera shakes when I move the mouse" before the
# stray library turned out to be the cause.
#
# "[U]nrealEditor": the brackets stop pgrep matching this script's own command
# line. The trailing space excludes UnrealEditor-Cmd, so headless test runs
# and Python commandlets are not mistaken for an open editor.
if pgrep -f "[U]nrealEditor .*DeepSpace\.uproject" >/dev/null; then
    echo "!!! The Unreal editor is running." >&2
    echo "    Building now would only produce a hot-reload library the editor" >&2
    echo "    will not load, leaving it running stale code." >&2
    echo "    Use ./rebuild.sh --force (closes the editor, clears the strays)." >&2
    exit 1
fi

"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    DeepSpaceEditor Linux Development \
    -project="$PROJECT" -waitmutex
