# ROOM file naming

View-specific ROOM files use the pattern:

```text
<cell>.<view>.room
```

| View | Suffix | Example |
|------|--------|---------|
| Layout | `.layout.room` | `sg13g2_stdcell.layout.room` |
| Schematic | `.schematic.room` | `sg13g2_stdcell.schematic.room` |
| Symbol | `.symbol.room` | `inv.symbol.room` |
| Abstract | `.abstract.room` | `block.abstract.room` |

LibMan and the Xschem/KLayout integrations expect this layout. Legacy names like `schematic.room` (no cell prefix) still work for examples but are not recommended for libraries.

## API (`room_paths.h`)

```cpp
#include "room_paths.h"

room::roomFileName("inv", room::ViewType::Schematic);  // "inv.schematic.room"
room::parseRoomFilePath("path/inv.schematic.room");   // { cellName="inv", view=Schematic, valid=true }
room::isViewRoomFile(path, room::ViewType::Layout);
room::isRoomFilePath(path);                           // any *.room
room::roomFileGlob(room::ViewType::Schematic);        // "*.schematic.room"
```

HTML reference: [ROOM path helpers](https://ihp-gmbh.github.io/Room/roompaths.html).

## On-disk vs filename

The **filename** encodes the primary view for tools (sniff + LibMan). Inside the file, `Database.summary.view` and each `CellContent.viewType` describe the actual payload. Single-view files (one cell, one view) are the common case for LibMan `define(library, path)`.

## Payload folder (Xschem)

When editing via Xschem integration, exported `.sch`/`.sym` cache files live beside the ROOM file:

```text
my_cell/
  my_cell.schematic.room
  payload/
    my_cell.sch
```

The `.room` file remains authoritative; `payload/` is the editor working copy.
