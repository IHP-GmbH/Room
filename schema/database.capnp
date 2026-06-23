@0xd4e5f60718293a4b;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Property  = import "common.capnp".Property;
using ViewType  = import "dm.capnp".ViewType;
using LayerSpec = import "dm.capnp".LayerSpec;
using ViewPayload = import "views.capnp".ViewPayload;
using LibIndex    = import "index.capnp".LibIndex;

struct CellContent {
  viewType     @0 :ViewType;
  dbuPerMicron @1 :Float64;
  properties   @2 :List(Property);
  payload      @3 :ViewPayload;
}

struct PCellInfo {
  masterName @0 :Text;
  parameters @1 :List(Property);
}

struct Cell {
  name       @0 :Text;
  properties @1 :List(Property);
  contents   @2 :List(CellContent);
  aliases    @3 :List(Text);
  pCell      @4 :PCellInfo;
}

struct Lib {
  name       @0 :Text;
  properties @1 :List(Property);
  cells      @2 :List(Cell);
  layers     @3 :List(LayerSpec);
  index      @4 :LibIndex;
}

# Denormalized file header for fast sniffing (LibMan, tools) without reading geometry payloads.
struct FileSummary {
  view        @0 :ViewType;
  cellCount   @1 :UInt32;
  primaryCell @2 :Text;
}

struct Database {
  version    @0 :Text;
  generator  @1 :Text;
  technology @2 :Text;
  lib        @3 :Lib;
  summary    @4 :FileSummary;
}
