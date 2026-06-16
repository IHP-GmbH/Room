# KLayout integration — read/write `.core` directly

CORE is compiled **into** a KLayout `db_plugins/mcore.dll` streamer (no separate `libcore` link). Cap'n Proto comes from KLayout's LStream runtime (`xcapnp` / `xkj`) or from `third_party/capnp-install` when built standalone.

## Why `mcore`?

`mcore` is the **internal KLayout streamer id** (folder name, `TARGET`, DLL name: `mcore.dll` / `mcore_ui.dll`). The user-visible format is still **CORE** (`*.core`, title “CommonDB CORE” in the file dialog).

The name was chosen because KLayout builds streamers in **alphabetical SUBDIRS order**: `mcore` sorts **after** `lstream`, so `xcapnp` / `xkj` are already linked when `mcore.dll` is built. Renaming to `core` would require checking that build order still works.

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
   scripts\setup_klayout_mcore.cmd C:\path\to\KLayout
   ```

   Or manually on Windows:

   ```bat
   mklink /J C:\path\to\KLayout\src\plugins\streamers\mcore ^
           %CD%\integrations\klayout\mcore
   ```

   On Linux/macOS:

   ```bash
   ln -s "$(pwd)/integrations/klayout/mcore" /path/to/KLayout/src/plugins/streamers/mcore
   ```

3. **Patch `streamers.pro`** in the KLayout tree (or apply `integrations/klayout/streamers.pro.patch`):

   ```qmake
   SUBDIRS += mcore
   ```

4. **Configure paths** (if you did not use the setup script):

   ```bat
   copy integrations\klayout\mcore\db_plugin\local.pri.example ^
        integrations\klayout\mcore\db_plugin\local.pri
   ```

   Edit `local.pri`: set `COMMONDB_ROOT` (this repo) and `KLAYOUT_SRC` (`…/KLayout/src`).

5. **Rebuild KLayout** (LStream must be enabled — default):

   ```bat
   cd C:\path\to\KLayout
   build.bat -j 4
   ```

   Folder `mcore` sorts after `lstream`, so `xcapnp`/`xkj` exist before `mcore.dll` links.

## Try it

**Until `mcore.dll` is built**, open `.core` via the GDS bridge:

```bat
scripts\open_core_in_klayout.cmd examples\gds_to_core\output\sample.core
```

Or in KLayout: **Macros → Load CORE (.core) via core_to_gds bridge** (install `scripts/klayout_load_core.lym` into KLayout macros). Set `COMMONDB_ROOT` to your checkout if the macro cannot find `core_to_gds`.

Direct `.core` open (after rebuild):

```bat
C:\path\to\KLayout\bin-release\klayout.exe examples\gds_to_core\output\sample.core
```

KLayout smoke tests (from repo root, with `klayout -b` on `PATH`):

```bat
set COMMONDB_ROOT=%CD%
klayout -b -r scripts\test_core_load.rb
klayout -b -r scripts\test_core_roundtrip.rb
```

## Files

| Path | Role |
|------|------|
| `integrations/klayout/mcore/core.pri` | Lists CommonDB `src/*.cpp` + generated capnp |
| `integrations/klayout/mcore/db_plugin/coreReader.cc` | `Database::loadFromFile` → `db::Layout` |
| `integrations/klayout/mcore/db_plugin/coreWriter.cc` | `db::Layout` → `Database::saveToFile` (compact) |
| `integrations/klayout/generated/` | Pre-generated capnp C++ (committed) |
| `integrations/klayout/streamers.pro.patch` | One-line KLayout build integration |

## Limits (POC)

- Layout view only (no schematic in KLayout)
- Library name: read from CORE `Lib.name` → KLayout meta `libname`; on save uses `SaveLayoutOptions.libname`, else meta `libname`, else output filename stem, else `default`
- Lib-level properties: `Lib.properties` ↔ `layout.prop_id()` (GDS file-level props, OAS strict headers, etc.)
- No format-specific save options yet (compact geometry always)
