# ROOM schema evolution policy

Cap'n Proto schemas live under `schema/`. This document defines how they change on the path to production v1.

## Goals

- **Forward compatibility** — older readers ignore unknown fields; newer readers tolerate missing optional data.
- **Skip-friendly views** — tools that do not understand a view type must be able to copy or store opaque payloads without decoding geometry.
- **Clear deprecation** — fields move through `deprecated` → unused → removed across major versions only.

## Versioning

| Artifact | Field | Meaning |
|----------|-------|---------|
| `Database.version` | text | ROOM format version (e.g. `0.2`, `1.0`) — set by serializer |
| Cap'n Proto file IDs | `@0x…` | Fixed per schema file; never reuse IDs |

**Major** (e.g. 0.x → 1.0): breaking C++ API or incompatible default interpretation.  
**Minor**: new fields, new view types, new optional structs — backward compatible.

## Current model (v1.0)

```
Database → Lib → Cell(aliases[], pCell) → CellContent(viewType, dbuPerMicron, dbuPerEditorUnit, payload)
                → layers[]   (library master catalog)
```

Each `CellContent` owns a per-view `layers[]` table in C++ and in `payload.*.layers` on disk. Geometry lives in `payload.*.block` (verbose) or `payload.*.compact` (default). The C++ API exposes `CellContent.block()` and `CellContent.layers()` as the in-memory view body.

```
CellContent.payload :union
  layout    → LayoutViewData   (layers + block)
  schematic → SchematicViewData
  symbol    → SymbolViewData
  abstract  → AbstractViewData
  opaque    → OpaqueViewData   # skip without decode
```

Optional persisted [index.capnp](../schema/index.capnp) at `Lib` level (not wired yet):

- `topCells`, per-cell `bbox`, `childRefs` — derived; rebuild if absent or stale.

## Rules for schema changes

1. **Add, don't repurpose** — new capability gets new field numbers or union arms; never change the meaning of an existing field.
2. **Deprecate in place** — comment `# deprecated: use X` in `.capnp`; keep field until next major.
3. **Update roadmap** — mark items in [PRODUCTION_ROADMAP.md](PRODUCTION_ROADMAP.md).
4. **Regression tests** — any serializer change must keep existing round-trip tests green; add tests for new fields.
5. **C++ API lag is allowed** — schema may lead the C++ API briefly if new structs are optional and unused by legacy code paths.

## Format history

| Version | On-disk geometry | Notes |
|---------|------------------|-------|
| 0.1 | `CellContent.block` | Pre-release; not supported |
| 0.2 | dual `block` + `payload` | Short-lived; not supported |
| 1.0 | `CellContent.payload` only | Current |

## Extension points (planned)

- `Property` — string key/value today; insufficient for typed PDK data.
- `Extension` struct: `id`, `version`, `DataBlob` — registered extension IDs documented in a central registry (TBD).

## Review checklist (before merging schema PR)

- [ ] New fields use new indices only  
- [ ] Unknown view / extension path documented  
- [ ] `Database.version` bump considered  
- [ ] Round-trip tests updated  
- [ ] PRODUCTION_ROADMAP.md checkbox updated  
