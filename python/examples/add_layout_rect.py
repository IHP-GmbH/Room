#!/usr/bin/env python3
"""Create my_rect.layout.room and register it in sg13g2.projects (one shot)."""

from __future__ import annotations

from pathlib import Path

import room
from room import LayerPurpose, ViewType

lib = Path(r"C:/Users/anton/Documents/IHP-AnalogAcademy/modules/module_0_foundations")
projects = Path(r"C:/Users/anton/Documents/LibMan/tests/data/sg13g2.projects")
name = "my_rect"
lib_name = "module_0_foundations"

out = lib / name / f"{name}.layout.room"
out.parent.mkdir(parents=True, exist_ok=True)

with room.create() as db:
    db.lib_name = lib_name
    db.generator = "add_layout_rect.py"
    db.add_layer(1, 0, "Metal1", LayerPurpose.DRAWING)
    cell = db.get_or_create_cell(name)
    layout = cell.ensure_content(ViewType.LAYOUT, dbu_per_micron=1000.0)
    layout.add_rect(layer_id=0, llx=0, lly=0, urx=2000, ury=1000)
    db.save(out, ViewType.LAYOUT)
    print("saved", out)

# LibMan define() — path relative to the .projects directory
rel = out.resolve().relative_to(projects.resolve().parent)
stored = str(rel).replace("/", "\\")
line = f'define("{lib_name}", "{stored}");'

text = projects.read_text(encoding="utf-8") if projects.exists() else ""
already = any(
    lib_name in ln and out.name in ln and "define(" in ln for ln in text.splitlines()
)
if already:
    print("already in project:", line)
else:
    lines = text.splitlines(keepends=True)
    insert_at = next(
        (i for i, ln in enumerate(lines) if ln.lstrip().startswith("attach(")),
        len(lines),
    )
    entry = line + "\n"
    lines.insert(insert_at, entry)
    if lines and not "".join(lines).endswith("\n"):
        lines.append("\n")
    projects.write_text("".join(lines), encoding="utf-8")
    print("appended", line)

print("Re-open the project in LibMan (or Ctrl+E → Save) to reload.")
