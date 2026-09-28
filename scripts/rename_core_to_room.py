#!/usr/bin/env python3
"""Rename ROOM/core -> ROOM/room across CommonDB source tree."""
from __future__ import annotations

import os
import re
import shutil
from pathlib import Path

ROOT = Path(r"C:\Users\anton\Documents\CommonDB")

SKIP_DIR_NAMES = {
    ".git",
    "build",
    "build-tools",
    "build-wsl",
    "build-export",
    "third_party",
    "capnp-install",
    "capnp-install-linux",
    ".deps",
    "generated",  # under integrations may keep for now; rebuild regenerates
}

# Content replacements — longest / most specific first
REPLACEMENTS: list[tuple[str, str]] = [
    # CMake aliases / packages
    ("ROOM::room_utils", "ROOM::room_utils"),
    ("ROOM::room", "ROOM::room"),
    ("ROOM_BOOTSTRAP_CAPNP", "ROOM_BOOTSTRAP_CAPNP"),
    ("ROOM_BUILD_XSCHEM_TCL", "ROOM_BUILD_XSCHEM_TCL"),
    ("ROOM_BUILD_EXAMPLES", "ROOM_BUILD_EXAMPLES"),
    ("ROOM_BUILD_OAS_TESTS", "ROOM_BUILD_OAS_TESTS"),
    ("ROOM_BUILD_TESTS", "ROOM_BUILD_TESTS"),
    ("_ROOM_TOP_LEVEL", "_ROOM_TOP_LEVEL"),
    ("ROOM_INSTALL_PREFIX", "ROOM_INSTALL_PREFIX"),
    ("ROOM_INCLUDE_DIRS", "ROOM_INCLUDE_DIRS"),
    ("ROOM_ROOT", "ROOM_ROOT"),
    ("ROOMConfig.cmake", "ROOMConfig.cmake"),
    ("ROOMTargets.cmake", "ROOMTargets.cmake"),
    ("ROOMTargets", "ROOMTargets"),
    ("lib/cmake/ROOM", "lib/cmake/ROOM"),
    ("include/ROOM", "include/ROOM"),
    ("project(ROOM", "project(ROOM"),
    ("NAMESPACE ROOM::", "NAMESPACE ROOM::"),
    ("Export ROOMTargets", "Export ROOMTargets"),
    ("FILE ROOMTargets.cmake", "FILE ROOMTargets.cmake"),
    # Targets / tool names
    ("make_empty_room_views", "make_empty_room_views"),
    ("gds_room_roundtrip", "gds_room_roundtrip"),
    ("oas_room_roundtrip", "oas_room_roundtrip"),
    ("room_file_sniff", "room_file_sniff"),
    ("room_paths_unit", "room_paths_unit"),
    ("room_to_xschem", "room_to_xschem"),
    ("room_to_qucs", "room_to_qucs"),
    ("room_to_gds", "room_to_gds"),
    ("gds_to_room", "gds_to_room"),
    ("qucs_to_room", "qucs_to_room"),
    ("xschem_to_room", "xschem_to_room"),
    ("oas_to_room", "oas_to_room"),
    ("room_utils", "room_utils"),
    ("room_oas", "room_oas"),
    ("roomtcl", "roomtcl"),
    ("libroom", "libroom"),
    # Paths / API identifiers
    ("room_paths.h", "room_paths.h"),
    ("room_paths.cpp", "room_paths.cpp"),
    ("room_paths", "room_paths"),
    ("ROOM_FILE_NAMING", "ROOM_FILE_NAMING"),
    ("ParsedRoomPath", "ParsedRoomPath"),
    ("parseRoomFilePath", "parseRoomFilePath"),
    ("isViewRoomFile", "isViewRoomFile"),
    ("isRoomFilePath", "isRoomFilePath"),
    ("kRoomFileExtension", "kRoomFileExtension"),
    ("roomFileGlob", "roomFileGlob"),
    ("roomFileName", "roomFileName"),
    ("RoomFile", "RoomFile"),
    ("roomFile", "roomFile"),
    ("setup_klayout_mroom", "setup_klayout_mroom"),
    ("klayout_load_room", "klayout_load_room"),
    ("open_room_in_klayout", "open_room_in_klayout"),
    ("test_room_roundtrip", "test_room_roundtrip"),
    ("test_room_properties", "test_room_properties"),
    ("test_room_load", "test_room_load"),
    ("run_oas_room_roundtrip", "run_oas_room_roundtrip"),
    # KLayout plugin
    ("mroom", "mroom"),
    ("roomFormat", "roomFormat"),
    ("roomReader", "roomReader"),
    ("roomWriter", "roomWriter"),
    ("roomPlugin", "roomPlugin"),
    # Namespace
    ("namespace room", "namespace room"),
    ("} // namespace room", "} // namespace room"),
    ("room::", "room::"),
    # File extension (after path helpers renamed)
    (".layout.room", ".layout.room"),
    (".schematic.room", ".schematic.room"),
    (".symbol.room", ".symbol.room"),
    (".abstract.room", ".abstract.room"),
    ("*.room", "*.room"),
    (".core\"", ".room\""),
    (".room'", ".room'"),
    (".room`", ".room`"),
    (".room ", ".room "),
    (".core\n", ".room\n"),
    (".room)", ".room)"),
    (".room,", ".room,"),
    (".room.", ".room."),
    (".room/", ".room/"),
    (".room]", ".room]"),
    (".room}", ".room}"),
    ("`.room`", "`.room`"),
    # Docs / product name (after specific ROOM_*)
    ("Build ROOM ", "Build ROOM "),
    ("for ROOM", "for ROOM"),
    ("the ROOM ", "the ROOM "),
    ("The ROOM ", "The ROOM "),
    ("a ROOM ", "a ROOM "),
    ("A ROOM ", "A ROOM "),
    ("ROOM file", "ROOM file"),
    ("ROOM File", "ROOM File"),
    ("ROOM files", "ROOM files"),
    ("ROOM Files", "ROOM Files"),
    ("ROOM format", "ROOM format"),
    ("ROOM Format", "ROOM Format"),
    ("ROOM API", "ROOM API"),
    ("ROOM library", "ROOM library"),
    ("ROOM Library", "ROOM Library"),
    ("ROOM targets", "ROOM targets"),
    ("ROOM tests", "ROOM tests"),
    ("ROOM test", "ROOM test"),
    ("ROOM example", "ROOM example"),
    ("ROOM integration", "ROOM integration"),
    ("ROOM Integration", "ROOM Integration"),
    ("FetchRoom", "FetchRoom"),
    ("EnsureCapnp", "EnsureCapnp"),  # no-op keep
    ("\"core;compact\"", "\"room;compact\""),
    ("LABELS \"core;", "LABELS \"room;"),
    # Remaining standalone ROOM token in docs (careful)
    ("# ROOM", "# ROOM"),
    ("## ROOM", "## ROOM"),
    ("### ROOM", "### ROOM"),
    ("**ROOM**", "**ROOM**"),
    ("`ROOM`", "`ROOM`"),
    (" ROOM\n", " ROOM\n"),
    ("(ROOM)", "(ROOM)"),
    ("ROOM,", "ROOM,"),
    ("ROOM.", "ROOM."),
    ("ROOM:", "ROOM:"),
    ("ROOM ", "ROOM "),
    (" ROOM", " ROOM"),
]

