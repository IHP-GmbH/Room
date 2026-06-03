@0xd4e5f60718293a4b;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("cdb::schema");

using Property  = import "common.capnp".Property;
using ViewType  = import "dm.capnp".ViewType;
using LayerSpec = import "dm.capnp".LayerSpec;
using Block     = import "design.capnp".Block;

struct CellContent {
  viewType     @0 :ViewType;
  dbuPerMicron @1 :Float64;
  layers       @2 :List(LayerSpec);
  properties   @3 :List(Property);
  block        @4 :Block;
}

struct Cell {
  name       @0 :Text;
  properties @1 :List(Property);
  contents   @2 :List(CellContent);
}

struct Lib {
  name       @0 :Text;
  properties @1 :List(Property);
  cells      @2 :List(Cell);
}

struct Database {
  version    @0 :Text;
  generator  @1 :Text;
  technology @2 :Text;
  lib        @3 :Lib;
}
