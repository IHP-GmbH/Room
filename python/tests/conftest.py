from __future__ import annotations

import os
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SAMPLE_GDS = REPO_ROOT / "testdata" / "sample.gds"


@pytest.fixture(scope="session")
def sample_gds() -> Path:
    if not SAMPLE_GDS.is_file():
        pytest.skip(f"missing fixture {SAMPLE_GDS}")
    return SAMPLE_GDS


@pytest.fixture
def tmp_room(tmp_path: Path, sample_gds: Path) -> Path:
    import room

    out = tmp_path / "sample.room"
    with room.from_gds(sample_gds) as db:
        db.save(out)
    return out
