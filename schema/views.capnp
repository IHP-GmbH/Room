@0xe5f60718293a4b5c;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("room::schema");

using LayerSpec    = import "dm.capnp".LayerSpec;
using Property     = import "common.capnp".Property;
using Block        = import "design.capnp".Block;
using CompactBlock = import "compact.capnp".CompactBlock;

# Per-view payloads stored in CellContent.payload. Tools may skip union arms they do not understand.
# C++ API maps payload.*.block to CellContent.block() in memory; see docs/SCHEMA_EVOLUTION.md.

struct LayoutViewData {
  layers  @0 :List(LayerSpec);
  block   @1 :Block;
  compact @2 :CompactBlock;
}

struct SchematicViewData {
  layers  @0 :List(LayerSpec);
  block   @1 :Block;
  compact @2 :CompactBlock;
}

struct SymbolViewData {
  layers  @0 :List(LayerSpec);
  block   @1 :Block;
  compact @2 :CompactBlock;
}

struct AbstractViewData {
  layers  @0 :List(LayerSpec);
  block   @1 :Block;
  compact @2 :CompactBlock;
}

# Opaque bytes for unknown or future view types — copy without decode.
struct OpaqueViewData {
  mimeType @0 :Text;
  data     @1 :Data;
}

struct ViewPayload {
  union {
    layout    @0 :LayoutViewData;
    schematic @1 :SchematicViewData;
    symbol    @2 :SymbolViewData;
    abstract  @3 :AbstractViewData;
    opaque    @4 :OpaqueViewData;
  }
}
