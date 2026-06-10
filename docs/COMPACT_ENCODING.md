# Compact geometry encoding

CORE can store view topology in two on-disk forms inside `CellContent.payload.*`:

| Form | Cap'n Proto field | Default |
|------|-------------------|---------|
| **Compact** | `compact` (`CompactBlock`) | **yes** — `SaveOptions::compactGeometry = true` |
| **Verbose** | `block` (`Block`) | opt-in |

The reader auto-detects the form: if `block` has shapes, instances, or nets, it reads **verbose**; otherwise it reads **compact** when `compact` holds geometry.

Compact encoding groups shapes by layer, stores rectangle coordinates as flat arrays, delta-encodes polygon/path vertices, and preserves per-shape `Property` lists.

## Benchmark: `sg13g2_stdcell.gds`

Design: **84 cells**, **15 layers**. Measured on Windows (MinGW 8.1, Debug build), June 2026.

### File size

| Artifact | Size | vs verbose |
|----------|------|------------|
| Input GDS | 626 688 B | — |
| Verbose `.core` | 1 226 152 B | 100% |
| Compact `.core` (default) | 918 056 B | **74.9%** (−25.1%) |
| GDS round-trip `.core` | 917 360 B | ~75% |

Compact reduces `.core` size by about **one quarter** on this standard-cell library without gzip.

### Save / load time (same in-memory database)

| Operation | Verbose | Compact |
|-----------|---------|---------|
| CORE save | 17.8 ms | 32.9 ms |
| CORE load | 43.2 ms | 43.5 ms |

Compact **save** is slower (encoding cost); **load** is effectively the same. For archive workflows the smaller file usually wins on I/O.

### Full round-trip pipeline

#### GDS → CORE → GDS (native GDS path)

| Stage | Time |
|-------|------|
| GDS import | 48.7 ms |
| CORE save (compact) | 23.2 ms |
| CORE load | 56.7 ms |
| GDS export | 18.9 ms |
| **Total** | **147.6 ms** |

#### OAS → CORE → OAS (via KLayout bridge today)

| Stage | Time |
|-------|------|
| OAS import | 4241 ms |
| CORE save (compact) | 18.1 ms |
| CORE load | 45.3 ms |
| OAS export | 4544 ms |
| **Total** | **8849 ms** |

OAS import/export currently converts through a temporary GDS file using KLayout (`OasImporter` / `OasExporter`). CORE serialization is fast; the OAS bridge dominates. A native OAS reader is on the [production roadmap](PRODUCTION_ROADMAP.md).

## API

```cpp
// Default: compact
db.saveToFile("design.core");

// Verbose block encoding (debugging, external tools expecting block)
core::SaveOptions opts;
opts.compactGeometry = false;
db.saveToFile("design_verbose.core", opts);

// Load: auto-detects compact vs verbose
core::Database db = core::Database::loadFromFile("design.core");
```

## Reproduce

```bat
cmake --build build --target compact_geometry_size gds_core_roundtrip oas_core_roundtrip
build\compact_geometry_size.exe examples\gds_to_core\data\sg13g2_stdcell.gds
build\gds_core_roundtrip.exe examples\gds_to_core\data\sg13g2_stdcell.gds build\tests\perf\roundtrip.gds
scripts\run_oas_core_roundtrip_test.cmd build
```

CTest: `compact_geometry_size` (size + property round-trip).

## Schema

- [`schema/compact.capnp`](../schema/compact.capnp) — `CompactBlock`, `CompactLayerShapes`
- [`schema/views.capnp`](../schema/views.capnp) — `compact @2` on each `*ViewData`

See also [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md).
