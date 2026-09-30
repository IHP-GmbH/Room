from __future__ import annotations

import json
from pathlib import Path

from room.cli import main


def test_cli_info_and_ls(tmp_room: Path, capsys):
    assert main(["info", str(tmp_room)]) == 0
    out = capsys.readouterr().out
    assert "lib_name:" in out
    assert "cell_count:" in out

    assert main(["info", "--json", str(tmp_room)]) == 0
    data = json.loads(capsys.readouterr().out)
    assert "cells" in data

    assert main(["info", "--sniff", "--json", str(tmp_room)]) == 0
    sniff_data = json.loads(capsys.readouterr().out)
    assert sniff_data["cell_count"] >= 1

    assert main(["ls", str(tmp_room)]) == 0
    names = [line for line in capsys.readouterr().out.splitlines() if line.strip()]
    assert names
