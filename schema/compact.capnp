@0xb3c4d5e6f708192a;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Point     = import "common.capnp".Point;
using Box       = import "common.capnp".Box;
using Transform = import "common.capnp".Transform;
using Property  = import "common.capnp".Property;
using Instance  = import "design.capnp".Instance;
using Net       = import "design.capnp".Net;

# Layer-grouped geometry with delta-encoded polygon/path vertices.
# Rectangles are stored as flat [llx,lly,urx,ury,...] per layer.

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

  # Per-shape properties in layer order: rects, polygons, paths, texts.
  shapePropertyCounts @11 :List(UInt32);
  shapeProperties @12 :List(Property);
}

struct CompactBlock {
  layerShapes @0 :List(CompactLayerShapes);
  instances @1 :List(Instance);
  nets @2 :List(Net);
  bbox @3 :Box;
}
