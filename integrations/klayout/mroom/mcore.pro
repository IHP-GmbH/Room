
TEMPLATE = subdirs

SUBDIRS = db_plugin

!equals(HAVE_QT, "0") {
  SUBDIRS += lay_plugin
  lay_plugin.depends += db_plugin
}
