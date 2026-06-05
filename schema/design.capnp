@0xc3d4e5f60718293a;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Point     = import "common.capnp".Point;
using Box       = import "common.capnp".Box;
using Transform = import "common.capnp".Transform;
using Property  = import "common.capnp".Property;

struct RectGeom {
  box     @0 :Box;
  layerId @1 :UInt32;
}

struct PolygonGeom {
  points  @0 :List(Point);
  layerId @1 :UInt32;
}

struct PathGeom {
  points  @0 :List(Point);
  width   @1 :UInt32;
  layerId @2 :UInt32;
  cap     @3 :PathCap = round;
  enum PathCap { flush @0; round @1; square @2; }
}

struct TextGeom {
  position @0 :Point;
  text     @1 :Text;
  layerId  @2 :UInt32;
  height   @3 :UInt32;
}

struct Shape {
  union {
    rect    @0 :RectGeom;
    polygon @1 :PolygonGeom;
    path    @2 :PathGeom;
    text    @3 :TextGeom;
  }
  properties @4 :List(Property);
}

struct Instance {
  cellName   @0 :Text;
  transform  @1 :Transform;
  properties @2 :List(Property);
}

struct Term {
  name     @0 :Text;
  layerId  @1 :UInt32;
  position @2 :Point;
}

struct Net {
  name    @0 :Text;
  terms   @1 :List(Term);
  sigType @2 :SigType = signal;
  enum SigType { signal @0; power @1; ground @2; clock @3; }
}

struct Block {
  shapes    @0 :List(Shape);
  instances @1 :List(Instance);
  nets      @2 :List(Net);
  bbox      @3 :Box;
}
