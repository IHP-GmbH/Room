# Sniffing `.room` files

**Sniff** reads only the Cap'n Proto header of a `.room` file — no geometry payloads are decoded. LibMan, project browsers, and CI use this to pick the right tool and view without loading the full database.

Full API reference: [`docs/html/filesummary.html`](html/filesummary.html).

## API

```cpp
#include "file_summary.h"

room::RoomFileInfo info = room::sniffRoomFile("cell.schematic.room");
// info.version, info.generator, info.technology, info.libName
// info.summary.view      → layout | schematic | symbol | abstract
// info.summary.cellCount
// info.summary.primaryCell
```

| Type / function | Library | Purpose |
|-----------------|---------|---------|
| `room::FileSummary` | `ROOM::room` | Denormalized view + cell inventory |
| `room::RoomFileInfo` | `ROOM::room` | Header fields + `FileSummary` |
| `sniffRoomFile(path)` | `ROOM::room` | Read metadata from disk |
| `FileSummary::fromLib()` | `ROOM::room` | Build summary when saving |
| `Database::fileSummary()` | `ROOM::room` | Summary attached after load/save |

On save, `Database::saveToFile(path, fileView)` writes `Database.summary` (Cap'n Proto `FileSummary`) so sniff is O(header) even for large layout files.

## CLI / test

After build:

```bash
cmake --build build --target room_file_sniff
./build/room_file_sniff examples/xschem_to_room/data/test.sch build/tests/sniff_fixture.schematic.room
```

The test imports an Xschem `.sch`, saves a `.room`, sniffs it, and checks `summary.view == Schematic`.

## When to use sniff vs full load

| Task | Use |
|------|-----|
| Tool routing (KLayout vs Xschem) | `sniffRoomFile` |
| Cell list in file browser | `sniffRoomFile` or `xschem_bridge::listCells` after full load |
| Edit geometry | `Database::loadFromFile` |
| Import from GDS/Xschem | Importer → `saveToFile` |

## Related

- [ROOM file naming](ROOM_FILE_NAMING.md) — `cell.schematic.room` convention
- [`room_paths.h`](../src/room_paths.h) — parse view from filename
