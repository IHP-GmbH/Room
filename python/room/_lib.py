"""ctypes loader for the ROOM C API shared library."""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    POINTER,
    Structure,
    c_char_p,
    c_double,
    c_int,
    c_int64,
    c_uint16,
    c_uint32,
    c_void_p,
    create_string_buffer,
)
from pathlib import Path


class RoomFileInfoC(Structure):
    _fields_ = [
        ("version", ctypes.c_char * 64),
        ("generator", ctypes.c_char * 256),
        ("technology", ctypes.c_char * 256),
        ("lib_name", ctypes.c_char * 256),
        ("view", c_int),
        ("cell_count", c_uint32),
        ("primary_cell", ctypes.c_char * 256),
    ]


class RoomBoxC(Structure):
    _fields_ = [
        ("llx", c_int64),
        ("lly", c_int64),
        ("urx", c_int64),
        ("ury", c_int64),
        ("empty", c_int),
    ]


class RoomRectC(Structure):
    _fields_ = [
        ("layer_id", c_uint32),
        ("llx", c_int64),
        ("lly", c_int64),
        ("urx", c_int64),
        ("ury", c_int64),
    ]


class RoomInstanceC(Structure):
    _fields_ = [
        ("cell_name", ctypes.c_char * 256),
        ("x", c_int64),
        ("y", c_int64),
        ("orient", c_int),
        ("mag", c_double),
    ]


class RoomLayerC(Structure):
    _fields_ = [
        ("layer_num", c_uint16),
        ("data_type", c_uint16),
        ("name", ctypes.c_char * 128),
        ("purpose", c_int),
    ]


def _candidate_dll_paths() -> list[Path]:
    env = os.environ.get("ROOM_C_DLL")
    paths: list[Path] = []
    if env:
        paths.append(Path(env))

    here = Path(__file__).resolve().parent
    # Prefer native lib shipped inside the installed/editable package.
    roots = [
        here,
        here.parent,
        here.parent.parent / "build-python" / "python",
        here.parent.parent / "build" / "python",
        Path.cwd(),
        Path.cwd() / "python",
    ]
    names = [
        "room_c.dll",
        "libroom_c.dll",
        "libroom_c.so",
        "room_c.so",
        "libroom_c.dylib",
        "room_c.dylib",
    ]
    for root in roots:
        for name in names:
            paths.append(root / name)
            paths.append(root / "python" / name)
    return paths


def _prepare_windows_dll_search(native_dir: Path) -> None:
    """Prefer the package dir (bundled MinGW runtimes), then MinGW installs."""
    dirs: list[Path] = [native_dir]
    env = os.environ.get("MINGW_BIN")
    if env:
        dirs.append(Path(env))
    dirs.extend(
        [
            Path(r"C:\msys64\mingw64\bin"),
            Path(r"C:\msys64\ucrt64\bin"),
            Path(r"C:\msys64\clang64\bin"),
        ]
    )
    # Avoid shutil.which("g++") here: in Git Bash/MSYS it can point at a
    # toolchain whose zlib conflicts with the MinGW-built room_c DLL.

    seen: set[str] = set()
    path_prefix: list[str] = []
    for folder in dirs:
        if not folder.is_dir():
            continue
        key = str(folder.resolve()).lower()
        if key in seen:
            continue
        seen.add(key)
        try:
            os.add_dll_directory(str(folder))
        except (OSError, AttributeError):
            pass
        path_prefix.append(str(folder))

    if path_prefix:
        # Put known-good dirs first so MSYS /usr/bin zlib cannot win.
        os.environ["PATH"] = os.pathsep.join(path_prefix + [os.environ.get("PATH", "")])


def _load_library() -> ctypes.CDLL:
    last_error: Exception | None = None
    # Search the DLL's own directory for dependencies (bundled MinGW runtimes).
    winmode = None
    if sys.platform == "win32":
        load_dll_dir = getattr(os, "add_dll_directory", None) is not None
        # LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
        winmode = 0x00000100 | 0x00001000 if load_dll_dir else None

    for path in _candidate_dll_paths():
        if not path.is_file():
            continue
        try:
            if sys.platform == "win32":
                _prepare_windows_dll_search(path.parent)
                if winmode is not None:
                    return ctypes.CDLL(str(path), winmode=winmode)
            return ctypes.CDLL(str(path))
        except OSError as exc:
            last_error = exc
            continue

    searched = "\n  ".join(str(p) for p in _candidate_dll_paths()[:12])
    raise RuntimeError(
        "Could not load room_c shared library. Build with -DROOM_BUILD_PYTHON=ON "
        f"and set ROOM_C_DLL if needed.\nTried:\n  {searched}"
        + (f"\nLast error: {last_error}" if last_error else "")
        + "\nOn Windows, ensure MinGW64 runtime is available "
        r"(e.g. C:\msys64\mingw64\bin on PATH, or set MINGW_BIN), "
        "or reinstall with: python -m pip install --force-reinstall ./python"
    )


