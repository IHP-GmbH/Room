@0xa1b2c3d4e5f60718;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("room::schema");

# Basic 2D geometry types used across layout, schematic and symbol views.

struct Point {
  x @0 :Int64;
  y @1 :Int64;
}

struct Box {
  llx @0 :Int64;
  lly @1 :Int64;
  urx @2 :Int64;
  ury @3 :Int64;
}

enum Orient {
  r0   @0;
  r90  @1;
  r180 @2;
  r270 @3;
  my   @4;  # mirror Y
  mx   @5;  # mirror X
  mx90 @6;
  my90 @7;
}

struct Transform {
  x       @0 :Int64;
  y       @1 :Int64;
  orient  @2 :Orient;
  mag     @3 :Float64 = 1.0;
}

struct Property {
  name  @0 :Text;
  value @1 :Text;
}
