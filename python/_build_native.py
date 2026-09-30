"""Build/copy room_c native library into the room package for pip install."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
PKG = ROOT / "room"
BUILD = REPO / "build-python"

NATIVE_NAMES = (
    "libroom_c.dll",
    "room_c.dll",
    "libroom_c.so",
    "room_c.so",
    "libroom_c.dylib",
    "room_c.dylib",
)


def _find_existing() -> Path | None:
    env = os.environ.get("ROOM_C_DLL")
    if env and Path(env).is_file():
        return Path(env)
    for folder in (PKG, BUILD / "python", REPO / "build" / "python"):
        for name in NATIVE_NAMES:
            path = folder / name
            if path.is_file():
                return path
    return None


def _cmake() -> str:
    for candidate in (
        os.environ.get("CMAKE"),
        shutil.which("cmake"),
        r"C:\Qt\Tools\CMake_64\bin\cmake.exe",
        r"C:\Program Files\CMake\bin\cmake.exe",
    ):
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("cmake not found on PATH (needed to build room_c)")


def _configure_and_build() -> Path:
    cmake = _cmake()
    BUILD.mkdir(parents=True, exist_ok=True)

    cfg = [
        cmake,
        "-S",
        str(REPO),
        "-B",
        str(BUILD),
        f"-DCMAKE_BUILD_TYPE={os.environ.get('CMAKE_BUILD_TYPE', 'Release')}",
        "-DROOM_BUILD_PYTHON=ON",
        "-DROOM_BUILD_EXAMPLES=OFF",
        "-DROOM_BUILD_TESTS=OFF",
    ]

    if sys.platform == "win32":
        mingw = Path(os.environ.get("MINGW_BIN", r"C:\msys64\mingw64\bin"))
        gpp = mingw / "g++.exe"
        make = mingw / "mingw32-make.exe"
        if gpp.is_file() and make.is_file():
            cfg.extend(
                [
                    "-G",
                    "MinGW Makefiles",
                    f"-DCMAKE_CXX_COMPILER={gpp.as_posix()}",
                    f"-DCMAKE_MAKE_PROGRAM={make.as_posix()}",
                ]
            )
            os.environ["PATH"] = str(mingw) + os.pathsep + os.environ.get("PATH", "")
            os.environ.setdefault("MINGW_BIN", str(mingw))

    print("Configuring ROOM Python native library...", flush=True)
    subprocess.check_call(cfg)
    print("Building room_c...", flush=True)
    subprocess.check_call([cmake, "--build", str(BUILD), "--target", "room_c", "-j"])

    found = _find_existing()
    if not found:
        raise RuntimeError("room_c built but shared library not found under build-python/python/")
    return found


def ensure_native_in_package() -> Path:
    """Ensure a room_c shared library is present inside python/room/ for packaging."""
    src = _find_existing()
    if src is None or not str(src.resolve()).startswith(str(PKG.resolve())):
        if src is None:
            src = _configure_and_build()
        dest = PKG / src.name
        if src.resolve() != dest.resolve():
            shutil.copy2(src, dest)
            print(f"Copied {src.name} -> {dest}", flush=True)
        return dest
    return src


if __name__ == "__main__":
    path = ensure_native_in_package()
    print(path)
