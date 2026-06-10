# Compact geometry encoding

CORE stores view topology in two on-disk forms inside `CellContent.payload.*`:

| Form | Cap'n Proto field | Default |
|------|-------------------|---------|
| **Compact** | `compact` (`CompactBlock`) | **yes** — `SaveOptions::compactGeometry = true` |
| **Verbose** | `block` (`Block`) | opt-in |

The reader auto-detects the form: if `block` has shapes, instances, or nets, it reads **verbose**; otherwise it reads **compact** when `compact` holds geometry.

## Encoding features (compact v2)

| Feature | Description |
|---------|-------------|
| Layer grouping | Shapes grouped by `layerId` |
| Delta vertices | Polygon/path centerlines delta-encoded |
| **Rect arrays** | GDS AREF-like grids (`CompactRectArray`) |
| **Rect groups** | GDS SREF-like identical-size placements (`CompactRectGroup`) |
| **Polygon/path repeats** | Shared delta template + origin list |
| **Varint delta streams** | `polygonDeltasPacked` / `pathDeltasPacked` (zigzag varint) when smaller than `List(Int64)` |
| Shape properties | Per-shape `Property` lists preserved |

## Benchmark: `sg13g2_stdcell.gds`

Design: **84 cells**, **15 layers**.  
Environment: Windows 10, MinGW 8.1, **Debug** build, June 2026.  
Command: `compact_geometry_size` + `gds_core_roundtrip` (see [Reproduce](#reproduce)).

### File size

| Artifact | Size | vs verbose | vs GDS |
|----------|------|------------|--------|
| Input GDS | 626 688 B | — | 100% |
| Verbose `.core` | 1 226 152 B | 100% | 196% |
| Compact v2 `.core` | **492 128 B** | **40.1%** | **78.5%** |
| GDS round-trip `.core` | 492 128 B | 40.1% | 78.5% |

Compact v2 is ~60% smaller than verbose and **~21% smaller than source GDS** on this standard-cell library.

### Save / load time (same in-memory database)

Measured by `compact_geometry_size` after GDS import (8 shapes tagged with test properties):

| Operation | Verbose | Compact v2 |
|-----------|---------|------------|
| CORE save | **10.5 ms** | **58.4 ms** |
| CORE load | **27.9 ms** | **31.2 ms** |
| Save+load | 38.4 ms | 89.6 ms |

Compact **save** is slower (repetition detection + encoding). **Load** is within ~12% of verbose.

### Full round-trip pipeline (GDS → CORE → GDS)

| Stage | Time |
|-------|------|
| GDS import | 38.2 ms |
| CORE save (compact v2) | 71.8 ms |
| CORE load | 40.7 ms |
| GDS export | 17.6 ms |
| **CORE total (save+load)** | **112.5 ms** |
| **GDS total (import+export)** | **55.8 ms** |
| **End-to-end total** | **168.3 ms** |

Exchange path (GDS only) is ~2.5× faster than full round-trip; most CORE overhead is on **save** (compact v2 analysis).

### OAS → CORE → OAS (reference)

OAS import/export still uses a KLayout GDS bridge (~8.8 s total on this design in prior runs). See [PRODUCTION_ROADMAP.md](PRODUCTION_ROADMAP.md) §7.

## API

```cpp
db.saveToFile("design.core");  // compact v2 by default

core::SaveOptions opts;
opts.compactGeometry = false;
db.saveToFile("design_verbose.core", opts);

core::Database db = core::Database::loadFromFile("design.core");
```

## Reproduce

```bat
cmake --build build --target compact_geometry_size compact_repetition_unit gds_core_roundtrip
build\compact_geometry_size.exe examples\gds_to_core\data\sg13g2_stdcell.gds build\tests\perf\verbose.core build\tests\perf\compact.core
build\gds_core_roundtrip.exe examples\gds_to_core\data\sg13g2_stdcell.gds build\tests\perf\roundtrip.gds
build\compact_repetition_unit.exe
ctest -R compact -V
```

## Schema

- [`schema/compact.capnp`](../schema/compact.capnp) — `CompactBlock`, repetitions, packed deltas
- [`schema/views.capnp`](../schema/views.capnp) — `compact @2` on each `*ViewData`

See also [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md).
