#ifndef HDR_roomWriter
#define HDR_roomWriter

#include "dbPluginCommon.h"
#include "dbWriter.h"

namespace roomdb
{

class DB_PLUGIN_PUBLIC Writer
  : public db::WriterBase
{
public:
  void write (db::Layout &layout, tl::OutputStream &stream, const db::SaveLayoutOptions &options) override;
};

}

#endif
