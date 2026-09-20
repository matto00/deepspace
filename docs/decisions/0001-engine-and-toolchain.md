# 0001 — Engine acquisition, toolchain, and Linux launch

**Date:** 2026-09-20
**Status:** Accepted

## Context

DeepSpace targets Unreal Engine 5 on Arch Linux with Hyprland, an RTX 4070 Ti
SUPER, and a 4K display at scale 2. Epic supports Linux unofficially, and its
documentation is written for Ubuntu and for source builds. Several documented
steps turned out not to apply.

## Decisions

### Precompiled binary, not a source build

Engine lives at `~/UnrealEngine/UE_5.8`, outside the repo and never committed.
A source build takes hours and far more disk, and is only needed to modify the
engine itself. Version installed: **5.8.2**.

### The bundled toolchain is already present — do not run Setup.sh

Epic's Linux quickstart says to run `Engine/Build/BatchFiles/Linux/SetupToolchain.sh`.
**That file does not exist in the precompiled distribution.** The clang toolchain
ships bundled at:

```
Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/
```

`Setup.sh` does exist but is Debian-specific (it drives `dpkg-query` and
`apt-get`) and then calls both `SetupToolchain.sh` and `BuildThirdParty.sh`,
neither of which ships in the binary distribution. Under `set -e` it fails
partway. **Do not run it on Arch.**

The bundled clang is 20.1.8, not the 18.1.0 Epic's requirements page lists. The
system clang (22.1.8) and gcc (16.1.1) are irrelevant — the build scripts use
the bundled toolchain regardless.

### GenerateProjectFiles.sh lives under Engine/Build/BatchFiles/Linux/

There is **no root-level `GenerateProjectFiles.sh`** in the binary distribution;
that exists only in source builds.

Note: with `-vscode` it produces `.vscode/compileCommands_*.json`, **not** a
root-level `compile_commands.json`. If Neovim/clangd is set up later, point it
at that file.

### Forced XWayland for sharp HiDPI rendering

Unreal's SDL Wayland backend is not HiDPI-aware. On the 4K output at scale 2 it
rendered a ~1080p buffer that the compositor upscaled, producing visible blur.
Confirmed by dragging the window to the 1080p output, where it was crisp.

`~/caelestia/local/hypr-user.lua` already sets
`xwayland = { force_zero_scaling = true }`, so XWayland clients render at native
resolution. Forcing Unreal onto XWayland therefore fixes it with no compositor
change.

`~/.local/bin/unreal-editor` sets `SDL_VIDEODRIVER=x11` and execs the editor.
The override is local rather than global because `caelestia/uwsm/env-hyprland`
sets `SDL_VIDEODRIVER='wayland,x11,windows'` system-wide for good reasons.

**Always launch via `unreal-editor`.** Running `Engine/Binaries/Linux/UnrealEditor`
directly brings the blur back.

A desktop entry at `~/.local/share/applications/unreal-editor.desktop` wraps the
same script and registers the `application/x-uproject` MIME type.

### No file descriptor limit change needed

The common advice is to raise `nofile` via `/etc/security/limits.d/`. Arch's
systemd default already gives **524288**, far above the 65536 usually
recommended. glibc is 2.44, past the 2.35 Epic recommends for startup speed.

## Consequences

- Disk: 39.8 GB download, **73 GB** extracted — Epic's documented ~43 GB is stale.
- Embedded Chromium (CEF) fails to initialize hardware GL and logs a wall of
  ANGLE/EGL errors at every launch. It falls back to software. This affects only
  in-editor browser panels (login, marketplace), not the viewport, which uses
  Unreal's own Vulkan RHI. Accepted as cosmetic; not worth tuning ANGLE flags.
- Driver 610.43.03 and Vulkan 1.4.341 needed no workarounds. The `-onethread`
  fallback often required on NVIDIA/Linux was **not** necessary.
