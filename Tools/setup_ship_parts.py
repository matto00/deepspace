"""Author the ship's parts (wear and upgrades decision 5).

The script writes three things:
- one UShipModuleDataAsset per row of Tools/ship_parts.json, in
  Content/Ship/Parts;
- the catalogue DA_ShipCatalogue, which lists every part in the JSON's order;
- BP_DeepSpaceGameMode's StartingModules: the six stock parts, in bay order.

The JSON is the one source, because a .uasset is binary and a number settled
into one is invisible to git. Tune a part in the JSON and re-run this.
DeepSpace.Ship.Parts.Contract holds the JSON, the assets and
FShipRatings::Stock() to the same numbers.

The three hand-made modules in Content/Ship/Modules (DA_LifeSupport, DA_Lights
and DA_Sensors) are today's ship. The script reads their draws before it
writes anything, and it stops unless they equal the JSON's and sum to 620 W.
It copies each module to its stock part's path, rewrites the copy from the
JSON, and deletes the original once the game mode no longer holds it.

BP_DeepSpaceGameMode today saves no StartingModules of its own: a Blueprint
saves only what differs from its parent, so it inherits the C++ constructor's
list. This script gives it its own list, the six stock parts, and from then
on that saved list is what play and Tests/StockShip.h read. A stale saved
list would leave play on the old modules while every C++ default looked
right, which is why DeepSpace.Ship.Parts.StockIsToday holds the two equal.

Python has no factory of its own for these assets, so each new one is made
through DataAssetFactory with its class set. If that returns nothing, the run
stops and names the asset.

Run with the editor closed, through the machine-wide lock:

    . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_ship_parts.py" \\
        -unattended -nopause -nosplash -NoLiveCoding

unreal.log does not reach stdout under the commandlet; the report is in
Saved/setup_ship_parts.txt.
"""

import json
import os
import re
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOGUE = json.load(open(os.path.join(HERE, "ship_parts.json")))
DIRECTORY = CATALOGUE["directory"]
CATALOGUE_ASSET = CATALOGUE["catalogue"]
MODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"
OLD_DIRECTORY = "/Game/Ship/Modules"

# The hand-made modules, by the stock part each one becomes.
HAND_MADE = {
    "LifeSupport.Stock": OLD_DIRECTORY + "/DA_LifeSupport",
    "Lights.Stock": OLD_DIRECTORY + "/DA_Lights",
    "Sensors.Stock": OLD_DIRECTORY + "/DA_Sensors",
}
HAND_MADE_WATTS = 620.0

EAL = unreal.EditorAssetLibrary
log = []


def note(line):
    log.append(line)
    unreal.log(line)


