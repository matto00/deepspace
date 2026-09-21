#!/usr/bin/env bash
#
# Open DeepSpace in the Unreal editor, the right way, from anywhere -- a
# terminal, or the caelestia app launcher via unreal-editor.desktop.
#
#   ./launch.sh              open DeepSpace (rebuilding first if needed)
#   ./launch.sh --check      report whether a rebuild would happen, then exit
#   ./launch.sh FILE...      not DeepSpace: hand straight to unreal-editor
#
# "The right way" means three things the bare editor does not do:
#
#   * Rebuild first when C++ is newer than the compiled module. Otherwise the
#     editor loads a stale library and asks for a manual rebuild, which fails
#     from inside the editor on Linux (see ./rebuild.sh).
#   * Focus the editor if it is already open, rather than starting a second
#     copy on the same project.
#   * Launch through unreal-editor, which forces XWayland for sharp HiDPI
#     rendering (docs/decisions/0001-engine-and-toolchain.md).
#
# Launched from a desktop entry there is no terminal, so progress and failures
# are reported with notify-send, and rebuild output goes to Saved/Logs/.

set -uo pipefail

cd "$(dirname "$(readlink -f "$0")")"

EDITOR_BIN="$HOME/.local/bin/unreal-editor"   # absolute: desktop entries get a minimal PATH
PROJECT="$PWD/DeepSpace.uproject"
LIB=Binaries/Linux/libUnrealEditor-DeepSpace.so
REBUILD_LOG=Saved/Logs/launch-rebuild.log

notify() {
    command -v notify-send >/dev/null 2>&1 && notify-send -a DeepSpace "$@"
}

# Anything other than DeepSpace goes straight through: this script backs the
# general Unreal Editor entry, which also opens .uproject files by MIME type.
if [[ $# -gt 0 && $1 != --check ]]; then
    exec "$EDITOR_BIN" "$@"
fi

# Why a rebuild is needed, or empty if it is not.
stale_reason() {
    if [[ ! -f $LIB ]]; then
        echo "the module has never been built"
    elif compgen -G "Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.so" >/dev/null; then
        echo "stray hot-reload libraries are present"
    elif [[ -n $(find Source DeepSpace.uproject -newer "$LIB" -type f \
                 \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' -o -name '*.uproject' \) \
                 -print -quit) ]]; then
        echo "C++ has changed since the last build"
    fi
}

reason=$(stale_reason)

if [[ ${1:-} == --check ]]; then
    echo "${reason:-up to date: no rebuild needed}"
    exit 0
fi

# [U]nrealEditor: the brackets stop pgrep matching its own command line.
if pgrep -f "[U]nrealEditor .*DeepSpace.uproject" >/dev/null; then
    command -v hyprctl >/dev/null 2>&1 && hyprctl -q dispatch focuswindow class:UnrealEditor
    exit 0
fi

if [[ -n $reason ]]; then
    notify "Rebuilding before opening" "$reason"
    mkdir -p "$(dirname "$REBUILD_LOG")"
    if ! ./rebuild.sh >"$REBUILD_LOG" 2>&1; then
        notify -u critical "DeepSpace rebuild failed" "See $PWD/$REBUILD_LOG"
        exit 1
    fi
fi

exec "$EDITOR_BIN" "$PROJECT"
