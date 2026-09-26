#!/usr/bin/env bash
# Canonical compile check for DeepSpace. Run after every C++ change.
set -euo pipefail

UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$ROOT/DeepSpace.uproject"

# shellcheck source=Tools/ue_lock.sh
. "$ROOT/Tools/ue_lock.sh"

# Refuse to build under a running editor. UBT will happily succeed here, but
# it cannot replace the library the editor holds mapped: it writes
# libUnrealEditor-DeepSpace-NNNN.so and leaves the manifest naming the
# original. The editor then runs a module that no longer matches the source
# -- and the symptom is not a build error but something inexplicable in play.
# An hour went into "the camera shakes when I move the mouse" before the
# stray library turned out to be the cause.
#
# The danger is precisely "some process has THIS tree's library mapped", so
# that is what is checked. An editor open on another checkout -- the main
# tree while an agent builds in a worktree -- maps a different file and is
# no reason to refuse. Headless runs map it too, but they hold the lock
# below, so a build never meets one.
LIB="$ROOT/Binaries/Linux/libUnrealEditor-DeepSpace"
if grep -qsF "$LIB" /proc/[0-9]*/maps 2>/dev/null; then
    echo "!!! A running editor has this tree's module loaded:" >&2
    echo "    $LIB.so" >&2
    echo "    Building now would only produce a hot-reload library the editor" >&2
    echo "    will not load, leaving it running stale code." >&2
    echo "    Use ./rebuild.sh --force (closes the editor, clears the strays)." >&2
    exit 1
fi

ue_locked "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    DeepSpaceEditor Linux Development \
    -project="$PROJECT" -waitmutex
