"""Fail if any Blueprint in the project does not compile.

Nothing else catches this. The C++ builds, the tests pass, the level
validates -- and the editor still refuses to play, because a Blueprint is
carrying dangling references to something C++ removed. That is how
BP_ShipConsole shipped broken: the TextRenderComponent its graph drove was
deleted, the component check passed, and the four compile errors only
appeared when a human pressed Play.

Run after any change that removes or renames a C++ component, UPROPERTY or
BlueprintImplementableEvent, with the editor closed:

    UnrealEditor-Cmd DeepSpace.uproject -run=pythonscript \
        -script=".../Tools/check_blueprints.py" -unattended -nopause -nosplash

The verdict goes to Saved/check_blueprints.txt; the commandlet exits non-zero
if anything is broken.
"""

import sys

import unreal

GOOD = (unreal.BlueprintStatus.BS_UP_TO_DATE,
        unreal.BlueprintStatus.BS_UP_TO_DATE_WITH_WARNINGS)

# Only the content this project actually authored. The template variants
# Epic ships (/Game/FirstPerson, /Game/Variant_Horror, /Game/Variant_Shooter,
# /Game/LevelPrototyping) contain 24 Blueprints that do not compile against
# this project's C++ and never have: they are orphaned samples nothing loads.
# Checking them would mean a permanently red guard, which is the same as no
# guard at all. If that content is ever deleted, delete this comment with it.
SEARCH_PATHS = ("/Game/Blueprints", "/Game/Characters/DeepSpace", "/Game/UI")


def main():
    lines = []
    broken = []

    for root in SEARCH_PATHS:
        for path in unreal.EditorAssetLibrary.list_assets(root, recursive=True):
            asset = unreal.load_asset(path)
            if not isinstance(asset, unreal.Blueprint):
                continue
            # Compile before asking: a Blueprint freshly loaded reports the
            # status it was saved with, not the truth about current C++.
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            status = asset.get_editor_property("status")
            ok = status in GOOD
            lines.append("%-30s %s%s" % (asset.get_name(), status,
                                         "" if ok else "   <-- DOES NOT COMPILE"))
            if not ok:
                broken.append(asset.get_name())

    lines.append("")
    lines.append("PASS: %d blueprints, all compiling" % (len(lines) - 1)
                 if not broken else
                 "FAIL: %s" % ", ".join(broken))

    with open(unreal.Paths.project_saved_dir() + "check_blueprints.txt", "w") as f:
        f.write("\n".join(lines) + "\n")

    for line in lines:
        unreal.log(line)

    if broken:
        sys.exit(1)


main()
