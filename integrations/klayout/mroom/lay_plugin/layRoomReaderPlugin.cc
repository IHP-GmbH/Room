#include "roomFormat.h"
#include "roomReader.h"
#include "dbLoadLayoutOptions.h"
#include "layRoomReaderPlugin.h"

#include "tlClassRegistry.h"

namespace lay
{

class CoreReaderPluginDeclaration
  : public StreamReaderPluginDeclaration
{
public:
  CoreReaderPluginDeclaration ()
    : StreamReaderPluginDeclaration (roomdb::ReaderOptions ().format_name ())
  {
  }

  StreamReaderOptionsPage *format_specific_options_page (QWidget * /*parent*/) const override
  {
    return nullptr;
  }

  db::FormatSpecificReaderOptions *create_specific_options () const override
  {
    return new roomdb::ReaderOptions ();
  }
};

static tl::RegisteredClass<lay::PluginDeclaration>
  plugin_decl (new CoreReaderPluginDeclaration (), 10000, "ROOMReader");

}
