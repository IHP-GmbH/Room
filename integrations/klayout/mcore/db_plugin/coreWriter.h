#ifndef HDR_coreWriter
#define HDR_coreWriter

#include "dbPluginCommon.h"
#include "dbWriter.h"

namespace coredb
{

class DB_PLUGIN_PUBLIC Writer
  : public db::WriterBase
{
public:
  void write (db::Layout &layout, tl::OutputStream &stream, const db::SaveLayoutOptions &options) override;
};

}

#endif
