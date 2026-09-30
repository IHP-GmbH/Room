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

# MinGW-built room_c needs these at load time. Bundle them next to the DLL so
# Windows Python works even when Git Bash/MSYS puts a conflicting zlib on PATH.
MINGW_RUNTIME_DLLS = (
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "zlib1.dll",
)


def _mingw_bin() -> Path | None:
    env = os.environ.get("MINGW_BIN")
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates.extend(
        [
            Path(r"C:\msys64\mingw64\bin"),
            Path(r"C:\msys64\ucrt64\bin"),
            Path(r"C:\msys64\clang64\bin"),
        ]
    )
    gpp = shutil.which("g++")
    if gpp:
        candidates.append(Path(gpp).resolve().parent)
    for folder in candidates:
        if (folder / "g++.exe").is_file():
            return folder
    return None


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
        mingw = _mingw_bin() or Path(os.environ.get("MINGW_BIN", r"C:\msys64\mingw64\bin"))
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


def _bundle_mingw_runtime(dest_dir: Path) -> None:
    if sys.platform != "win32":
        return
    mingw = _mingw_bin()
    if mingw is None:
        print("WARNING: MinGW bin not found; runtime DLLs not bundled", flush=True)
        return
    for name in MINGW_RUNTIME_DLLS:
        src = mingw / name
        if not src.is_file():
            print(f"WARNING: missing MinGW runtime {src}", flush=True)
            continue
        dest = dest_dir / name
        if dest.is_file() and dest.stat().st_size == src.stat().st_size:
            continue
        shutil.copy2(src, dest)
        print(f"Bundled {name} -> {dest}", flush=True)


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
        src = dest
    _bundle_mingw_runtime(PKG)
    return src


if __name__ == "__main__":
    path = ensure_native_in_package()
    print(path)
