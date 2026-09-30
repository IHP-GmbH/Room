from __future__ import annotations

from pathlib import Path

import room
from room import LayerPurpose, Orient, ViewType


def test_mutate_create_save_reload(tmp_path: Path):
    path = tmp_path / "mutated.room"

    with room.create() as db:
        db.lib_name = "demo_lib"
        db.technology = "sg13g2"
        db.generator = "python-mutate-test"
        db.add_layer(1, 0, "Metal1", LayerPurpose.DRAWING)
        cell = db.get_or_create_cell("INVX1")
        layout = cell.ensure_content(ViewType.LAYOUT, dbu_per_micron=1000.0)
        layout.add_rect(layer_id=0, llx=0, lly=0, urx=100, ury=200)
        layout.add_rect(layer_id=0, llx=10, lly=10, urx=40, ury=50)
        child = db.get_or_create_cell("FILL")
        child.ensure_content(ViewType.LAYOUT)
        layout.add_instance("FILL", x=1000, y=2000, orient=Orient.R0, mag=1.0)
        db.save(path)

    with room.open(path) as db:
        assert db.lib_name == "demo_lib"
        assert db.technology == "sg13g2"
        assert db.generator == "python-mutate-test"
        assert [layer.name for layer in db.layers] == ["Metal1"]
        cell = db.find_cell("INVX1")
        assert cell is not None
        layout = cell.layout
        assert layout is not None
        rects = [s for s in layout.shapes if isinstance(s, room.Rect)]
        assert len(rects) == 2
        boxes = {(r.llx, r.lly, r.urx, r.ury) for r in rects}
        assert boxes == {(0, 0, 100, 200), (10, 10, 40, 50)}
        insts = layout.instances
        assert len(insts) == 1
        assert insts[0].cell_name == "FILL"
        assert insts[0].x == 1000 and insts[0].y == 2000
        bbox = layout.bbox
        assert not bbox.empty
        assert bbox.urx >= 100


def test_get_or_create_is_idempotent():
    with room.create() as db:
        a = db.get_or_create_cell("A")
        b = db.get_or_create_cell("A")
        assert a.name == b.name == "A"
        assert len(db.cells) == 1
