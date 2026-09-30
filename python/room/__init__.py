"""ROOM Python bindings.

Typical use::

    import room
    db = room.open("design.room")
    print(db.cell_names())
    db.to_gds("out.gds")

CLI::

    python -m room info design.room
    python -m room ls design.room
    python -m room convert chip.gds chip.room
"""

from .api import (
    Box,
    Cell,
    CellContent,
    Database,
    FileInfo,
    Instance,
    Layer,
    LayerPurpose,
    Orient,
    Rect,
    ShapeType,
    ViewType,
    has_oas,
    sniff,
)
from .cli import main as cli_main

__all__ = [
    "Box",
    "Cell",
    "CellContent",
    "Database",
    "FileInfo",
    "Instance",
    "Layer",
    "LayerPurpose",
    "Orient",
    "Rect",
    "ShapeType",
    "ViewType",
    "cli_main",
    "create",
    "from_gds",
    "from_oas",
    "from_qucs",
    "from_xschem",
    "has_oas",
    "open",
    "sniff",
]

open = Database.open
create = Database.create
from_gds = Database.from_gds
from_qucs = Database.from_qucs
from_xschem = Database.from_xschem
from_oas = Database.from_oas
