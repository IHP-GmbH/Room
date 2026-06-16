
TARGET = mcore_ui
DESTDIR = $$OUT_PWD/../../../../lay_plugins

include($$PWD/../../../lay_plugin.pri)

exists($$PWD/../db_plugin/local.pri) {
  include($$PWD/../db_plugin/local.pri)
}

INCLUDEPATH += $$PWD/../db_plugin
DEPENDPATH += $$PWD/../db_plugin
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
