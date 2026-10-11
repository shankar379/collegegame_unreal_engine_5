"""Imports the campus map picture (three.js repo: node tools/export-unreal-map.mjs -> export/unreal/campus_map.png)
as the HUD's minimap / full-map texture /Game/CampusUI/Icons/T_Icon_campusmap (UI texture: no mips, sharp)."""
import os
import traceback

import unreal

SRC = os.environ.get("CAMPUS_MAP_PNG", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal\campus_map.png")
REPORT = os.path.join(os.path.dirname(__file__), "import_map.txt")
out = []
try:
    t = unreal.AssetImportTask()
    t.filename = SRC
    t.destination_path = "/Game/CampusUI/Icons"
    t.destination_name = "T_Icon_campusmap"
    t.automated = True
    t.replace_existing = True
    t.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset("/Game/CampusUI/Icons/T_Icon_campusmap")
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    out.append("imported %s %dx%d" % (tex.get_path_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
except Exception:
    out.append("ERR " + traceback.format_exc())
open(REPORT, "w").write("\n".join(out))
