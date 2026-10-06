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

# EM S-parameter publish handle (EMStudio / LibMan). Heavy artifacts stay as file refs.
struct EmPort {
  name  @0 :Text;
  index @1 :UInt16;
}

struct EmTopologySnapshot {
  layoutPath @0 :Text;
  layoutHash @1 :Text;
  topCell    @2 :Text;
}

struct EmSetupSnapshot {
  variant       @0 :Text;
  modelPath     @1 :Text;
  modelHash     @2 :Text;
  substratePath @3 :Text;
  substrateHash @4 :Text;
  tool          @5 :Text;
}

struct EmModelViewData {
  defaultVariant @0 :Text;
  snpPath        @1 :Text;
  ports          @2 :List(EmPort);
  tool           @3 :Text;
  emstudioPath   @4 :Text;
  z0             @5 :Float64;
  topology       @6 :EmTopologySnapshot;
  setup          @7 :EmSetupSnapshot;
  snpHash        @8 :Text;
  publishedAt    @9 :Text;
}

struct ViewPayload {
  union {
    layout    @0 :LayoutViewData;
    schematic @1 :SchematicViewData;
    symbol    @2 :SymbolViewData;
    abstract  @3 :AbstractViewData;
    opaque    @4 :OpaqueViewData;
    emModel   @5 :EmModelViewData;
  }
}
