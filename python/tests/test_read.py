from __future__ import annotations

from pathlib import Path

import room


def test_create_empty_database():
    with room.create() as db:
        assert db.version == "1.0"
        assert db.lib_name == "default"
        assert db.cell_names() == []
        assert db.layers == []


def test_sniff_and_open_roundtrip(tmp_room: Path):
    info = room.sniff(tmp_room)
    assert info.cell_count >= 1
    assert info.lib_name

    with room.open(tmp_room) as db:
        names = db.cell_names()
        assert len(names) == info.cell_count or len(names) >= 1
        assert db.lib_name == info.lib_name
        meta = db.info()
        assert meta["cell_count"] == len(names)
        assert isinstance(meta["cells"], list)


def test_find_cell(tmp_room: Path):
    with room.open(tmp_room) as db:
        name = db.cell_names()[0]
        cell = db.find_cell(name)
        assert cell is not None
        assert cell.name == name
        assert db.find_cell("__no_such_cell__") is None


def test_layout_shapes_readable(tmp_room: Path):
    with room.open(tmp_room) as db:
        found_shapes = False
        for cell in db.cells:
            layout = cell.layout
            if layout is None:
                continue
            shapes = layout.shapes
            if shapes:
                found_shapes = True
                # At least one shape entry (rect or typed dict)
                assert len(shapes) >= 1
                break
        assert found_shapes