def upper_snake(name):
    """A C++ enumerator as Python spells it: LifeSupport -> LIFE_SUPPORT,
    RangeLy -> RANGE_LY, Aux1 -> AUX1."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def enum_value(enum, name):
    value = getattr(enum, upper_snake(name), None)
    if value is None:
        raise RuntimeError(f"{enum.__name__} has no {name} (looked for {upper_snake(name)})")
    return value


def path_of(asset):
    return f"{DIRECTORY}/{asset}"


def row_by_id(part_id):
    for row in CATALOGUE["parts"]:
        if row["id"] == part_id:
            return row
    raise RuntimeError(f"ship_parts.json has no row {part_id}")


def check_hand_made_draws():
    """Today's three draws, read wherever they are now (the old module, or
    the stock part a previous run made of it) before anything is written."""
    total = 0.0
    for part_id, old in HAND_MADE.items():
        row = row_by_id(part_id)
        where = old if EAL.does_asset_exist(old) else path_of(row["asset"])
        if not EAL.does_asset_exist(where):
            raise RuntimeError(f"{part_id}: neither {old} nor {where} exists, so today's draw cannot be read")
        watts = EAL.load_asset(where).get_editor_property("power_draw")
        note(f"{part_id}: {where} draws {watts:.1f} W; the JSON says {row['draw']}")
        if abs(watts - float(row["draw"])) > 1e-3:
            raise RuntimeError(f"{part_id}: the asset draws {watts} W and the JSON {row['draw']} W; "
                               f"set the JSON's draw to the asset's")
        total += watts
    if abs(total - HAND_MADE_WATTS) > 1e-3:
        raise RuntimeError(f"the hand-made modules draw {total} W together, not {HAND_MADE_WATTS}: "
                           f"today's ship is not the one the spec describes")


def copy_hand_made():
    """Each hand-made module copied to its stock part's path, keeping its
    class and draw. The original is deleted once nothing holds it."""
    for part_id, old in HAND_MADE.items():
        new = path_of(row_by_id(part_id)["asset"])
        if EAL.does_asset_exist(old) and not EAL.does_asset_exist(new):
            if not EAL.duplicate_asset(old, new):
                raise RuntimeError(f"could not copy {old} to {new}")
            note(f"copied {old} to {new}")


def ensure_asset(asset, cls):
    path = path_of(asset)
    if EAL.does_asset_exist(path):
        found = EAL.load_asset(path)
        if not isinstance(found, cls):
            raise RuntimeError(f"{path} is a {type(found).__name__}, not a {cls.__name__}")
        return found
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    made = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset, DIRECTORY, cls, factory)
    if made is None:
        raise RuntimeError(f"could not create {path} as a {cls.__name__}")
    note(f"created {path}")
    return made


def write_part(row):
    part = ensure_asset(row["asset"], unreal.ShipModuleDataAsset)
    part.set_editor_property("module_id", row["id"])
    part.set_editor_property("display_name", row["name"])
    part.set_editor_property("words", row["words"])
    part.set_editor_property("power_draw", float(row["draw"]))
    part.set_editor_property("bay", enum_value(unreal.ShipBay, row["bay"]))
    ratings = unreal.Map(unreal.ShipRating, float)
    for name, value in row["ratings"].items():
        ratings[enum_value(unreal.ShipRating, name)] = float(value)
    part.set_editor_property("ratings", ratings)
    if not EAL.save_loaded_asset(part, False):
        raise RuntimeError(f"could not save {row['asset']}")
    note(f"{row['id']}: {row['bay']}, draw {row['draw']} W, {row['ratings']}")
    return part


def write_catalogue(parts):
    catalogue = ensure_asset(CATALOGUE_ASSET, unreal.ShipPartCatalogue)
    catalogue.set_editor_property("parts", parts)
    if len(catalogue.get_editor_property("parts")) != len(parts):
        raise RuntimeError(f"{CATALOGUE_ASSET}.Parts did not take")
    if not EAL.save_loaded_asset(catalogue, False):
        raise RuntimeError(f"could not save {CATALOGUE_ASSET}")
    note(f"{CATALOGUE_ASSET}: {len(parts)} parts")


def write_game_mode(stock):
    bp = unreal.load_asset(MODE_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("starting_modules", stock)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    if len(cdo.get_editor_property("starting_modules")) != len(stock):
        raise RuntimeError("BP_DeepSpaceGameMode.StartingModules did not survive the compile")
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("could not save BP_DeepSpaceGameMode")
    note("BP_DeepSpaceGameMode.StartingModules: " + ", ".join(p.get_name() for p in stock))


def delete_hand_made():
    for old in HAND_MADE.values():
        if EAL.does_asset_exist(old):
            if not EAL.delete_asset(old):
                raise RuntimeError(f"could not delete {old}: something still references it")
            note(f"deleted {old}")
    if EAL.does_directory_exist(OLD_DIRECTORY) and not EAL.list_assets(OLD_DIRECTORY):
        EAL.delete_directory(OLD_DIRECTORY)
        note(f"deleted {OLD_DIRECTORY}")


def main():
    check_hand_made_draws()
    copy_hand_made()
    parts = [write_part(row) for row in CATALOGUE["parts"]]
    write_catalogue(parts)
    stock = [part for part, row in zip(parts, CATALOGUE["parts"]) if row["id"].endswith(".Stock")]
    if len(stock) != 6:
        raise RuntimeError(f"expected six stock parts, found {len(stock)}")
    write_game_mode(stock)
    delete_hand_made()
    note("DONE")


try:
    main()
except Exception:
    note("FAILED\n" + traceback.format_exc())
    raise
finally:
    with open(unreal.Paths.project_saved_dir() + "setup_ship_parts.txt", "w") as f:
        f.write("\n".join(log) + "\n")
