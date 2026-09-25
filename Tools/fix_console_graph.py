"""Drop BP_ShipConsole's legacy event graph.

The console used to drive a TextRenderComponent readout from Blueprint. The
Slate screen replaced it and the component was removed, which left the graph
calling TextRender functions on a Target that no longer exists -- four
compile errors, and a Blueprint that refuses to play.

The graph held nothing else worth keeping: C++ sets bCanEverTick = false, so
its ReceiveTick was already dead; BeginPlay is handled in C++; and
interaction goes through UInteractableComponent and the reach trace, not
overlap. Removing it also puts the asset back inside the project's rule that
Blueprints only assign assets and expose tunable values.

Reversible: git checkout HEAD -- Content/Blueprints/BP_ShipConsole.uasset
"""

import unreal

BP_PATH = "/Game/Blueprints/BP_ShipConsole"

log = []


def note(line):
    log.append(line)
    unreal.log(line)


def main():
    bp = unreal.load_asset(BP_PATH)
    if not bp:
        raise RuntimeError("no blueprint at %s" % BP_PATH)

    note("before: status=%s graphs=%s" % (
        bp.get_editor_property("status"),
        unreal.BlueprintEditorLibrary.list_graph_names(bp)))

    graph = unreal.BlueprintEditorLibrary.find_event_graph(bp)
    if graph:
        unreal.BlueprintEditorLibrary.remove_graph(bp, graph)
        note("removed EventGraph")
    else:
        note("no EventGraph to remove")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    status = bp.get_editor_property("status")
    note("after: status=%s graphs=%s" % (
        status, unreal.BlueprintEditorLibrary.list_graph_names(bp)))

    # The components must survive; the screen is the whole point of the asset.
    cdo = unreal.get_default_object(bp.generated_class())
    names = sorted(c.get_name() for c in cdo.get_components_by_class(unreal.ActorComponent))
    note("components: %s" % names)
    for required in ("Screen", "Interactable", "Mesh"):
        if required not in names:
            raise RuntimeError("%s went missing -- refusing to save" % required)

    if status not in (unreal.BlueprintStatus.BS_UP_TO_DATE,
                      unreal.BlueprintStatus.BS_UP_TO_DATE_WITH_WARNINGS):
        raise RuntimeError("still not compiling (%s) -- refusing to save" % status)

    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    note("saved")

    with open(unreal.Paths.project_saved_dir() + "fix_console_graph.txt", "w") as f:
        f.write("\n".join(log) + "\n")


main()
