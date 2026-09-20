#!/usr/bin/env bash
# Canonical compile check for DeepSpace. Run after every C++ change.
set -euo pipefail

UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
PROJECT="$(cd "$(dirname "$0")" && pwd)/DeepSpace.uproject"

"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    DeepSpaceEditor Linux Development \
    -project="$PROJECT" -waitmutex
