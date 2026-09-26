# Sourced, not run. One heavy Unreal process at a time, across every checkout.
#
# This is a 6c/12t desktop. A DeepSpace build and a headless test run each
# want most of it, and agents working in separate git worktrees would
# otherwise start them concurrently. UBT already queues builds behind its own
# mutex (-waitmutex; the mutex is keyed on the engine install, so it spans
# worktrees), but nothing queues UnrealEditor-Cmd, and nothing queues a build
# behind a test run.
#
# The lock file lives in the git common directory, which every worktree of
# this repository shares, so "every checkout" is literally true. flock waits;
# it never fails, and it is released when the holder exits however it exits.
UE_LOCK="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --git-common-dir 2>/dev/null)"
UE_LOCK="$(cd "$(dirname "${BASH_SOURCE[0]}")" && cd "$UE_LOCK" && pwd)/deepspace-unreal.lock"

ue_locked() {
    if ! flock -n "$UE_LOCK" true 2>/dev/null; then
        echo ">>> Another Unreal build or test run holds the lock; waiting..." >&2
    fi
    flock "$UE_LOCK" nice -n 19 "$@"
}
