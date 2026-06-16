#include "coreFormat.h"
#include "coreWriter.h"
#include "dbSaveLayoutOptions.h"
#include "layCoreWriterPlugin.h"

#include "tlClassRegistry.h"

namespace lay
{

class CoreWriterPluginDeclaration
  : public StreamWriterPluginDeclaration
{
public:
  CoreWriterPluginDeclaration ()
    : StreamWriterPluginDeclaration (coredb::WriterOptions ().format_name ())
  {
  }

  StreamWriterOptionsPage *format_specific_options_page (QWidget * /*parent*/) const override
  {
    return nullptr;
  }

  db::FormatSpecificWriterOptions *create_specific_options () const override
  {
    return new coredb::WriterOptions ();
  }
};

static tl::RegisteredClass<lay::PluginDeclaration>
  plugin_decl (new CoreWriterPluginDeclaration (), 10000, "COREWriter");

}