# add_library(room → room) etc. via regex after string pass
REGEX_REPLACEMENTS: list[tuple[str, str]] = [
    (r"\badd_library\(core\b", "add_library(room"),
    (r"\btarget_link_libraries\(([^\)]*)\bcore\b", r"target_link_libraries(\1room"),
    (r"\btarget_include_directories\(core\b", "target_include_directories(room"),
    (r"\bset_target_properties\(core\b", "set_target_properties(room"),
    (r"\binstall\(TARGETS core\b", "install(TARGETS room"),
    (r"\bALIAS core\b", "ALIAS room"),
    (r"\blibrary\(core\b", "library(room"),
    (r"\bTARGET core\b", "TARGET room"),
    (r"\badd_dependencies\(([^\)]*)\bcore\b", r"add_dependencies(\1room"),
    (r"\blibroom\b", "libroom"),
]

TEXT_EXTS = {
    ".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx",
    ".md", ".txt", ".cmake", ".in", ".pri", ".pro", ".patch",
    ".yml", ".yaml", ".json", ".lym", ".rb", ".py", ".sh", ".cmd", ".bat",
    ".capnp", ".html", ".css", ".qml", ".qrc", ".ui", ".xml", ".toml",
    ".gitignore", ".gitattributes", ".editorconfig",
}

FILE_RENAMES: list[tuple[str, str]] = [
    ("cmake/ROOMConfig.cmake", "cmake/ROOMConfig.cmake"),
    ("docs/ROOM_FILE_NAMING.md", "docs/ROOM_FILE_NAMING.md"),
    ("src/room_paths.h", "src/room_paths.h"),
    ("src/room_paths.cpp", "src/room_paths.cpp"),
    ("tools/make_empty_room_views.cpp", "tools/make_empty_room_views.cpp"),
    ("tests/room_file_sniff.cpp", "tests/room_file_sniff.cpp"),
    ("tests/room_paths_unit.cpp", "tests/room_paths_unit.cpp"),
    ("tests/gds_room_roundtrip.cpp", "tests/gds_room_roundtrip.cpp"),
    ("tests/oas_room_roundtrip.cpp", "tests/oas_room_roundtrip.cpp"),
    ("scripts/setup_klayout_mroom.cmd", "scripts/setup_klayout_mroom.cmd"),
    ("scripts/klayout_load_room.lym", "scripts/klayout_load_room.lym"),
    ("scripts/open_room_in_klayout.cmd", "scripts/open_room_in_klayout.cmd"),
    ("scripts/test_room_load.rb", "scripts/test_room_load.rb"),
    ("scripts/test_room_roundtrip.rb", "scripts/test_room_roundtrip.rb"),
    ("scripts/test_room_properties.rb", "scripts/test_room_properties.rb"),
    ("scripts/run_oas_room_roundtrip_test.sh", "scripts/run_oas_room_roundtrip_test.sh"),
    ("scripts/run_oas_room_roundtrip_test.cmd", "scripts/run_oas_room_roundtrip_test.cmd"),
    ("docs/html/corepaths.html", "docs/html/roompaths.html"),
    ("integrations/xschem_tcl/core_tcl.cpp", "integrations/xschem_tcl/room_tcl.cpp"),
]

