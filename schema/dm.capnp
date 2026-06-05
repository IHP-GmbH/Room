@0xb2c3d4e5f6071829;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Property = import "common.capnp".Property;

enum ViewType {
  layout    @0;
  schematic @1;
  symbol    @2;
  abstract  @3;
}

enum LayerPurpose {
  drawing  @0;
  pin      @1;
  label    @2;
  boundary @3;
  blockage @4;
  wire     @5;
  fill     @6;
  other    @7;
}

struct LayerSpec {
  layerNum  @0 :UInt16;
  dataType  @1 :UInt16;
  name      @2 :Text;
  purpose   @3 :LayerPurpose;
}
