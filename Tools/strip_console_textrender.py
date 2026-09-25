"""Remove the console Blueprint's TextRender readout.

The console now carries a real Slate screen. The old TextRender stood 22 cm
proud of the panel, which is in front of that screen and in the player's
way. Only that one node is touched; nothing else in the Blueprint is.
"""
import unreal, traceback

out = []
try:
    path = "/Game/Blueprints/BP_ShipConsole"
    bp = unreal.load_asset(path)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    removed = 0
    for h in handles:
        data = sub.k2_find_subobject_data_from_handle(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if obj and isinstance(obj, unreal.TextRenderComponent):
            out.append("removing %s" % obj.get_name())
            sub.delete_subobject(handles[0], h, bp)
            removed += 1
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    out.append("removed %d, recompiled and saved" % removed)
except Exception:
    out.append(traceback.format_exc())
with open(unreal.Paths.project_saved_dir()+"strip_textrender.txt","w") as f:
    f.write("\n".join(out)+"\n")
