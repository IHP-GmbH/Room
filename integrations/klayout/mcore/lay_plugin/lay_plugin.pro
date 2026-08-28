
TARGET = mcore_ui
DESTDIR = $$OUT_PWD/../../../../lay_plugins

exists($$PWD/../db_plugin/local.pri) {
  include($$PWD/../db_plugin/local.pri)
}

isEmpty(KLAYOUT_SRC) {
  KLAYOUT_SRC = $$clean_path($$PWD/../../../../..)
}

include($$KLAYOUT_SRC/plugins/lay_plugin.pri)

INCLUDEPATH += $$PWD/../db_plugin $$KLAYOUT_SRC/plugins/common
DEPENDPATH += $$PWD/../db_plugin $$KLAYOUT_SRC/plugins/common
LIBS += -L$$DESTDIR/../db_plugins -lmcore

!isEmpty(RPATH) {
  QMAKE_RPATHDIR += $$RPATH/db_plugins
}

HEADERS += \
  layCoreReaderPlugin.h \
  layCoreWriterPlugin.h

SOURCES += \
  layCoreReaderPlugin.cc \
  layCoreWriterPlugin.cc