DIR_RENAMES: list[tuple[str, str]] = [
    ("examples/gds_to_room", "examples/gds_to_room"),
    ("examples/room_to_gds", "examples/room_to_gds"),
    ("examples/qucs_to_room", "examples/qucs_to_room"),
    ("examples/room_to_qucs", "examples/room_to_qucs"),
    ("examples/xschem_to_room", "examples/xschem_to_room"),
    ("examples/room_to_xschem", "examples/room_to_xschem"),
    ("examples/oas_to_room", "examples/oas_to_room"),
    ("integrations/klayout/mroom", "integrations/klayout/mroom"),
]

KLAYOUT_FILE_RENAMES = [
    ("integrations/klayout/mroom/core.pri", "integrations/klayout/mroom/room.pri"),
    ("integrations/klayout/mroom/db_plugin/roomFormat.h", "integrations/klayout/mroom/db_plugin/roomFormat.h"),
    ("integrations/klayout/mroom/db_plugin/roomFormat.cc", "integrations/klayout/mroom/db_plugin/roomFormat.cc"),
    ("integrations/klayout/mroom/db_plugin/roomReader.h", "integrations/klayout/mroom/db_plugin/roomReader.h"),
    ("integrations/klayout/mroom/db_plugin/roomReader.cc", "integrations/klayout/mroom/db_plugin/roomReader.cc"),
    ("integrations/klayout/mroom/db_plugin/roomWriter.h", "integrations/klayout/mroom/db_plugin/roomWriter.h"),
    ("integrations/klayout/mroom/db_plugin/roomWriter.cc", "integrations/klayout/mroom/db_plugin/roomWriter.cc"),
    ("integrations/klayout/mroom/db_plugin/roomPlugin.cc", "integrations/klayout/mroom/db_plugin/roomPlugin.cc"),
]


def should_skip(path: Path) -> bool:
    parts = set(path.parts)
    if parts & SKIP_DIR_NAMES:
        # allow docs and source; skip only if any parent is skip
        for p in path.parents:
            if p.name in SKIP_DIR_NAMES:
                return True
    return False


def is_text_file(path: Path) -> bool:
    if path.suffix.lower() in TEXT_EXTS:
        return True
    if path.name in {"CMakeLists.txt", "README", "LICENSE", "Dockerfile"}:
        return True
    if path.name.startswith(".git"):
        return path.name in {".gitignore", ".gitattributes"}
    return False


def transform_text(s: str) -> str:
    for old, new in REPLACEMENTS:
        s = s.replace(old, new)
    for pat, repl in REGEX_REPLACEMENTS:
        s = re.sub(pat, repl, s)
    return s


def process_files() -> int:
    changed = 0
    for dirpath, dirnames, filenames in os.walk(ROOT):
        # prune
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES and not d.startswith("build")]
        for name in filenames:
            path = Path(dirpath) / name
            if should_skip(path) or not is_text_file(path):
                continue
            try:
                raw = path.read_bytes()
            except OSError:
                continue
            # skip binary-ish
            if b"\0" in raw[:2048]:
                continue
            for enc in ("utf-8", "utf-8-sig", "cp1252", "latin-1"):
                try:
                    text = raw.decode(enc)
                    break
                except UnicodeDecodeError:
                    text = None
            else:
                continue
            new = transform_text(text)
            if new != text:
                path.write_text(new, encoding="utf-8", newline="\n")
                changed += 1
                print(f"  edit: {path.relative_to(ROOT)}")
    return changed


def rename_path(old_rel: str, new_rel: str) -> None:
    old = ROOT / old_rel
    new = ROOT / new_rel
    if not old.exists():
        print(f"  miss: {old_rel}")
        return
    new.parent.mkdir(parents=True, exist_ok=True)
    if new.exists():
        print(f"  exists: {new_rel}")
        return
    shutil.move(str(old), str(new))
    print(f"  move: {old_rel} -> {new_rel}")


def main() -> None:
    print("1) Content rewrite...")
    n = process_files()
    print(f"   {n} files edited")

    print("2) Directory renames...")
    for a, b in DIR_RENAMES:
        rename_path(a, b)

    print("3) File renames...")
    for a, b in FILE_RENAMES:
        rename_path(a, b)
    for a, b in KLAYOUT_FILE_RENAMES:
        rename_path(a, b)

    # Second pass after renames (paths inside moved dirs)
    print("4) Second content pass...")
    n2 = process_files()
    print(f"   {n2} files edited")

    print("Done.")


if __name__ == "__main__":
    main()
