
TARGET = mroom
DESTDIR = $$OUT_PWD/../../../../db_plugins

exists($$PWD/local.pri) {
  include($$PWD/local.pri)
}

isEmpty(KLAYOUT_SRC) {
  KLAYOUT_SRC = $$clean_path($$PWD/../../../../..)
}

include($$KLAYOUT_SRC/plugins/db_plugin.pri)

include($$PWD/../room.pri)

LSTREAM_DIR = $$clean_path($$KLAYOUT_SRC/plugins/streamers/lstream/db_plugin)
INCLUDEPATH += $$LSTREAM_DIR

HEADERS += \
  roomPlugin.h \
  roomReader.h \
  roomWriter.h \
  roomFormat.h \
  propertyBridge.h \
  $$LSTREAM_DIR/lstrCompressor.h \
  $$LSTREAM_DIR/lstrCompressed.h

SOURCES += \
  roomPlugin.cc \
  roomReader.cc \
  roomWriter.cc \
  roomFormat.cc \
  propertyBridge.cc \
  $$LSTREAM_DIR/lstrCompressor.cc \
  $$LSTREAM_DIR/lstrCompressed.cc
