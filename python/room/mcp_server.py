"""ROOM stdio MCP server — full access to the ROOM Python API for LLM agents.

Protocol: JSON-RPC 2.0 over newline-delimited stdin/stdout (MCP tools subset).

Run::

    python -m room.mcp_server
    # or
    python -m room mcp

Primary tool: ``room_python_eval`` — executes arbitrary Python with the same
surface as ``import room`` (Database / Cell / CellContent / converters / …).
Convenience wrappers (info/ls/sniff/convert) are thin helpers around that API.
"""

from __future__ import annotations

import inspect
import json
import sys
import traceback
from typing import Any

import room as room_pkg
from . import api as room_api
from .cli import main as cli_main


PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "room", "version": "0.2.0"}


def _ok_text(text: str) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": text}]}


def _err_text(message: str) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": message}], "isError": True}


def _public_methods(cls: type) -> list[str]:
    names: list[str] = []
    for name, obj in inspect.getmembers(cls):
        if name.startswith("_"):
            continue
        if inspect.isfunction(obj) or inspect.ismethoddescriptor(obj) or isinstance(
            obj, (staticmethod, classmethod, property)
        ):
            names.append(name)
        elif callable(obj):
            names.append(name)
    # Also instance methods from the class dict / MRO that getmembers may miss as unbound
    for name in dir(cls):
        if name.startswith("_"):
            continue
        if name not in names:
            attr = getattr(cls, name, None)
            if callable(attr) or isinstance(attr, property):
                names.append(name)
    return sorted(set(names))


def _api_help_text() -> str:
    lines = [
        "ROOM Python API (full access via room_python_eval / `import room`)",
        "",
        "Module entry points:",
        "  room.open(path)            # Cap’n Proto OR room-json *.room",
        "  room.create()",
        "  room.from_gds / from_oas / from_qucs / from_xschem(path)",
        "  room.sniff(path) / room.has_oas()",
        "  Types: ViewType, ShapeType, Orient, LayerPurpose, Box, Rect, Instance, Pin, Layer, …",
        "",
        f"Database methods: {', '.join(_public_methods(room_api.Database))}",
        f"Cell methods: {', '.join(_public_methods(room_api.Cell))}",
        f"CellContent methods: {', '.join(_public_methods(room_api.CellContent))}",
        "",
        "Typical inspect:",
        "  db = room.open(r'…/cell.symbol.room')",
        "  cell = db.find_cell('cell') or db.cells()[0]",
        "  cnt = cell.content()  # or content('schematic'|'symbol'|'layout')",
        "  result = [p.name for p in cnt.pins()]",
        "  result = [(i.name, i.cell_name) for i in cnt.instances()]  # label, master",
        "",
        "Mutate / save:",
        "  cell = db.get_or_create_cell('name')  # alias: db.create_cell",
        "  cnt = cell.ensure_content('layout')",
        "  cnt.add_rect(...); cnt.add_instance(...)",
        "  cnt.set_instance_property_by_name('Vdd', 'value', '1.4')  # persists in DB",
        "  # Do NOT mutate Instance.properties dict alone — it is a snapshot.",
        "  db.save()              # rewrite opened path (view = file_view)",
        "  db.save(r'out.room', 'symbol')",
        "  db.to_gds / to_oas / to_qucs / to_xschem(...)",
        "",
        "MCP tools: room_python_eval (full API), room_api_help, room_api_dir,",
        "           room_info, room_ls, room_sniff, room_convert",
        "Note: copying a view into another library cell folder is a DOOR operation",
        "(copy_selected_view), not a single Database method.",
    ]
    return "\n".join(lines)


