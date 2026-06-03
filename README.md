<p align="center">
  <img src="docs/html/logo.svg" alt="CommonDB logo" width="120" height="120">
</p>

<h1 align="center">CommonDB</h1>

<p align="center">
  <strong>Open-source IC layout and schematic database</strong><br>
  Binary <code>.cdb</code> files · Cap'n Proto · C++17
</p>

---

**CommonDB** (`cdb::`) is a teaching and application API for storing chip design topology: libraries, cells, layers, shapes, instances, and nets. Layout and schematic data share one model; the view kind (`layout`, `schematic`, `symbol`, `abstract`) lives in `CellContent`.

The project is part of the **IHP** open-source IC design flow. Typical inputs are **GDSII** (e.g. IHP sg13g2 standard cells) or **Qucs** `.sch` schematics.

## Data model

```
Database
 └── Lib
      └── Cell[]
           └── CellContent[]    ← viewType, dbuPerMicron, layers
                └── Block
                     ├── Shape[]
                     ├── Instance[]
                     └── Net[] / Term[]
```

## Requirements

| Tool | Notes |
|------|--------|
| **CMake** ≥ 3.16 | Configure and build |
| **C++17 compiler** | MinGW-w64 (Windows), GCC or Clang (Linux) |
| **Git** | Clones Cap'n Proto on first configure (if needed) |
| **Cap'n Proto** | Built automatically into `third_party/capnp-install/` by CMake |

On Windows, the repo’s VS Code tasks assume **Qt MinGW 8.1** (`C:/Qt/Tools/mingw810_64`). Adjust compiler paths if you use another toolchain.

## Build

On the **first** `cmake` configure, CommonDB builds Cap'n Proto into `third_party/capnp-install/` automatically if it is not already there (option `COMMONDB_BOOTSTRAP_CAPNP=ON`, default). That step can take several minutes; later configures are instant.

To build Cap'n Proto by hand (optional), see [third_party/README.md](third_party/README.md).

### Configure and compile

**Windows (command line, MinGW Makefiles):**

```bat
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw810_64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw810_64/bin/g++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/mingw810_64/bin/mingw32-make.exe

cmake --build build -j4
```

**Linux:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### VS Code

Open the folder in VS Code (CMake Tools extension recommended):

1. **CMake: Configure Debug** — configures CommonDB and bootstraps Cap'n Proto if needed  
2. **Build: Static Libraries (.a)** — `cdb_core`, `cdb_utils`  
3. **Build: All Targets** — adds `gds_to_cdb`, `qucs_to_cdb`, `make_sample_gds`  
4. **Example: GDS to CommonDB** / **Example: Qucs to CommonDB** — run demos after a full build  

Default build task: **Build: Static Libraries (.a)** (`Ctrl+Shift+B` may need to be bound to that task).

## Build outputs

| Target | Type | Description |
|--------|------|-------------|
| `cdb_core` | static library | Database API + Cap'n Proto serialization |
| `cdb_utils` | static library | GDS / Qucs importers, text dump |
| `gds_to_cdb` | executable | GDSII → `.cdb` |
| `qucs_to_cdb` | executable | Qucs `.sch` → `.cdb` (round-trip export) |
| `make_sample_gds` | executable | Minimal sample GDS for tests |

Install headers and libraries into a prefix:

```bat
cmake --install build --prefix install
```

Use `find_package(CommonDB)` from another CMake project via `install/lib/cmake/CommonDB/`.

## Examples

### GDS layout import

Bundled demo (no arguments — uses `examples/gds_to_cdb/data/sg13g2_stdcell.gds`):

```bat
build\gds_to_cdb.exe
```

Custom paths:

```bat
build\gds_to_cdb.exe input.gds output\layout.cdb output\layout.txt CELL_NAME
```

### Qucs schematic import

```bat
build\qucs_to_cdb.exe examples\qucs_to_cdb\data\rc_lowpass.sch examples\qucs_to_cdb\output\schematic.cdb
```

### Minimal GDS

```bat
build\make_sample_gds.exe testdata\sample.gds
build\gds_to_cdb.exe testdata\sample.gds output\sample.cdb
```

## Documentation

HTML API reference lives in [`docs/html/`](docs/html/) (start at [`index.html`](docs/html/index.html)).

| How to view | Notes |
|-------------|--------|
| **Local** | Clone the repo and open `docs/html/index.html` in a browser. |
| **GitHub Pages** | Only if the repo is **public**, or the org has **GitHub Team / Enterprise** (private Pages). The workflow publishes to the [`gh-pages`](https://github.com/IHP-GmbH/CommonDB/tree/gh-pages) branch. |

**This repository is private.** On GitHub Free, Pages is not available for private repos — `Settings → Pages` may show **404**, and https://ihp-gmbh.github.io/CommonDB/ will not work until an org admin enables Pages (paid plan) or makes the repo public.

Workflow: [`.github/workflows/pages.yml`](.github/workflows/pages.yml) updates `gh-pages` on each push to `main`. When Pages is allowed: **Settings → Pages → Deploy from branch → `gh-pages` / (root)**.

## Project layout

| Path | Purpose |
|------|---------|
| `src/` | Core C++ API (`Database`, `Cell`, `Shape`, …) |
| `schema/` | Cap'n Proto schemas (`.capnp`) |
| `utils/` | `GdsImporter`, `QucsImporter`, `QucsExporter`, `TextDumper` |
| `examples/gds_to_cdb/` | GDS import example + sample data |
| `examples/qucs_to_cdb/` | Qucs import example + sample `.sch` |
| `third_party/capnp-install/` | Cap'n Proto compiler and libraries |
| `scripts/mkcapnp.cmd` | Build Cap'n Proto on Windows |
| `docs/html/` | Documentation and `logo.svg` |
| `LICENSE` | GNU GPL v3.0 |

## License

CommonDB is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE) for the full text.
