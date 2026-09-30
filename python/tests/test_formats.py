from __future__ import annotations

from pathlib import Path

import pytest

import room

REPO = Path(__file__).resolve().parents[2]
QUCS_SCH = REPO / "examples" / "qucs_to_room" / "data" / "rc_lowpass.sch"
XSCHEM_SCH = REPO / "examples" / "xschem_to_room" / "data" / "test.sch"


@pytest.fixture
def qucs_sch() -> Path:
    if not QUCS_SCH.is_file():
        pytest.skip(f"missing {QUCS_SCH}")
    return QUCS_SCH


@pytest.fixture
def xschem_sch() -> Path:
    if not XSCHEM_SCH.is_file():
        pytest.skip(f"missing {XSCHEM_SCH}")
    return XSCHEM_SCH


def test_qucs_to_room_roundtrip(qucs_sch: Path, tmp_path: Path):
    room_path = tmp_path / "qucs.room"
    sch_out = tmp_path / "out.sch"

    with room.from_qucs(qucs_sch) as db:
        assert len(db.cells()) >= 1
        db.save(room_path, view="schematic")
        db.to_qucs(sch_out)

    assert room_path.is_file() and room_path.stat().st_size > 0
    assert sch_out.is_file() and sch_out.stat().st_size > 0

    with room.open(room_path) as db:
        assert db.cell_names()


def test_xschem_to_room_roundtrip(xschem_sch: Path, tmp_path: Path):
    room_path = tmp_path / "xschem.room"
    sch_out = tmp_path / "out.sch"

    with room.from_xschem(xschem_sch) as db:
        assert len(db.cells()) >= 1
        db.save(room_path, view="schematic")
        db.to_xschem(sch_out)

    assert room_path.is_file()
    assert sch_out.is_file() and sch_out.stat().st_size > 0


def test_oas_export_from_gds(sample_gds: Path, tmp_path: Path):
    if not room.has_oas():
        pytest.skip("room_c built without OAS (zlib/room_oas)")

    oas_path = tmp_path / "layout.oas"
    with room.from_gds(sample_gds) as db:
        assert len(db.cells()) >= 1
        db.to_oas(oas_path)

    assert oas_path.is_file() and oas_path.stat().st_size > 0
    assert room.has_oas() is True


def test_cli_qucs_and_xschem(qucs_sch: Path, xschem_sch: Path, tmp_path: Path):
    from room.cli import main

    qucs_room = tmp_path / "q.room"
    x_room = tmp_path / "x.room"
    assert main(["convert", "--tool", "qucs", str(qucs_sch), str(qucs_room), "--view", "schematic"]) == 0
    assert main(["convert", "--tool", "xschem", str(xschem_sch), str(x_room), "--view", "schematic"]) == 0
    assert qucs_room.is_file() and x_room.is_file()
