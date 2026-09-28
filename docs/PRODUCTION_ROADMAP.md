# ROOM production roadmap

Target: **commercial-quality open-source IC design database** — robust, performant, extensible, suitable for production EDA flows (not a teaching prototype).

This checklist maps external review (e.g. comparison with LStream) to concrete work items. Status: `[ ]` todo · `[~]` in progress · `[x]` done.

Reference: [DESIGN_CHARTER.md](DESIGN_CHARTER.md) · [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md)

---

## Priority order

1. Charter + v1 Definition of Done  
2. Skip-friendly opaque views + extension registry  
3. ~~Native OAS read/write (strict)~~ (done; preserved round-trip XOR 0; layout editor owns edited export)  
4. Streaming + optional `.room` compression  
5. Benchmark CI table + large-design stress tests  
6. API stability + licensing strategy  

---

## 1. Positioning and production criteria

- [x] **Design charter** — scope, audience, non-goals ([DESIGN_CHARTER.md](DESIGN_CHARTER.md))
- [ ] **v1 Definition of Done** — measurable SLA (max design size, round-trip parity, load/save time budgets)
- [ ] **Internal vs exchange** — `.room` as canonical model; GDS/OAS/LEF as exchange (document boundaries)
- [ ] **Public roadmap** — v0.x → v1.0 milestones on GitHub

---

## 2. Format architecture: extensibility

- [x] **Schema evolution policy** ([SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md))
- [x] **Per-view payloads** — `CellContent.payload` only on disk (format v1.0); C++ API uses `CellContent.block()` in memory
- [ ] **Skip-friendly sub-messages** — opaque views preserved on read/write without full geometry parse
- [ ] **Extension registry** — typed vendor/user extensions beyond string properties
- [x] **Remove legacy `block` / `legacyLayers`** from `CellContent` schema

---

## 3. Encapsulation (layers, views)

- [x] **Per-view layer tables** — `CellContent.layers()` + per-view payload; lib master catalog; GDS/Qucs importers populate view tables
- [x] **Layer semantics** — `LayerPurpose` on `LayerSpec`; GDS (boundary/wire/label) and schematic (wire/pin/label) heuristics; round-trip test `encapsulation_roundtrip`
- [x] **Cell identity** — `Cell.aliases`, `PCellInfo` (master + parameters) in schema/C++; serialization round-trip

---

## 4. Derived structures (fast access)

- [x] **In-memory `LibIndex`** — top cells, ref counts, child refs, per-cell bbox, placements ([lib_index.h](../src/lib_index.h), test `lib_index`)
- [x] **Persisted index** — `Lib.index` in `.room`; rebuild on load if entries empty (test `lib_index_persist`)
- [x] **Per-cell bbox** — `recomputeAllBBoxes()` on save/load and importers; validated in tests
- [ ] **Spatial index** — R-tree or tile grid for hit-test / DRC-friendly traversal
- [ ] **Library statistics** — shape counts per layer, cell count, file size metrics in dumps

---

## 5. Geometry and coordinates (64-bit end-to-end)

- [x] **Schema** — `Point` / `Box` / `Transform` use `Int64` ([common.capnp](../schema/common.capnp))
- [ ] **Pipeline audit** — GDS import, transforms, export, overflow regression tests
- [x] **DBU policy** — `dbuPerMicron` (layout) and `dbuPerEditorUnit` (schematic/symbol); see [html/coordscale.html](html/coordscale.html)
- [ ] **Exchange parity** — arrays, path extensions, text attributes (GDS/OAS feature matrix)

---

## 6. Compactness and performance

- [x] **Layout-optimized storage (v2)** — layer groups, delta vertices, AREF/SREF-like rect repeats, polygon/path templates, zigzag varint packed streams ([COMPACT_ENCODING.md](COMPACT_ENCODING.md))
- [~] **Benchmark suite** — `compact_geometry_size` + [COMPACT_ENCODING.md](COMPACT_ENCODING.md) (Debug, June 2026); CI table + memory peak TBD
- [ ] **Streaming** — incremental read/write for large libraries (avoid full-RAM materialization)
- [ ] **Optional file compression** — gzip/zstd wrapper on `.room` (varint streams done; whole-file gzip TBD)

---

## 7. Native codecs (production path)

- [x] **OAS hierarchy reader** — pure C++ ([utils/oas_reader.cpp](../utils/oas_reader.cpp))
- [x] **OAS geometry codec** — native import + strict export ([utils/oas_reader.cpp](../utils/oas_reader.cpp), [utils/oas_writer.cpp](../utils/oas_writer.cpp), [utils/oas_strict.cpp](../utils/oas_strict.cpp), [utils/oas_geometry.cpp](../utils/oas_geometry.cpp)); preserved payload round-trip (KLayout XOR 0 on sg13g2); KLayout for GDS→OAS fixture prep and fallback template only
- [ ] **GDS regression matrix** — expand beyond sg13g2 + sample
- [ ] **LEF/DEF, CDL/SPICE** — per product roadmap

---

## 8. API and commercial adoption

- [ ] **Public vs internal API** — stable headers, semantic versioning
- [ ] **Error model** — warnings vs errors; structured import reports
- [ ] **Thread-safety policy** — documented (e.g. read-only concurrent access)
- [ ] **Licensing strategy** — GPL today; evaluate LGPL / dual license for ecosystem

---

## 9. Quality bar

- [x] **Round-trip CI** — GDS + OAS (KLayout XOR) on Ubuntu
- [ ] **Large-design tests** — PDK cells, stress files, timing budgets in CI summary
- [ ] **Fuzzing** — `.room` and exchange file parsers
- [ ] **Cross-tool golden files** — KLayout and other viewers

---

## 10. Documentation and governance

- [x] **This roadmap**
- [ ] **Architecture doc** — ROOM vs LStream/OA; what we adopt and why
- [ ] **Contribution / API stability policy**
- [ ] **Reply to external reviewers** — production intent + prioritized gaps (see charter)

---

*Last updated: 2026-06-10*
