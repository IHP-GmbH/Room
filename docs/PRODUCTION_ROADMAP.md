# CORE production roadmap

Target: **commercial-quality open-source IC design database** — robust, performant, extensible, suitable for production EDA flows (not a teaching prototype).

This checklist maps external review (e.g. comparison with LStream) to concrete work items. Status: `[ ]` todo · `[~]` in progress · `[x]` done.

Reference: [DESIGN_CHARTER.md](DESIGN_CHARTER.md) · [SCHEMA_EVOLUTION.md](SCHEMA_EVOLUTION.md)

---

## Priority order

1. Charter + v1 Definition of Done  
2. Per-view schema + versioning policy  
3. Derived hierarchy / bbox indices + regression tests  
4. Native OAS geometry codec (remove KLayout from hot path)  
5. Compact layout encoding + benchmarks  
6. API stability + licensing strategy  

---

## 1. Positioning and production criteria

- [x] **Design charter** — scope, audience, non-goals ([DESIGN_CHARTER.md](DESIGN_CHARTER.md))
- [ ] **v1 Definition of Done** — measurable SLA (max design size, round-trip parity, load/save time budgets)
- [ ] **Internal vs exchange** — `.core` as canonical model; GDS/OAS/LEF as exchange (document boundaries)
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

- [ ] **Per-view layer tables** (or lib + per-view mapping)
- [ ] **Layer semantics** — layout vs schematic; pin / label / wire / blockage purposes
- [ ] **Cell identity** — names, aliases, PCell parameters for PDK flows

---

## 4. Derived structures (fast access)

- [x] **In-memory `LibIndex`** — top cells, ref counts, child refs, per-cell bbox, placements ([lib_index.h](../src/lib_index.h), test `lib_index`)
- [x] **Persisted index** — `Lib.index` in `.core`; rebuild on load if entries empty (test `lib_index_persist`)
- [x] **Per-cell bbox** — `recomputeAllBBoxes()` on save/load and importers; validated in tests
- [ ] **Spatial index** — R-tree or tile grid for hit-test / DRC-friendly traversal
- [ ] **Library statistics** — shape counts per layer, cell count, file size metrics in dumps

---

## 5. Geometry and coordinates (64-bit end-to-end)

- [x] **Schema** — `Point` / `Box` / `Transform` use `Int64` ([common.capnp](../schema/common.capnp))
- [ ] **Pipeline audit** — GDS import, transforms, export, overflow regression tests
- [ ] **DBU policy** — document `dbuPerMicron` per view; rounding rules
- [ ] **Exchange parity** — arrays, path extensions, text attributes (GDS/OAS feature matrix)

---

## 6. Compactness and performance

- [ ] **Layout-optimized storage** — layer-grouped runs, repetitions, compressed polygon streams
- [ ] **Benchmark suite** — load/save time, memory peak, file size vs GDS/OAS
- [ ] **Streaming** — incremental read/write for large libraries (avoid full-RAM materialization)

---

## 7. Native codecs (production path)

- [~] **OAS hierarchy reader** — pure C++ ([utils/oas_reader.cpp](../utils/oas_reader.cpp))
- [ ] **OAS geometry codec** — native import/export without KLayout bridge
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
- [ ] **Fuzzing** — `.core` and exchange file parsers
- [ ] **Cross-tool golden files** — KLayout and other viewers

---

## 10. Documentation and governance

- [x] **This roadmap**
- [ ] **Architecture doc** — CORE vs LStream/OA; what we adopt and why
- [ ] **Contribution / API stability policy**
- [ ] **Reply to external reviewers** — production intent + prioritized gaps (see charter)

---

## Suggested response to reviewers (Matthias)

> CORE is early, but the target is a **production-grade open-source IC database**, not a minimal teaching schema. The current model validated API and round-trips quickly. Gaps you noted — skip-friendly view payloads, per-view encapsulation, derived indices, compact geometry — are on the roadmap before v1. Which of these would you treat as blocking for a production layout archive?

---

*Last updated: 2026-06-03*
