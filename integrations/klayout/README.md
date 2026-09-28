# KLayout integration — read/write `.room` directly

ROOM is compiled **into** a KLayout `db_plugins/mroom.dll` streamer (no separate `libroom` link). Cap'n Proto comes from KLayout's LStream runtime (`xcapnp` / `xkj`) or from `third_party/capnp-install` when built standalone.

## Why `mroom`?

`mroom` is the **internal KLayout streamer id** (folder name, `TARGET`, DLL name: `mroom.dll` / `mroom_ui.dll`). The user-visible format is still **ROOM** (`*.room`, title “CommonDB ROOM” in the file dialog).

The name was chosen because KLayout builds streamers in **alphabetical SUBDIRS order**: `mroom` sorts **after** `lstream`, so `xcapnp` / `xkj` are already linked when `mroom.dll` is built. Renaming to `core` would require checking that build order still works.

## Setup (once)

Run commands from the **CommonDB repository root** unless noted otherwise.

1. **Generate Cap'n Proto C++** (when schema changes):

   ```bat
   cd schema
   ..\third_party\capnp-install\bin\capnp.exe compile ^
     -I..\third_party\capnp-install\include -I. ^
     -o..\third_party\capnp-install\bin\capnpc-c++.exe:..\integrations\klayout\generated ^
     common.capnp dm.capnp design.capnp database.capnp views.capnp index.capnp compact.capnp
   ```

2. **Link streamer into KLayout tree**:

   ```bat
   scripts\setup_klayout_mroom.cmd C:\path\to\KLayout
   ```

   Or manually on Windows:

   ```bat
   mklink /J C:\path\to\KLayout\src\plugins\streamers\mroom ^
           %CD%\integrations\klayout\mroom
   ```

   On Linux/macOS:

   ```bash
   ln -s "$(pwd)/integrations/klayout/mroom" /path/to/KLayout/src/plugins/streamers/mroom
   ```

3. **Patch `streamers.pro`** in the KLayout tree (or apply `integrations/klayout/streamers.pro.patch`):

   ```qmake
   SUBDIRS += mroom
   ```

4. **Configure paths** (if you did not use the setup script):

   ```bat
   copy integrations\klayout\mroom\db_plugin\local.pri.example ^
        integrations\klayout\mroom\db_plugin\local.pri
   ```

   Edit `local.pri`: set `COMMONDB_ROOT` (this repo) and `KLAYOUT_SRC` (`…/KLayout/src`).

5. **Rebuild KLayout** (LStream must be enabled — default):

   ```bat
   cd C:\path\to\KLayout
   build.bat -j 4
   ```

   Folder `mroom` sorts after `lstream`, so `xcapnp`/`xkj` exist before `mroom.dll` links.

## Try it

**Until `mroom.dll` is built**, open `.room` via the GDS bridge:

```bat
scripts\open_room_in_klayout.cmd examples\gds_to_room\output\sample.room
```

Or in KLayout: **Macros → Load ROOM (.room) via room_to_gds bridge** (install `scripts/klayout_load_room.lym` into KLayout macros). Set `COMMONDB_ROOT` to your checkout if the macro cannot find `room_to_gds`.

Direct `.room` open (after rebuild):

```bat
C:\path\to\KLayout\bin-release\klayout.exe examples\gds_to_room\output\sample.room
```

KLayout smoke tests (from repo root, with `klayout -b` on `PATH`):

```bat
set COMMONDB_ROOT=%CD%
klayout -b -r scripts\test_room_load.rb
klayout -b -r scripts\test_room_roundtrip.rb
```

## Files

| Path | Role |
|------|------|
| `integrations/klayout/mroom/core.pri` | Lists CommonDB `src/*.cpp` + generated capnp |
| `integrations/klayout/mroom/db_plugin/roomReader.cc` | `Database::loadFromFile` → `db::Layout` |
| `integrations/klayout/mroom/db_plugin/roomWriter.cc` | `db::Layout` → `Database::saveToFile` (compact) |
| `integrations/klayout/generated/` | Pre-generated capnp C++ (committed) |
| `integrations/klayout/streamers.pro.patch` | One-line KLayout build integration |

## Limits (POC)

- Layout view only (no schematic in KLayout)
- Library name: read from ROOM `Lib.name` → KLayout meta `libname`; on save uses `SaveLayoutOptions.libname`, else meta `libname`, else output filename stem, else `default`
- Lib-level properties: `Lib.properties` ↔ `layout.prop_id()` (GDS file-level props, OAS strict headers, etc.)
- No format-specific save options yet (compact geometry always)