def _bind(lib: ctypes.CDLL) -> ctypes.CDLL:
    lib.room_db_create.restype = c_void_p
    lib.room_db_open.argtypes = [c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_db_open.restype = c_void_p
    lib.room_db_save.argtypes = [c_void_p, c_char_p, c_int, c_char_p, ctypes.c_size_t]
    lib.room_db_save.restype = c_int
    lib.room_db_free.argtypes = [c_void_p]
    lib.room_db_free.restype = None

    for name in ("room_db_version", "room_db_generator", "room_db_technology", "room_db_lib_name",
                 "room_db_summary_primary_cell"):
        fn = getattr(lib, name)
        fn.argtypes = [c_void_p]
        fn.restype = c_char_p

    lib.room_db_file_view.argtypes = [c_void_p]
    lib.room_db_file_view.restype = c_int
    lib.room_db_summary_view.argtypes = [c_void_p]
    lib.room_db_summary_view.restype = c_int
    lib.room_db_summary_cell_count.argtypes = [c_void_p]
    lib.room_db_summary_cell_count.restype = c_uint32

    for name in ("room_db_set_version", "room_db_set_generator", "room_db_set_technology",
                 "room_db_set_lib_name"):
        fn = getattr(lib, name)
        fn.argtypes = [c_void_p, c_char_p]
        fn.restype = None

    lib.room_db_set_file_view.argtypes = [c_void_p, c_int]
    lib.room_db_set_file_view.restype = None

    lib.room_sniff.argtypes = [c_char_p, POINTER(RoomFileInfoC), c_char_p, ctypes.c_size_t]
    lib.room_sniff.restype = c_int

    lib.room_db_layer_count.argtypes = [c_void_p]
    lib.room_db_layer_count.restype = c_int
    lib.room_db_layer_get.argtypes = [c_void_p, c_int, POINTER(RoomLayerC)]
    lib.room_db_layer_get.restype = c_int
    lib.room_db_layer_add.argtypes = [c_void_p, c_uint16, c_uint16, c_char_p, c_int]
    lib.room_db_layer_add.restype = c_int

    lib.room_db_cell_count.argtypes = [c_void_p]
    lib.room_db_cell_count.restype = c_int
    lib.room_db_cell_name.argtypes = [c_void_p, c_int]
    lib.room_db_cell_name.restype = c_char_p
    lib.room_db_find_cell.argtypes = [c_void_p, c_char_p]
    lib.room_db_find_cell.restype = c_int
    lib.room_db_add_cell.argtypes = [c_void_p, c_char_p]
    lib.room_db_add_cell.restype = c_int

    lib.room_db_cell_content_count.argtypes = [c_void_p, c_int]
    lib.room_db_cell_content_count.restype = c_int
    lib.room_db_cell_content_view.argtypes = [c_void_p, c_int, c_int]
    lib.room_db_cell_content_view.restype = c_int
    lib.room_db_cell_content_dbu.argtypes = [c_void_p, c_int, c_int]
    lib.room_db_cell_content_dbu.restype = c_double
    lib.room_db_cell_ensure_content.argtypes = [c_void_p, c_int, c_int, c_double]
    lib.room_db_cell_ensure_content.restype = c_int
    lib.room_db_cell_bbox.argtypes = [c_void_p, c_int, c_int, POINTER(RoomBoxC)]
    lib.room_db_cell_bbox.restype = c_int

    lib.room_db_shape_count.argtypes = [c_void_p, c_int, c_int]
    lib.room_db_shape_count.restype = c_int
    lib.room_db_shape_type.argtypes = [c_void_p, c_int, c_int, c_int]
    lib.room_db_shape_type.restype = c_int
    lib.room_db_shape_rect.argtypes = [c_void_p, c_int, c_int, c_int, POINTER(RoomRectC)]
    lib.room_db_shape_rect.restype = c_int
    lib.room_db_add_rect.argtypes = [c_void_p, c_int, c_int, c_uint32, c_int64, c_int64, c_int64, c_int64]
    lib.room_db_add_rect.restype = c_int

    lib.room_db_instance_count.argtypes = [c_void_p, c_int, c_int]
    lib.room_db_instance_count.restype = c_int
    lib.room_db_instance_get.argtypes = [c_void_p, c_int, c_int, c_int, POINTER(RoomInstanceC)]
    lib.room_db_instance_get.restype = c_int
    lib.room_db_add_instance.argtypes = [c_void_p, c_int, c_int, c_char_p, c_int64, c_int64, c_int, c_double]
    lib.room_db_add_instance.restype = c_int

    lib.room_from_gds.argtypes = [c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_from_gds.restype = c_void_p
    lib.room_to_gds.argtypes = [c_void_p, c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_to_gds.restype = c_int

    lib.room_from_qucs.argtypes = [c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_from_qucs.restype = c_void_p
    lib.room_to_qucs.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_to_qucs.restype = c_int

    lib.room_from_xschem.argtypes = [c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_from_xschem.restype = c_void_p
    lib.room_to_xschem.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_to_xschem.restype = c_int

    lib.room_has_oas.argtypes = []
    lib.room_has_oas.restype = c_int
    lib.room_from_oas.argtypes = [c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_from_oas.restype = c_void_p
    lib.room_to_oas.argtypes = [c_void_p, c_char_p, c_char_p, ctypes.c_size_t]
    lib.room_to_oas.restype = c_int
    return lib


_LIB: ctypes.CDLL | None = None


def get_lib() -> ctypes.CDLL:
    global _LIB
    if _LIB is None:
        _LIB = _bind(_load_library())
    return _LIB


def errbuf(size: int = 1024):
    return create_string_buffer(size)


def cstr(value: str | None) -> bytes | None:
    if value is None:
        return None
    return value.encode("utf-8")


def decode(ptr) -> str:
    if not ptr:
        return ""
    if isinstance(ptr, bytes):
        return ptr.decode("utf-8", errors="replace")
    return ctypes.cast(ptr, c_char_p).value.decode("utf-8", errors="replace")  # type: ignore[union-attr]
