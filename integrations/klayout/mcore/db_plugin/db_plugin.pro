
TARGET = mcore
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
  corePlugin.h \
  coreReader.h \
  coreWriter.h \
  coreFormat.h \
  propertyBridge.h

SOURCES += \
  corePlugin.cc \
  coreReader.cc \
  coreWriter.cc \
  coreFormat.cc \
  propertyBridge.cc
