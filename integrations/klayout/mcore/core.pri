# CommonDB CORE sources compiled into the KLayout db plugin (no external libcore).
#
# Requires COMMONDB_ROOT in db_plugin/local.pri (Cap'n Proto from third_party/capnp-install).

isEmpty(COMMONDB_ROOT) {
  error("Set COMMONDB_ROOT in integrations/klayout/mcore/db_plugin/local.pri")
}

CORE_GEN = $$COMMONDB_ROOT/integrations/klayout/generated
CORE_SRC = $$COMMONDB_ROOT/src
CAPNP_ROOT = $$COMMONDB_ROOT/third_party/capnp-install
INCLUDEPATH += \
  $$CORE_SRC \
  $$CORE_GEN \
  $$COMMONDB_ROOT/utils \
  $$CAPNP_ROOT/include

LIBS += -L$$CAPNP_ROOT/lib -lcapnp -lkj

DEFINES += CORE_KLAYOUT_PLUGIN

CONFIG += c++17
QMAKE_CXXFLAGS += -std=c++17

SOURCES += \
  $$CORE_SRC/box.cpp \
  $$CORE_SRC/enums.cpp \
  $$CORE_SRC/shape.cpp \
  $$CORE_SRC/instance.cpp \
  $$CORE_SRC/term.cpp \
  $$CORE_SRC/net.cpp \
  $$CORE_SRC/block.cpp \
  $$CORE_SRC/cell_content.cpp \
  $$CORE_SRC/source_info.cpp \
  $$CORE_SRC/cell.cpp \
  $$CORE_SRC/layer_utils.cpp \
  $$CORE_SRC/lib.cpp \
  $$CORE_SRC/lib_index.cpp \
  $$CORE_SRC/database.cpp \
  $$CORE_SRC/file_summary.cpp \
  $$CORE_SRC/serialization.cpp \
  $$CORE_SRC/compact_codec.cpp \
  $$CORE_SRC/varint_codec.cpp \
  $$CORE_GEN/common.capnp.c++ \
  $$CORE_GEN/dm.capnp.c++ \
  $$CORE_GEN/design.capnp.c++ \
  $$CORE_GEN/database.capnp.c++ \
  $$CORE_GEN/views.capnp.c++ \
  $$CORE_GEN/index.capnp.c++ \
  $$CORE_GEN/compact.capnp.c++