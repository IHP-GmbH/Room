"""High-level Python API for ROOM databases."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

from . import _lib


class ViewType(IntEnum):
    LAYOUT = 0
    SCHEMATIC = 1
    SYMBOL = 2
    ABSTRACT = 3
    EMMODEL = 4


class ShapeType(IntEnum):
    RECT = 0
    POLYGON = 1
    PATH = 2
    TEXT = 3
    ARC = 4


class Orient(IntEnum):
    R0 = 0
    R90 = 1
    R180 = 2
    R270 = 3
    MY = 4
    MX = 5
    MX90 = 6
    MY90 = 7


class LayerPurpose(IntEnum):
    DRAWING = 0
    PIN = 1
    LABEL = 2
    BOUNDARY = 3
    BLOCKAGE = 4
    WIRE = 5
    FILL = 6
    OTHER = 7


VIEW_NAMES = {
    "layout": ViewType.LAYOUT,
    "schematic": ViewType.SCHEMATIC,
    "symbol": ViewType.SYMBOL,
    "abstract": ViewType.ABSTRACT,
    "emmodel": ViewType.EMMODEL,
    "em_model": ViewType.EMMODEL,
}


def parse_view(value: str | ViewType | int) -> ViewType:
    if isinstance(value, ViewType):
        return value
    if isinstance(value, int):
        return ViewType(value)
    key = str(value).strip().lower()
    if key not in VIEW_NAMES:
        raise ValueError(f"Unknown view type: {value!r}")
    return VIEW_NAMES[key]


@dataclass(frozen=True)
class Box:
    llx: int
    lly: int
    urx: int
    ury: int
    empty: bool = False


@dataclass(frozen=True)
class Rect:
    layer_id: int
    llx: int
    lly: int
    urx: int
    ury: int


@dataclass(frozen=True)
class Instance:
    cell_name: str
    x: int
    y: int
    orient: Orient = Orient.R0
    mag: float = 1.0


@dataclass(frozen=True)
class Layer:
    layer_num: int
    data_type: int
    name: str
    purpose: LayerPurpose = LayerPurpose.DRAWING


@dataclass(frozen=True)
class FileInfo:
    version: str
    generator: str
    technology: str
    lib_name: str
    view: ViewType
    cell_count: int
    primary_cell: str


def sniff(path: str | Path) -> FileInfo:
    """Read .room header metadata without fully decoding geometry."""
    lib = _lib.get_lib()
    info = _lib.RoomFileInfoC()
    err = _lib.errbuf()
    rc = lib.room_sniff(_lib.cstr(str(path)), info, err, len(err))
    if rc != 0:
        raise RuntimeError(err.value.decode("utf-8", errors="replace") or "sniff failed")
    return FileInfo(
        version=_lib.decode(info.version),
        generator=_lib.decode(info.generator),
        technology=_lib.decode(info.technology),
        lib_name=_lib.decode(info.lib_name),
        view=ViewType(info.view),
        cell_count=int(info.cell_count),
        primary_cell=_lib.decode(info.primary_cell),
    )


def has_oas() -> bool:
    """True when room_c was linked with OASIS support (zlib / room_oas)."""
    return bool(_lib.get_lib().room_has_oas())


class CellContent:
    def __init__(self, db: "Database", cell_index: int, content_index: int):
        self._db = db
        self._cell_index = cell_index
        self._content_index = content_index

    def view(self) -> ViewType:
        """View kind (C++ ``viewType()``)."""
        return ViewType(self._db._lib.room_db_cell_content_view(self._db._ptr, self._cell_index, self._content_index))

    def dbu_per_micron(self) -> float:
        """DBU per micron for layout views (C++ ``dbuPerMicron()``)."""
        return float(self._db._lib.room_db_cell_content_dbu(self._db._ptr, self._cell_index, self._content_index))

    def bbox(self) -> Box:
        """Cached axis-aligned bounding box (C++ ``block().bbox()``)."""
        out = _lib.RoomBoxC()
        if self._db._lib.room_db_cell_bbox(self._db._ptr, self._cell_index, self._content_index, out) != 0:
            raise RuntimeError("bbox unavailable")
        return Box(out.llx, out.lly, out.urx, out.ury, bool(out.empty))

    def shapes(self) -> list[Rect | dict]:
        """Geometry list (C++ ``block().shapes()``). Rectangles are ``Rect``; other kinds are dicts."""
        count = self._db._lib.room_db_shape_count(self._db._ptr, self._cell_index, self._content_index)
        result: list[Rect | dict] = []
        for i in range(count):
            st = ShapeType(self._db._lib.room_db_shape_type(self._db._ptr, self._cell_index, self._content_index, i))
            if st == ShapeType.RECT:
                rect = _lib.RoomRectC()
                if self._db._lib.room_db_shape_rect(self._db._ptr, self._cell_index, self._content_index, i, rect) == 0:
                    result.append(Rect(rect.layer_id, rect.llx, rect.lly, rect.urx, rect.ury))
            else:
                result.append({"type": st.name.lower(), "index": i})
        return result

    def add_rect(self, layer_id: int, llx: int, lly: int, urx: int, ury: int) -> int:
        idx = self._db._lib.room_db_add_rect(
            self._db._ptr, self._cell_index, self._content_index, int(layer_id), int(llx), int(lly), int(urx), int(ury)
        )
        if idx < 0:
            raise RuntimeError("failed to add rect")
        return int(idx)

    def instances(self) -> list[Instance]:
        """Child placements (C++ ``block().instances()``)."""
        count = self._db._lib.room_db_instance_count(self._db._ptr, self._cell_index, self._content_index)
        result: list[Instance] = []
        for i in range(count):
            out = _lib.RoomInstanceC()
            if self._db._lib.room_db_instance_get(self._db._ptr, self._cell_index, self._content_index, i, out) != 0:
                continue
            result.append(
                Instance(
                    cell_name=_lib.decode(out.cell_name),
                    x=out.x,
                    y=out.y,
                    orient=Orient(out.orient),
                    mag=out.mag,
                )
            )
        return result

    def add_instance(
        self,
        cell_name: str,
        x: int = 0,
        y: int = 0,
        orient: Orient | int = Orient.R0,
        mag: float = 1.0,
    ) -> int:
        idx = self._db._lib.room_db_add_instance(
            self._db._ptr,
            self._cell_index,
            self._content_index,
            _lib.cstr(cell_name),
            int(x),
            int(y),
            int(orient),
            float(mag),
        )
        if idx < 0:
            raise RuntimeError("failed to add instance")
        return int(idx)


class Cell:
    def __init__(self, db: "Database", index: int):
        self._db = db
        self._index = index

    def name(self) -> str:
        """Cell name (C++ ``Cell::name()``)."""
        return _lib.decode(self._db._lib.room_db_cell_name(self._db._ptr, self._index))

    def contents(self) -> list[CellContent]:
        """All view bodies on this cell (C++ ``contents()``)."""
        n = self._db._lib.room_db_cell_content_count(self._db._ptr, self._index)
        return [CellContent(self._db, self._index, i) for i in range(n)]

    def content(self, view: str | ViewType | int = ViewType.LAYOUT) -> CellContent | None:
        """Lookup view body (C++ ``findContent``)."""
        wanted = parse_view(view)
        for content in self.contents():
            if content.view() == wanted:
                return content
        return None

    def ensure_content(self, view: str | ViewType | int = ViewType.LAYOUT, dbu_per_micron: float = 1000.0) -> CellContent:
        """Get or create view body (C++ ``getOrCreateContent``)."""
        idx = self._db._lib.room_db_cell_ensure_content(
            self._db._ptr, self._index, int(parse_view(view)), float(dbu_per_micron)
        )
        if idx < 0:
            raise RuntimeError(f"failed to ensure content for cell {self.name()!r}")
        return CellContent(self._db, self._index, idx)

    def layout(self) -> CellContent | None:
        """Convenience for ``content(ViewType.LAYOUT)``."""
        return self.content(ViewType.LAYOUT)


class Database:
    """Root handle for a ROOM design (.room file)."""

    def __init__(self, ptr, *, owned: bool = True):
        if not ptr:
            raise RuntimeError("null ROOM database handle")
        self._ptr = ptr
        self._owned = owned
        self._lib = _lib.get_lib()

    def __del__(self):
        self.close()

    def __enter__(self) -> "Database":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def close(self) -> None:
        if getattr(self, "_owned", False) and getattr(self, "_ptr", None):
            self._lib.room_db_free(self._ptr)
            self._ptr = None
            self._owned = False

    @classmethod
    def create(cls) -> "Database":
        ptr = _lib.get_lib().room_db_create()
        if not ptr:
            raise RuntimeError("failed to create database")
        return cls(ptr)

    @classmethod
    def open(cls, path: str | Path) -> "Database":
        err = _lib.errbuf()
        ptr = _lib.get_lib().room_db_open(_lib.cstr(str(path)), err, len(err))
        if not ptr:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to open {path}")
        return cls(ptr)

    @classmethod
    def from_gds(cls, path: str | Path) -> "Database":
        err = _lib.errbuf()
        ptr = _lib.get_lib().room_from_gds(_lib.cstr(str(path)), err, len(err))
        if not ptr:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to import {path}")
        return cls(ptr)

    @classmethod
    def from_qucs(cls, path: str | Path) -> "Database":
        err = _lib.errbuf()
        ptr = _lib.get_lib().room_from_qucs(_lib.cstr(str(path)), err, len(err))
        if not ptr:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to import {path}")
        return cls(ptr)

    @classmethod
    def from_xschem(cls, path: str | Path) -> "Database":
        err = _lib.errbuf()
        ptr = _lib.get_lib().room_from_xschem(_lib.cstr(str(path)), err, len(err))
        if not ptr:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to import {path}")
        return cls(ptr)

    @classmethod
    def from_oas(cls, path: str | Path) -> "Database":
        if not has_oas():
            raise RuntimeError("OAS support was not built into room_c (install zlib and rebuild)")
        err = _lib.errbuf()
        ptr = _lib.get_lib().room_from_oas(_lib.cstr(str(path)), err, len(err))
        if not ptr:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to import {path}")
        return cls(ptr)

    def save(self, path: str | Path, view: str | ViewType | int = ViewType.LAYOUT) -> None:
        err = _lib.errbuf()
        rc = self._lib.room_db_save(self._ptr, _lib.cstr(str(path)), int(parse_view(view)), err, len(err))
        if rc != 0:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to save {path}")

    def to_gds(self, path: str | Path) -> None:
        err = _lib.errbuf()
        rc = self._lib.room_to_gds(self._ptr, _lib.cstr(str(path)), err, len(err))
        if rc != 0:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to export {path}")

    def to_qucs(self, path: str | Path, cell: str | None = None) -> None:
        err = _lib.errbuf()
        rc = self._lib.room_to_qucs(self._ptr, _lib.cstr(str(path)), _lib.cstr(cell), err, len(err))
        if rc != 0:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to export {path}")

    def to_xschem(self, path: str | Path, cell: str | None = None) -> None:
        err = _lib.errbuf()
        rc = self._lib.room_to_xschem(self._ptr, _lib.cstr(str(path)), _lib.cstr(cell), err, len(err))
        if rc != 0:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to export {path}")

    def to_oas(self, path: str | Path) -> None:
        if not has_oas():
            raise RuntimeError("OAS support was not built into room_c (install zlib and rebuild)")
        err = _lib.errbuf()
        rc = self._lib.room_to_oas(self._ptr, _lib.cstr(str(path)), err, len(err))
        if rc != 0:
            raise RuntimeError(err.value.decode("utf-8", errors="replace") or f"failed to export {path}")

    @property
    def version(self) -> str:
        return _lib.decode(self._lib.room_db_version(self._ptr))

    @version.setter
    def version(self, value: str) -> None:
        self._lib.room_db_set_version(self._ptr, _lib.cstr(value))

    @property
    def generator(self) -> str:
        return _lib.decode(self._lib.room_db_generator(self._ptr))

    @generator.setter
    def generator(self, value: str) -> None:
        self._lib.room_db_set_generator(self._ptr, _lib.cstr(value))

    @property
    def technology(self) -> str:
        return _lib.decode(self._lib.room_db_technology(self._ptr))

    @technology.setter
    def technology(self, value: str) -> None:
        self._lib.room_db_set_technology(self._ptr, _lib.cstr(value))

    @property
    def file_view(self) -> ViewType:
        return ViewType(self._lib.room_db_file_view(self._ptr))

    @file_view.setter
    def file_view(self, value: str | ViewType | int) -> None:
        self._lib.room_db_set_file_view(self._ptr, int(parse_view(value)))

    @property
    def lib_name(self) -> str:
        return _lib.decode(self._lib.room_db_lib_name(self._ptr))

    @lib_name.setter
    def lib_name(self, value: str) -> None:
        self._lib.room_db_set_lib_name(self._ptr, _lib.cstr(value))

    def summary_cell_count(self) -> int:
        return int(self._lib.room_db_summary_cell_count(self._ptr))

    def summary_primary_cell(self) -> str:
        return _lib.decode(self._lib.room_db_summary_primary_cell(self._ptr))

    def cells(self) -> list[Cell]:
        """All cells in the library (C++ ``lib().cells()``)."""
        n = self._lib.room_db_cell_count(self._ptr)
        return [Cell(self, i) for i in range(n)]

    def cell_names(self) -> list[str]:
        return [c.name() for c in self.cells()]

    def find_cell(self, name: str) -> Cell | None:
        idx = self._lib.room_db_find_cell(self._ptr, _lib.cstr(name))
        return Cell(self, idx) if idx >= 0 else None

    def get_or_create_cell(self, name: str) -> Cell:
        idx = self._lib.room_db_add_cell(self._ptr, _lib.cstr(name))
        if idx < 0:
            raise RuntimeError(f"failed to create cell {name!r}")
        return Cell(self, idx)

    def layers(self) -> list[Layer]:
        """Shared layer table (C++ ``lib().layers()``)."""
        n = self._lib.room_db_layer_count(self._ptr)
        result: list[Layer] = []
        for i in range(n):
            out = _lib.RoomLayerC()
            if self._lib.room_db_layer_get(self._ptr, i, out) != 0:
                continue
            result.append(
                Layer(
                    layer_num=out.layer_num,
                    data_type=out.data_type,
                    name=_lib.decode(out.name),
                    purpose=LayerPurpose(out.purpose),
                )
            )
        return result

    def add_layer(
        self,
        layer_num: int,
        data_type: int = 0,
        name: str = "",
        purpose: LayerPurpose | int = LayerPurpose.DRAWING,
    ) -> int:
        idx = self._lib.room_db_layer_add(
            self._ptr, int(layer_num), int(data_type), _lib.cstr(name), int(purpose)
        )
        if idx < 0:
            raise RuntimeError("failed to add layer")
        return int(idx)

    def info(self) -> dict:
        return {
            "version": self.version,
            "generator": self.generator,
            "technology": self.technology,
            "lib_name": self.lib_name,
            "file_view": self.file_view.name.lower(),
            "cell_count": len(self.cells()),
            "cells": self.cell_names(),
            "layer_count": len(self.layers()),
            "summary_cell_count": self.summary_cell_count(),
            "summary_primary_cell": self.summary_primary_cell(),
        }
