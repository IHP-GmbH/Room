# ROOM Python bindings
#
# ## Install (pip)
#
# From the repo root (builds `room_c` via CMake on first install if needed):
#
#   pip install ./python
#   # or editable:
#   pip install -e ./python
#
# On Windows use 64-bit MinGW on PATH (default `C:\msys64\mingw64\bin`), matching
# 64-bit CPython. Optional env vars: `MINGW_BIN`, `ROOM_C_DLL`, `CMAKE`.
#
# Then:
#
#   room info design.room
#   python -c "import room; print(room.create().lib_name)"
#
# ## Manual CMake build + tests
#
#   cmake -S . -B build-python -G "MinGW Makefiles" -DROOM_BUILD_PYTHON=ON
#   cmake --build build-python --target room_c -j
#   scripts\run_python_tests.cmd
#
#   set ROOM_C_DLL=%CD%\build-python\python\libroom_c.dll
#   set PYTHONPATH=%CD%\python
#   set MINGW_BIN=C:\msys64\mingw64\bin
#   python -m pytest python/tests -q
#
# ## API
#
#   import room
#   db = room.open("design.room")
#   print(db.cell_names())
#
#   python -m room info design.room
#   python -m room ls design.room
#   room convert chip.gds chip.room
#   room convert --tool qucs rc.sch rc.room --view schematic
#   room convert --tool xschem inv.sch inv.room --view schematic
#
# HTML docs (same site as C++): https://ihp-gmbh.github.io/Room/python.html
#
# PyPI wheels are not published yet — install from this repository.