def _eval_namespace() -> dict[str, Any]:
    """Same public surface as ``import room`` plus short aliases."""
    ns: dict[str, Any] = {
        "__name__": "__room_mcp__",
        "room": room_pkg,
        "json": json,
        "Path": __import__("pathlib").Path,
    }
    # Re-export package __all__ into the eval globals (open, create, Database, …).
    for name in getattr(room_pkg, "__all__", []):
        if hasattr(room_pkg, name):
            ns[name] = getattr(room_pkg, name)
    # Explicit extras that agents often expect at top level.
    ns.update(
        {
            "Database": room_api.Database,
            "Cell": room_api.Cell,
            "CellContent": room_api.CellContent,
            "ViewType": room_api.ViewType,
            "ShapeType": room_api.ShapeType,
            "Orient": room_api.Orient,
            "LayerPurpose": room_api.LayerPurpose,
            "Pin": room_api.Pin,
            "Instance": room_api.Instance,
            "Box": room_api.Box,
            "Rect": room_api.Rect,
            "Layer": room_api.Layer,
            "parse_view": room_api.parse_view,
        }
    )
    return ns


def _tool_defs() -> list[dict[str, Any]]:
    empty = {"type": "object", "properties": {}}
    path_prop = {
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to a .room (Cap’n Proto or room-json)"}
        },
        "required": ["path"],
    }
    return [
        {
            "name": "room_python_eval",
            "description": (
                "FULL ROOM Python API access. Exec Python with `import room` semantics "
                "(open/create/from_*, Database/Cell/CellContent, pins/instances/shapes, "
                "save/to_gds/…). Assign `result = …` to return data. Prefer this for any "
                "ROOM question that is not covered by a thin helper tool."
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "code": {
                        "type": "string",
                        "description": "Python 3 source; use print or set result=",
                    },
                },
                "required": ["code"],
            },
        },
        {
            "name": "room_api_help",
            "description": "Complete ROOM Python API cheat-sheet (methods on Database/Cell/CellContent).",
            "inputSchema": empty,
        },
        {
            "name": "room_api_dir",
            "description": (
                "Introspect a live object path, e.g. object='Database' or "
                "object='CellContent' or object='room' — returns dir()/signatures."
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "object": {
                        "type": "string",
                        "description": "Name in eval namespace: room, Database, Cell, CellContent, …",
                    },
                },
                "required": ["object"],
            },
        },
        {
            "name": "room_info",
            "description": "Shortcut: Database.open(path).info()",
            "inputSchema": path_prop,
        },
        {
            "name": "room_ls",
            "description": "Shortcut: list cell names (and optional layers).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "path": {"type": "string"},
                    "layers": {"type": "boolean"},
                },
                "required": ["path"],
            },
        },
        {
            "name": "room_sniff",
            "description": "Shortcut: room.sniff(path) header metadata.",
            "inputSchema": path_prop,
        },
        {
            "name": "room_convert",
            "description": "Shortcut: python -m room convert …",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "source": {"type": "string"},
                    "dest": {"type": "string"},
                    "view": {"type": "string"},
                    "format": {"type": "string"},
                    "tool": {"type": "string"},
                    "cell": {"type": "string"},
                },
                "required": ["source", "dest"],
            },
        },
    ]


def _dir_object(name: str) -> str:
    ns = _eval_namespace()
    if name not in ns:
        known = ", ".join(sorted(k for k in ns if not k.startswith("_")))
        return f"Unknown object {name!r}. Known: {known}"
    obj = ns[name]
    lines = [f"{name}: {obj!r}", ""]
    for attr in sorted(a for a in dir(obj) if not a.startswith("_")):
        try:
            member = getattr(obj, attr)
        except Exception as ex:  # noqa: BLE001
            lines.append(f"  {attr}: <error: {ex}>")
            continue
        if callable(member):
            try:
                sig = str(inspect.signature(member))
            except (TypeError, ValueError):
                sig = "(…)"
            doc = (inspect.getdoc(member) or "").strip().split("\n", 1)[0]
            lines.append(f"  {attr}{sig}" + (f"  # {doc}" if doc else ""))
        else:
            lines.append(f"  {attr} = {member!r}")
    return "\n".join(lines)


