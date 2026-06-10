@0xb3c4d5e6f708192a;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Point     = import "common.capnp".Point;
using Box       = import "common.capnp".Box;
using Transform = import "common.capnp".Transform;
using Property  = import "common.capnp".Property;
using Instance  = import "design.capnp".Instance;
using Net       = import "design.capnp".Net;

# GDS AREF-like grid of identical rectangles.
struct CompactRectArray {
  originLlx @0 :Int64;
  originLly @1 :Int64;
  width     @2 :Int64;
  height    @3 :Int64;
  columns   @4 :UInt32;
  rows      @5 :UInt32;
  stepX     @6 :Int64;
  stepY     @7 :Int64;
}

# GDS SREF-like placements of identical rectangles (same width/height).
struct CompactRectGroup {
  width @0 :Int64;
  height @1 :Int64;
  llx   @2 :List(Int64);
  lly   @3 :List(Int64);
}

# Identical polygon geometry placed at different origins (first vertex).
struct CompactPolygonRepeat {
  vertexCount @0 :UInt32;
  deltas      @1 :List(Int64);
  originX     @2 :List(Int64);
  originY     @3 :List(Int64);
}

# Identical path geometry (width + centerline) at different origins.
struct CompactPathRepeat {
  width       @0 :UInt32;
  vertexCount @1 :UInt32;
  deltas      @2 :List(Int64);
  originX     @3 :List(Int64);
  originY     @4 :List(Int64);
}

struct CompactLayerShapes {
  layerId @0 :UInt32;

  rectCoords @1 :List(Int64);

  polygonVertexCounts @2 :List(UInt32);
  polygonDeltas @3 :List(Int64);

  pathWidths @4 :List(UInt32);
  pathVertexCounts @5 :List(UInt32);
  pathDeltas @6 :List(Int64);

  textX @7 :List(Int64);
  textY @8 :List(Int64);
  textHeights @9 :List(UInt32);
  texts @10 :List(Text);

  shapePropertyCounts @11 :List(UInt32);
  shapeProperties @12 :List(Property);

  rectArrays @13 :List(CompactRectArray);
  rectGroups @14 :List(CompactRectGroup);
  polygonRepeats @15 :List(CompactPolygonRepeat);
  pathRepeats @16 :List(CompactPathRepeat);

  # Zigzag-varint packed Int64 streams (polygon/path). When non-empty, preferred over list @3/@6.
  polygonDeltasPacked @17 :Data;
  pathDeltasPacked @18 :Data;
}

struct CompactBlock {
  layerShapes @0 :List(CompactLayerShapes);
  instances @1 :List(Instance);
  nets @2 :List(Net);
  bbox @3 :Box;
}
