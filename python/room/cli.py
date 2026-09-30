"""Command-line interface: ``python -m room`` / ``room``."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .api import Database, has_oas, parse_view, sniff


def _cmd_info(args: argparse.Namespace) -> int:
    path = Path(args.path)
    if args.sniff:
        info = sniff(path)
        data = {
            "version": info.version,
            "generator": info.generator,
            "technology": info.technology,
            "lib_name": info.lib_name,
            "view": info.view.name.lower(),
            "cell_count": info.cell_count,
            "primary_cell": info.primary_cell,
        }
    else:
        with Database.open(path) as db:
            data = db.info()
    if args.json:
        print(json.dumps(data, indent=2))
    else:
        for key, value in data.items():
            if key == "cells" and isinstance(value, list) and not args.verbose:
                print(f"cells: {len(value)}")
            else:
                print(f"{key}: {value}")
    return 0


def _cmd_ls(args: argparse.Namespace) -> int:
    with Database.open(args.path) as db:
        names = db.cell_names()
        if args.cells_only or not args.layers:
            for name in names:
                print(name)
        if args.layers:
            for layer in db.layers:
                print(f"L{layer.layer_num}/{layer.data_type}\t{layer.name}\t{layer.purpose.name.lower()}")
    return 0


def _detect_sch_tool(path: Path, tool: str) -> str:
    if tool in {"qucs", "xschem"}:
        return tool
    head = path.read_text(encoding="utf-8", errors="ignore")[:400].lower()
    if "xschem" in head or head.lstrip().startswith("v {") or "\ng {" in head or head.startswith("g {"):
        return "xschem"
    return "qucs"


def _open_source(src: Path, fmt: str, tool: str) -> Database:
    fmt = fmt.lower().strip()
    ext = src.suffix.lower()

    if fmt == "gds" or (not fmt and ext in {".gds", ".gds2"}):
        return Database.from_gds(src)
    if fmt in {"oas", "oasis"} or (not fmt and ext in {".oas", ".oasis"}):
        if not has_oas():
            raise RuntimeError("OAS support not available in this room_c build")
        return Database.from_oas(src)
    if fmt == "qucs" or (fmt == "" and ext == ".sch" and _detect_sch_tool(src, tool) == "qucs"):
        return Database.from_qucs(src)
    if fmt == "xschem" or ext == ".sym" or (ext == ".sch" and _detect_sch_tool(src, tool) == "xschem"):
        return Database.from_xschem(src)
    if fmt == "room" or ext == ".room":
        return Database.open(src)
    raise RuntimeError(f"Cannot detect input format for {src}")


def _write_dest(db: Database, dst: Path, *, cell: str | None, view: str, tool: str, fmt: str) -> None:
    ext = dst.suffix.lower()
    fmt = fmt.lower().strip()
    cell_name = cell or None

    if ext == ".room" or fmt == "room":
        db.save(dst, view=parse_view(view))
        return
    if ext in {".gds", ".gds2"} or fmt == "gds":
        db.to_gds(dst)
        return
    if ext in {".oas", ".oasis"} or fmt in {"oas", "oasis"}:
        db.to_oas(dst)
        return
    if fmt == "xschem" or tool == "xschem" or ext == ".sym" or (ext == "" and tool == "xschem"):
        db.to_xschem(dst, cell=cell_name)
        return
    if ext == ".sch" or fmt == "qucs" or tool == "qucs":
        # .sch defaults to Qucs unless --tool/--format xschem was set above.
        if tool == "xschem" or fmt == "xschem":
            db.to_xschem(dst, cell=cell_name)
        else:
            db.to_qucs(dst, cell=cell_name)
        return
    if ext == "":
        db.to_xschem(dst, cell=cell_name)
        return
    raise RuntimeError(f"Unsupported output format for {dst}")


def _cmd_convert(args: argparse.Namespace) -> int:
    src = Path(args.source)
    dst = Path(args.dest)
    with _open_source(src, args.format, args.tool) as db:
        if args.lib_name:
            db.lib_name = args.lib_name
        # Prefer explicit output format; else infer from destination / tool.
        out_fmt = args.format if dst.suffix.lower() == "" and args.format else ""
        if args.tool == "xschem" and dst.suffix.lower() == ".sch":
            out_fmt = "xschem"
        _write_dest(
            db,
            dst,
            cell=args.cell or None,
            view=args.view,
            tool=args.tool,
            fmt=out_fmt or args.format,
        )
    print(f"Wrote {dst}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="room",
        description="ROOM database CLI (inspect, list, convert).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_info = sub.add_parser("info", help="Show database / file metadata")
    p_info.add_argument("path", help="Path to .room file")
    p_info.add_argument("--sniff", action="store_true", help="Header-only read (no full geometry decode)")
    p_info.add_argument("--json", action="store_true", help="JSON output")
    p_info.add_argument("-v", "--verbose", action="store_true", help="Include full cell name list")
    p_info.set_defaults(func=_cmd_info)

    p_ls = sub.add_parser("ls", help="List cells (and optionally layers)")
    p_ls.add_argument("path", help="Path to .room file")
    p_ls.add_argument("--cells", dest="cells_only", action="store_true", help="List cell names (default)")
    p_ls.add_argument("--layers", action="store_true", help="Also list library layers")
    p_ls.set_defaults(func=_cmd_ls)

    p_conv = sub.add_parser(
        "convert",
        help="Convert between GDS / OASIS / Qucs / Xschem and ROOM",
    )
    p_conv.add_argument("source", help="Input file (.gds/.oas/.sch/.sym/.room)")
    p_conv.add_argument("dest", help="Output file (.room/.gds/.oas/.sch/.sym) or Xschem directory")
    p_conv.add_argument("--view", default="layout", help="View type when writing .room (default: layout)")
    p_conv.add_argument("--lib-name", default="", help="Override library name on import")
    p_conv.add_argument("--cell", default="", help="Cell name for schematic export (default: first/primary)")
    p_conv.add_argument(
        "--format",
        default="",
        help="Force format name: gds, oas, qucs, xschem, room",
    )
    p_conv.add_argument(
        "--tool",
        choices=("auto", "qucs", "xschem"),
        default="auto",
        help="For .sch files choose Qucs or Xschem (default: auto-detect)",
    )
    p_conv.set_defaults(func=_cmd_convert)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
