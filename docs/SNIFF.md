# Sniffing `.core` files

**Sniff** reads only the Cap'n Proto header of a `.core` file — no geometry payloads are decoded. LibMan, project browsers, and CI use this to pick the right tool and view without loading the full database.

Full API reference: [`docs/html/filesummary.html`](html/filesummary.html).

## API

```cpp
#include "file_summary.h"

core::CoreFileInfo info = core::sniffCoreFile("cell.schematic.core");
// info.version, info.generator, info.technology, info.libName
// info.summary.view      → layout | schematic | symbol | abstract
// info.summary.cellCount
// info.summary.primaryCell
```

| Type / function | Library | Purpose |
|-----------------|---------|---------|
| `core::FileSummary` | `CORE::core` | Denormalized view + cell inventory |
| `core::CoreFileInfo` | `CORE::core` | Header fields + `FileSummary` |
| `sniffCoreFile(path)` | `CORE::core` | Read metadata from disk |
| `FileSummary::fromLib()` | `CORE::core` | Build summary when saving |
| `Database::fileSummary()` | `CORE::core` | Summary attached after load/save |

On save, `Database::saveToFile(path, fileView)` writes `Database.summary` (Cap'n Proto `FileSummary`) so sniff is O(header) even for large layout files.

## CLI / test

After build:

```bash
cmake --build build --target core_file_sniff
./build/core_file_sniff examples/xschem_to_core/data/test.sch build/tests/sniff_fixture.schematic.core
```

The test imports an Xschem `.sch`, saves a `.core`, sniffs it, and checks `summary.view == Schematic`.

## When to use sniff vs full load

| Task | Use |
|------|-----|
| Tool routing (KLayout vs Xschem) | `sniffCoreFile` |
| Cell list in file browser | `sniffCoreFile` or `xschem_bridge::listCells` after full load |
| Edit geometry | `Database::loadFromFile` |
| Import from GDS/Xschem | Importer → `saveToFile` |

## Related

- [CORE file naming](CORE_FILE_NAMING.md) — `cell.schematic.core` convention
- [`core_paths.h`](../src/core_paths.h) — parse view from filename
