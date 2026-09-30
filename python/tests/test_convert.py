from __future__ import annotations

from pathlib import Path

import room


def test_gds_to_room_to_gds(sample_gds: Path, tmp_path: Path):
    room_path = tmp_path / "from_gds.room"
    gds_out = tmp_path / "roundtrip.gds"

    with room.from_gds(sample_gds) as db:
        assert len(db.cells()) >= 1
        db.generator = "room-python-test"
        db.save(room_path)
        db.to_gds(gds_out)

    assert room_path.is_file() and room_path.stat().st_size > 0
    assert gds_out.is_file() and gds_out.stat().st_size > 0

    with room.open(room_path) as db:
        assert db.generator == "room-python-test"
        assert len(db.cell_names()) >= 1


def test_cli_convert(sample_gds: Path, tmp_path: Path):
    from room.cli import main

    room_path = tmp_path / "cli.room"
    gds_out = tmp_path / "cli.gds"
    assert main(["convert", str(sample_gds), str(room_path)]) == 0
    assert room_path.is_file()
    assert main(["convert", str(room_path), str(gds_out)]) == 0
    assert gds_out.is_file()
