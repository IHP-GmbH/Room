# Compact geometry encoding

ROOM stores view topology in two on-disk forms inside `CellContent.payload.*`:

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
Command: `compact_geometry_size` + `gds_room_roundtrip` (see [Reproduce](#reproduce)).

### File size

| Artifact | Size | vs verbose | vs GDS |
|----------|------|------------|--------|
| Input GDS | 626 688 B | — | 100% |
| Verbose `.room` | 1 216 976 B | 100% | 194% |
| Compact v2 `.room` | **485 320 B** | **39.9%** | **77.4%** |
| GDS round-trip `.room` | 485 320 B | 39.9% | 77.4% |

Compact v2 is ~60% smaller than verbose and **~23% smaller than source GDS** on this standard-cell library.

### Save / load time (same in-memory database)

Measured by `compact_geometry_size` after GDS import (8 shapes tagged with test properties):

| Operation | Verbose | Compact v2 |
|-----------|---------|------------|
| ROOM save | **10.5 ms** | **61.4 ms** |
| ROOM load | **27.4 ms** | **32.5 ms** |
| Save+load | 37.9 ms | 93.9 ms |

Compact **save** is slower (repetition detection + encoding). **Load** is within ~18% of verbose.

### Full round-trip pipeline (GDS → ROOM → GDS)

| Stage | Time |
|-------|------|
| GDS import | 35.2 ms |
| ROOM save (compact v2) | 53.6 ms |
| ROOM load | 37.3 ms |
| GDS export | 15.0 ms |
| **ROOM total (save+load)** | **91.0 ms** |
| **GDS total (import+export)** | **50.1 ms** |
| **End-to-end total** | **141.1 ms** |

Exchange path (GDS only) is ~2.8× faster than full round-trip; most ROOM overhead is on **save** (compact v2 analysis).

### OAS → ROOM → OAS (native strict codec)

Design: **84 cells**, **2932 shapes**, **12 layers** (`sg13g2_stdcell` via KLayout-prepared `source.oas`).  
Environment: Windows 10, MinGW 8.1, **Debug** build, June 2026.  
Command: `scripts\run_oas_room_roundtrip_test.cmd` or `oas_room_roundtrip source.oas roundtrip.oas`.

| Step | Time |
|------|------|
| OAS import | ~28 ms |
| ROOM save | ~37 ms |
| ROOM load | ~36 ms |
| OAS export (preserved strict payload) | ~7 ms |
| OAS re-import (verify) | ~53 ms |
| **ROOM total (save+load)** | **~73 ms** |
| **OAS total (import+export)** | **~35 ms** |
| **End-to-end total** | **~107 ms** |

Import decodes geometry into ROOM and stores strict OAS header/tail/geometry bytes on the library and cells. Export reassembles the file from those payloads (byte-identical to source on unchanged designs). **KLayout XOR = 0** on the reference fixture. A fallback writer (KLayout strict template + native rects) exists for ROOM-only designs without preserved payload; layout editing/export polish is left to the layout editor.

## API

```cpp
db.saveToFile("design.room");  // compact v2 by default

room::SaveOptions opts;
opts.compactGeometry = false;
db.saveToFile("design_verbose.room", opts);

room::Database db = room::Database::loadFromFile("design.room");
```

## Reproduce

```bat
cmake --build build --target compact_geometry_size compact_repetition_unit gds_room_roundtrip oas_room_roundtrip
build\compact_geometry_size.exe examples\gds_to_room\data\sg13g2_stdcell.gds build\tests\perf\verbose.room build\tests\perf\compact.room
build\gds_room_roundtrip.exe examples\gds_to_room\data\sg13g2_stdcell.gds build\tests\perf\roundtrip.gds
scripts\run_oas_room_roundtrip_test.cmd
build\compact_repetition_unit.exe
ctest -R "compact|encapsulation|oas" -V
```

## Schema

- [`schema/compact.capnp`](../schema/compact.capnp) — `CompactBlock`, repetitions, packed deltas
- [`schema/views.capnp`](../schema/views.capnp) — `compact @2` on each `*ViewData`

See also [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md).
