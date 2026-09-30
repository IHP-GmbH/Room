"""setuptools hooks: build room_c before packaging."""

from __future__ import annotations

import importlib.util
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.sdist import sdist as _sdist

try:
    from setuptools.command.editable_wheel import editable_wheel as _editable_wheel
except ImportError:  # older setuptools
    _editable_wheel = None  # type: ignore[misc, assignment]


def _load_build_native():
    path = Path(__file__).resolve().parent / "_build_native.py"
    spec = importlib.util.spec_from_file_location("room_build_native", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _ensure_native() -> None:
    _load_build_native().ensure_native_in_package()


class build_py(_build_py):
    def run(self) -> None:
        _ensure_native()
        super().run()


_cmdclass = {
    "build_py": build_py,
    "sdist": _sdist,
}

if _editable_wheel is not None:

    class editable_wheel(_editable_wheel):  # type: ignore[valid-type, misc]
        def run(self) -> None:
            _ensure_native()
            super().run()

    _cmdclass["editable_wheel"] = editable_wheel


setup(cmdclass=_cmdclass)
