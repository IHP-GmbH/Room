@0xf60718293a4b5c6d;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("core::schema");

using Box = import "common.capnp".Box;

# Optional derived index for fast hierarchy and bbox access (v1 target).
# Rebuild from canonical geometry when missing or stale.

struct CellIndexEntry {
  name      @0 :Text;
  bbox      @1 :Box;
  childRefs @2 :List(Text);
  refCount  @3 :UInt32;
}

struct LibIndex {
  topCells @0 :List(Text);
  entries  @1 :List(CellIndexEntry);
  placementCount @2 :UInt64;
}