def _call_tool(name: str, arguments: dict[str, Any] | None) -> dict[str, Any]:
    args = arguments or {}
    try:
        if name == "room_api_help":
            return _ok_text(_api_help_text())

        if name == "room_api_dir":
            obj = str(args.get("object", "")).strip() or "room"
            return _ok_text(_dir_object(obj))

        if name == "room_info":
            path = str(args.get("path", "")).strip()
            if not path:
                return _err_text("path is required")
            with room_api.Database.open(path) as db:
                data = db.info()
            return _ok_text(json.dumps(data, indent=2, default=str))

        if name == "room_ls":
            path = str(args.get("path", "")).strip()
            if not path:
                return _err_text("path is required")
            with room_api.Database.open(path) as db:
                out: dict[str, Any] = {"cells": db.cell_names()}
                if args.get("layers"):
                    out["layers"] = [
                        {
                            "layer": layer.layer_num,
                            "datatype": layer.data_type,
                            "name": layer.name,
                            "purpose": layer.purpose.name.lower(),
                        }
                        for layer in db.layers()
                    ]
            return _ok_text(json.dumps(out, indent=2))

        if name == "room_sniff":
            path = str(args.get("path", "")).strip()
            if not path:
                return _err_text("path is required")
            info = room_api.sniff(path)
            data = {
                "version": info.version,
                "generator": info.generator,
                "technology": info.technology,
                "lib_name": info.lib_name,
                "view": info.view.name.lower(),
                "cell_count": info.cell_count,
                "primary_cell": info.primary_cell,
            }
            return _ok_text(json.dumps(data, indent=2))

        if name == "room_python_eval":
            code = str(args.get("code", ""))
            if not code.strip():
                return _err_text("code is required")
            ns = _eval_namespace()
            exec(compile(code, "<room_python_eval>", "exec"), ns, ns)  # noqa: S102
            if "result" in ns:
                return _ok_text(json.dumps({"result": ns["result"]}, indent=2, default=str))
            return _ok_text("ok")

        if name == "room_convert":
            source = str(args.get("source", "")).strip()
            dest = str(args.get("dest", "")).strip()
            if not source or not dest:
                return _err_text("source and dest are required")
            argv = ["convert", source, dest]
            for key, flag in (
                ("view", "--view"),
                ("format", "--format"),
                ("tool", "--tool"),
                ("cell", "--cell"),
            ):
                val = str(args.get(key, "") or "").strip()
                if val:
                    argv.extend([flag, val])
            rc = cli_main(argv)
            if rc != 0:
                return _err_text(f"convert failed with exit code {rc}")
            return _ok_text(json.dumps({"ok": True, "source": source, "dest": dest}))

        return _err_text(f"Unknown tool: {name}")
    except Exception as exc:  # noqa: BLE001
        return _err_text(f"{exc}\n{traceback.format_exc()}")


def _reply(msg_id: Any, *, result: Any = None, error: dict[str, Any] | None = None) -> None:
    payload: dict[str, Any] = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        payload["error"] = error
    else:
        payload["result"] = result
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def _handle(msg: dict[str, Any]) -> None:
    method = msg.get("method")
    msg_id = msg.get("id")
    params = msg.get("params") or {}

    if msg_id is None:
        return

    if method == "initialize":
        _reply(
            msg_id,
            result={
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": SERVER_INFO,
            },
        )
        return

    if method == "tools/list":
        _reply(msg_id, result={"tools": _tool_defs()})
        return

    if method == "tools/call":
        name = str(params.get("name", ""))
        arguments = params.get("arguments") or {}
        if not isinstance(arguments, dict):
            arguments = {}
        _reply(msg_id, result=_call_tool(name, arguments))
        return

    if method == "ping":
        _reply(msg_id, result={})
        return

    _reply(msg_id, error={"code": -32601, "message": f"Method not found: {method}"})


def main(argv: list[str] | None = None) -> int:
    _ = argv
    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as exc:
            sys.stderr.write(f"room mcp: bad json: {exc}\n")
            sys.stderr.flush()
            continue
        if not isinstance(msg, dict):
            continue
        _handle(msg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
