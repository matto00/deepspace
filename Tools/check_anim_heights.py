"""
Checks the camera stays inside the capsule in every animation.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/check_anim_heights.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/check_anim_heights.txt

The camera rides the body's head bone. The engine guarantees the capsule fits
wherever the player stands, so a head that never rises above the capsule's top
can never carry the camera through a ceiling -- in the crawlway or anywhere.
This samples every clip the body plays and checks the head's peak against the
capsule for the posture that plays it, read from BP_DeepSpaceCharacter itself.
Imported clips the body does not play yet (typing, seated_idle, the
pilot_flips_switches pair) are deliberately absent: add a clip here when it is
wired into ABP_DeepSpaceBody.

It exists because the first crouch-walk clip peaked at 118 cm, above the
original 110 cm crawlway: the view would have passed through the ceiling on
every step. A future, taller clip fails here instead.
"""

import os
import traceback

import unreal

CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed"
ANIMS = "/Game/Characters/DeepSpace/Anims"
HEAD = "head"
SAMPLES = 40

# Room between the head bone and the capsule top: the camera's near plane
# and the lag of the spring arm holding it.
MARGIN = 5.0

POSTURE_CLIPS = {
    "standing": [UNARMED + "/MM_Idle", UNARMED + "/Walk/MF_Unarmed_Walk_Fwd", ANIMS + "/RTG_running"],
    "crouched": [ANIMS + "/RTG_crouching_idle", ANIMS + "/RTG_crouch_walk"],
    # Seated, the character keeps its standing capsule height; the cockpit
    # is 250 cm, so the standing capsule is the bound.
    "seated": [ANIMS + "/RTG_sitting_idle"],
    # Getting in and out of the seat starts and ends standing.
    "seat transitions": [ANIMS + "/RTG_stand_to_sit", ANIMS + "/RTG_sit_to_stand"],
}


def peak_head(anim):
    options = unreal.AnimPoseEvaluationOptions()
    length = anim.get_play_length()
    peak = -1e9
    for i in range(SAMPLES + 1):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length * i / SAMPLES, options)
        z = unreal.AnimPoseExtensions.get_bone_pose(pose, HEAD, unreal.AnimPoseSpaces.WORLD).translation.z
        peak = max(peak, z)
    return peak


def main():
    lines, failed = [], False
    try:
        bp = unreal.load_asset(CHARACTER_BP)
        defaults = unreal.get_default_object(bp.generated_class())
        standing = 2.0 * defaults.get_editor_property("capsule_component").get_unscaled_capsule_half_height()
        crouched = 2.0 * defaults.get_editor_property("character_movement").get_editor_property("crouched_half_height")
        limits = {"standing": standing, "crouched": crouched,
                  "seated": standing, "seat transitions": standing}
        lines.append("capsule: standing %.0f cm, crouched %.0f cm; margin %.0f cm" % (standing, crouched, MARGIN))

        for posture, clips in POSTURE_CLIPS.items():
            limit = limits[posture] - MARGIN
            for path in clips:
                anim = unreal.load_asset(path)
                if anim is None:
                    lines.append("FAIL %-16s %s is missing" % (posture, path))
                    failed = True
                    continue
                peak = peak_head(anim)
                ok = peak <= limit
                failed |= not ok
                lines.append("%s %-16s %-22s head peaks %.1f cm, limit %.1f" %
                             ("ok  " if ok else "FAIL", posture, anim.get_name(), peak, limit))
    except Exception:
        failed = True
        lines.append("FAIL\n" + traceback.format_exc())
    lines.append("")
    lines.append("FAIL: a head rises above its capsule; the camera can leave it." if failed
                 else "PASS: every clip keeps the head, and the camera, inside the capsule.")
    with open(os.path.join(unreal.Paths.project_saved_dir(), "check_anim_heights.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


main()
