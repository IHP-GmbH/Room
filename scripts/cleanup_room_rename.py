#!/usr/bin/env python3
"""Cleanup leftover core/.core references after rename."""
from __future__ import annotations

import os
import re
import shutil
from pathlib import Path

ROOT = Path(r"C:\Users\anton\Documents\CommonDB")
SKIP = {".git", "build", "build-tools", "build-wsl", "build-export", "third_party", "install", "generated"}

REPL = [
    ("core_bootstrap_capnp", "room_bootstrap_capnp"),
    ("sniffCoreFile", "sniffRoomFile"),
    ("CoreFileInfo", "RoomFileInfo"),
    ("layCoreReaderPlugin", "layRoomReaderPlugin"),
    ("layCoreWriterPlugin", "layRoomWriterPlugin"),
    ("corePlugin.h", "roomPlugin.h"),
    ("corePlugin.cc", "roomPlugin.cc"),
    ("coreFormat.h", "roomFormat.h"),
    ("coreReader.h", "roomReader.h"),
    ("coreWriter.h", "roomWriter.h"),
    ("corepaths.html", "roompaths.html"),
    ("core_paths.h", "room_paths.h"),
    ("CORE::room", "ROOM::room"),  # just in case
    ("CORE::core", "ROOM::room"),
    ("namespace coredb", "namespace roomdb"),
    ("kDefaultCore", "kDefaultRoom"),
    ("corePath", "roomPath"),
    ("Saved CORE", "Saved ROOM"),
    ("output.core", "output.room"),
    ("input.core", "input.room"),
    ("schematic.core", "schematic.room"),
    ("layout.core", "layout.room"),
    ("sample.core", "sample.room"),
    ("compact.core", "compact.room"),
    ("verbose.room", "verbose.room"),  # noop
    ("sniff_fixture.schematic.core", "sniff_fixture.schematic.room"),
    ("gds_props_roundtrip.core", "gds_props_roundtrip.room"),
    ("`<cell>.<view>.core`", "`<cell>.<view>.room`"),
    ("<cell>.<view>.core", "<cell>.<view>.room"),
    ("cell.view.core", "cell.view.room"),
    ("name.view.core", "name.view.room"),
    ("`.core`", "`.room`"),
    ("<code>.core</code>", "<code>.room</code>"),
    ("*.core", "*.room"),
    (".core file", ".room file"),
    (".core files", ".room files"),
    (".core`", ".room`"),
    (".core'", ".room'"),
    ('.core"', '.room"'),
    (".core ", ".room "),
    (".core)", ".room)"),
    (".core,", ".room,"),
    (".core.", ".room."),
    (".core<", ".room<"),
    (".core\n", ".room\n"),
    ("Binary <code>.core</code>", "Binary <code>.room</code>"),
]

FILE_MOVES = [
    ("integrations/klayout/mroom/lay_plugin/layCoreReaderPlugin.h",
     "integrations/klayout/mroom/lay_plugin/layRoomReaderPlugin.h"),
    ("integrations/klayout/mroom/lay_plugin/layCoreReaderPlugin.cc",
     "integrations/klayout/mroom/lay_plugin/layRoomReaderPlugin.cc"),
    ("integrations/klayout/mroom/lay_plugin/layCoreWriterPlugin.h",
     "integrations/klayout/mroom/lay_plugin/layRoomWriterPlugin.h"),
    ("integrations/klayout/mroom/lay_plugin/layCoreWriterPlugin.cc",
     "integrations/klayout/mroom/lay_plugin/layRoomWriterPlugin.cc"),
    ("integrations/klayout/mroom/db_plugin/corePlugin.h",
     "integrations/klayout/mroom/db_plugin/roomPlugin.h"),
]


def skip(p: Path) -> bool:
    return any(part in SKIP or part.startswith("build") for part in p.parts)


def main() -> None:
    for a, b in FILE_MOVES:
        old, new = ROOT / a, ROOT / b
        if old.exists() and not new.exists():
            shutil.move(str(old), str(new))
            print("move", a, "->", b)

    exts = {".h", ".hpp", ".cc", ".cpp", ".c", ".md", ".html", ".cmake", ".txt",
            ".pri", ".pro", ".patch", ".yml", ".yaml", ".json", ".lym", ".rb",
            ".py", ".sh", ".cmd", ".bat", ".capnp", ".gitignore"}
    n = 0
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP and not d.startswith("build")]
        for name in filenames:
            path = Path(dirpath) / name
            if skip(path) or path.suffix.lower() not in exts and name != "CMakeLists.txt":
                continue
            if path.name == "rename_core_to_room.py" or path.name == "cleanup_room_rename.py":
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except Exception:
                continue
            new = text
            for old, rep in REPL:
                new = new.replace(old, rep)
            # leftover bare namespace core:: in C++ (not room::schema already)
            new = re.sub(r"(?<![\w:])core::", "room::", new)
            if new != text:
                path.write_text(new, encoding="utf-8", newline="\n")
                n += 1
                print("edit", path.relative_to(ROOT))
    print("edited", n)


if __name__ == "__main__":
    main()
