# CORE design charter

## Mission

**CORE** (**C**ommon **O**pen **R**epository for **E**DA) is an open-source IC design database intended to reach **commercial production quality**: reliable round-trips, predictable performance, clear extension points, and a stable C++ API for layout and schematic data inside the IHP EDA flow and beyond.

## What CORE is

- A **format-neutral in-memory model** serialized to portable binary `.core` files (Cap'n Proto).
- A **C++17 library** (`core::`) for libraries, cells, views, layers, shapes, instances, and nets.
- An **integration hub** for importers/exporters (GDS, OAS, Qucs today; more exchange formats over time).

## What CORE is not (today)

- A drop-in replacement for OASIS/GDS as a compressed layout archive (bridge via KLayout where native codecs are incomplete).
- A full OpenAccess-compatible API or file format.
- A finished v1.0 product — the schema and tooling are evolving toward production criteria defined below.

## Primary scenarios

| Scenario | Description |
|----------|-------------|
| **IHP internal flow** | Import PDK/layout → edit in CORE → export to exchange formats; schematic path via Qucs. |
| **Tool integration** | Third-party tools link `libcore` and read/write `.core` without owning the schema. |
| **Long-term archive** | Extensible `.core` with per-view payloads, derived indices, and compact layout encoding (v1 target). |

## Design principles

1. **Correctness before features** — round-trip and regression tests are mandatory for exchange paths.
2. **Extensibility by design** — views and extensions must be skippable without parsing unknown payloads (see [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md)).
3. **Explicit encapsulation** — layers and geometry semantics belong to the view that owns them.
4. **Derived data is reproducible** — hierarchy indices and bounding boxes can be stored for speed but must be rebuildable from canonical geometry.
5. **64-bit coordinates end-to-end** — DBU integers in schema, importers, and exporters.

## Non-goals (v0.x)

- GUI or editor implementation.
- Full DRC/LVS engine inside CORE.
- Bit-identical `.core` files across platforms (logical equivalence is required; canonical byte order is a later concern).

## v1 Definition of Done (draft)

Production v1 is reached when **all** of the following hold:

| Criterion | Target (draft) |
|-----------|----------------|
| **Layout round-trip** | sg13g2_stdcell + agreed large regression set: GDS and native OAS paths, geometric XOR pass |
| **Coordinate range** | No silent overflow on ±2⁶³⁻¹ DBU in regression suite |
| **Hierarchy access** | O(1) or O(log n) top-cell and cell lookup via `LibIndex` (memory or persisted) |
| **BBox** | Every layout `Block` has valid bbox after import; validated in CI |
| **View extensibility** | New view type addable without changing layout/schematic parsers (skip unknown views) |
| **Performance budget** | Documented load/save times for reference designs (see [PRODUCTION_ROADMAP.md](PRODUCTION_ROADMAP.md)) |
| **API** | Documented public headers; breaking changes only in major versions |
| **Documentation** | Architecture, schema evolution, and importer parity matrix published |

Exact numeric budgets (file size, seconds, cell counts) to be set when reference large designs are added to the repo.

## Comparison with LStream (positioning)

LStream targets an **extensible layout archive** alternative to GDS/OAS. CORE shares that long-term direction but started with a **minimal unified model** to validate API and IHP integration. Gaps called out by LStream reviewers (per-view schemas, skip-friendly payloads, compact geometry, per-view layers) are tracked in [PRODUCTION_ROADMAP.md](PRODUCTION_ROADMAP.md) as v1 requirements, not optional nice-to-haves.

## Governance

- Schema and public API changes require update to [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md) and roadmap checkboxes.
- Commits to `main` are owner-maintained; contributors follow hook and review policy in the root README.
