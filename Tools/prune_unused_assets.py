"""Delete every asset nothing reaches.

The project was started from Epic's First Person template, which brings a
large amount of content -- weapons, two gameplay variants, and the whole
mannequin library -- that this game never touches. Deleting by folder is
unsafe, because the parts that *are* load-bearing (the mannequin skeleton
and three of its animations, the prototyping meshes the entire ship is
built from) sit inside those folders.

So this computes a dependency closure instead. The roots are the things
that are referenced from outside the content browser and therefore cannot
be discovered by walking references: the map, the Blueprints C++ names by
path, the data assets, and the assets the Python pipeline in Tools/ names
as string literals. Everything reachable from a root is kept. Everything
else goes.

Dry run by default; set DS_PRUNE_APPLY=1 to actually delete.

    DS_PRUNE_APPLY=0 ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
        "$PWD/DeepSpace.uproject" -run=pythonscript \
        -script="$PWD/Tools/prune_unused_assets.py" \
        -unattended -nopause -nosplash -NoLiveCoding

The report lands in Saved/prune_report.txt -- unreal.log does not reach
stdout under the commandlet.
"""

import os
import traceback

import unreal

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORT = os.path.join(PROJECT, "Saved", "prune_report.txt")
APPLY = os.environ.get("DS_PRUNE_APPLY") == "1"

# Folders this project authored. Everything in them is a root: they are
# ours, they are small, and a thing we wrote that is momentarily
# unreferenced is a work in progress, not garbage.
ROOT_DIRS = [
    "/Game/Maps",
    "/Game/Blueprints",
    "/Game/UI",
    "/Game/Ship",
    "/Game/Input",
    "/Game/Materials",
    "/Game/Characters/DeepSpace",
]

# Assets outside those folders that something names by string. A path here
# is load-bearing for a reason that no reference graph can see.
ROOT_ASSETS = [
    # The ship is built from these three meshes and this material.
    # Tools/build_hauler.py names them; note their pivots all differ.
    "/Game/LevelPrototyping/Meshes/SM_Cube",
    "/Game/LevelPrototyping/Meshes/SM_ChamferCube",
    "/Game/LevelPrototyping/Meshes/SM_Cylinder",
    "/Game/LevelPrototyping/Materials/M_PrototypeGrid",
    # The retarget pipeline's target. Tools/import_animations.py and
    # Tools/setup_character.py name it; without it no animation can be
    # re-imported, even though the retargeted clips no longer need it.
    "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    # The three stock clips used directly, unretargeted, by the blend
    # spaces and the anim blueprint.
    "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",
    "/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd",
    "/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop",
]

registry = unreal.AssetRegistryHelpers.get_asset_registry()
lib = unreal.EditorAssetLibrary
LOG = []


def say(line):
    LOG.append(str(line))
    unreal.log(str(line))


def package_of(path):
    """/Game/A/B.B -> /Game/A/B"""
    return str(path).split(".")[0]


def closure(roots):
    """Every package reachable from roots, following hard dependencies."""
    seen = set()
    stack = list(roots)
    while stack:
        pkg = stack.pop()
        if pkg in seen:
            continue
        seen.add(pkg)
        try:
            deps = registry.get_dependencies(
                pkg, unreal.AssetRegistryDependencyOptions(
                    include_soft_package_references=True,
                    include_hard_package_references=True,
                ))
        except Exception:
            deps = None
        for dep in (deps or []):
            dep = str(dep)
            # Engine and script packages are not ours to prune.
            if dep.startswith("/Game/"):
                stack.append(dep)
    return seen


def main():
    every = [package_of(p) for p in lib.list_assets("/Game", recursive=True, include_folder=False)]
    every = sorted(set(every))
    say("assets under /Game: %d" % len(every))

    roots = set()
    for d in ROOT_DIRS:
        if not lib.does_directory_exist(d):
            say("!! root dir missing: %s" % d)
            continue
        for p in lib.list_assets(d, recursive=True, include_folder=False):
            roots.add(package_of(p))
    for a in ROOT_ASSETS:
        if not lib.does_asset_exist(a):
            say("!! root asset missing: %s" % a)
            continue
        roots.add(a)
    say("roots: %d" % len(roots))

    keep = closure(roots)
    say("reachable: %d" % len(keep))

    doomed = [p for p in every if p not in keep]
    say("unreferenced: %d" % len(doomed))

    by_folder = {}
    for p in doomed:
        top = "/".join(p.split("/")[:3])
        by_folder.setdefault(top, []).append(p)
    say("")
    say("--- unreferenced, by folder ---")
    for top in sorted(by_folder):
        say("%-44s %d" % (top, len(by_folder[top])))

    # The interesting half: what survived inside the template folders.
    say("")
    say("--- kept inside template folders ---")
    for p in sorted(keep):
        if p.startswith("/Game/Characters/Mannequins") or p.startswith("/Game/LevelPrototyping"):
            say("  %s" % p)

    say("")
    say("--- every unreferenced asset ---")
    for p in doomed:
        say("  %s" % p)

    if not APPLY:
        say("")
        say("DRY RUN -- nothing deleted. Set DS_PRUNE_APPLY=1 to apply.")
        return

    say("")
    deleted = 0
    for p in doomed:
        try:
            if lib.delete_asset(p):
                deleted += 1
            else:
                say("!! refused: %s" % p)
        except Exception as exc:
            say("!! failed: %s (%s)" % (p, exc))
    say("deleted %d/%d" % (deleted, len(doomed)))

    for top in sorted(by_folder, reverse=True):
        try:
            if lib.does_directory_exist(top) and not lib.list_assets(top, recursive=True, include_folder=False):
                lib.delete_directory(top)
                say("removed empty folder %s" % top)
        except Exception as exc:
            say("!! folder %s: %s" % (top, exc))


try:
    main()
except Exception:
    LOG.append(traceback.format_exc())
finally:
    with open(REPORT, "w") as fh:
        fh.write("\n".join(LOG) + "\n")
