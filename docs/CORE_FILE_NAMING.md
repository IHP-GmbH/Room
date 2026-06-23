# CORE file naming

View-specific CORE files use the pattern:

```text
<cell>.<view>.core
```

| View | Suffix | Example |
|------|--------|---------|
| Layout | `.layout.core` | `sg13g2_stdcell.layout.core` |
| Schematic | `.schematic.core` | `sg13g2_stdcell.schematic.core` |
| Symbol | `.symbol.core` | `inv.symbol.core` |
| Abstract | `.abstract.core` | `block.abstract.core` |

LibMan and the Xschem/KLayout integrations expect this layout. Legacy names like `schematic.core` (no cell prefix) still work for examples but are not recommended for libraries.

## API (`core_paths.h`)

```cpp
#include "core_paths.h"

core::coreFileName("inv", core::ViewType::Schematic);  // "inv.schematic.core"
core::parseCoreFilePath("path/inv.schematic.core");   // { cellName="inv", view=Schematic, valid=true }
core::isViewCoreFile(path, core::ViewType::Layout);
core::isCoreFilePath(path);                           // any *.core
core::coreFileGlob(core::ViewType::Schematic);        // "*.schematic.core"
```

HTML reference: [`docs/html/corepaths.html`](html/corepaths.html).

## On-disk vs filename

The **filename** encodes the primary view for tools (sniff + LibMan). Inside the file, `Database.summary.view` and each `CellContent.viewType` describe the actual payload. Single-view files (one cell, one view) are the common case for LibMan `define(library, path)`.

## Payload folder (Xschem)

When editing via Xschem integration, exported `.sch`/`.sym` cache files live beside the CORE file:

```text
my_cell/
  my_cell.schematic.core
  payload/
    my_cell.sch
```

The `.core` file remains authoritative; `payload/` is the editor working copy.
