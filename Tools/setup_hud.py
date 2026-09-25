"""Point BP_DeepSpaceCharacter at the C++ HUD.

The Blueprint stores hud_widget_class, so changing the C++ default alone
changes nothing: the saved value wins. This reassigns it from WBP_HUD to the
C++ UShipHUDWidget, which is where HUD logic belongs (ADR 0002 -- Blueprints
only assign assets and expose tunable values, and a HUD that reads ship state
every frame is not that).

Run with the editor closed.
"""

import unreal

BP_PATH = "/Game/Blueprints/BP_DeepSpaceCharacter"

bp = unreal.load_asset(BP_PATH)
cdo = unreal.get_default_object(bp.generated_class())
before = cdo.get_editor_property("hud_widget_class")

cdo.set_editor_property("hud_widget_class", unreal.ShipHUDWidget)
unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

after = unreal.get_default_object(bp.generated_class()).get_editor_property("hud_widget_class")
with open(unreal.Paths.project_saved_dir() + "setup_hud.txt", "w") as f:
    f.write("before: %s\nafter:  %s\n" % (before, after))
