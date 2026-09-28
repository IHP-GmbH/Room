
TARGET = mroom
DESTDIR = $$OUT_PWD/../../../../db_plugins

exists($$PWD/local.pri) {
  include($$PWD/local.pri)
}

isEmpty(KLAYOUT_SRC) {
  KLAYOUT_SRC = $$clean_path($$PWD/../../../../..)
}

include($$KLAYOUT_SRC/plugins/db_plugin.pri)

include($$PWD/../core.pri)

HEADERS += \
  roomPlugin.h \
  roomReader.h \
  roomWriter.h \
  roomFormat.h \
  propertyBridge.h

SOURCES += \
  roomPlugin.cc \
  roomReader.cc \
  roomWriter.cc \
  roomFormat.cc \
  propertyBridge.cc
