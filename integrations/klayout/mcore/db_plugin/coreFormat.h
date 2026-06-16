#ifndef HDR_coreFormat
#define HDR_coreFormat

#include "dbPluginCommon.h"
#include "dbLoadLayoutOptions.h"
#include "dbSaveLayoutOptions.h"

namespace coredb
{

class DB_PLUGIN_PUBLIC ReaderOptions
  : public db::FormatSpecificReaderOptions
{
public:
  ReaderOptions () = default;

  db::FormatSpecificReaderOptions *clone () const override
  {
    return new ReaderOptions (*this);
  }

  const std::string &format_name () const override
  {
    static const std::string name ("CORE");
    return name;
  }
};

class DB_PLUGIN_PUBLIC WriterOptions
  : public db::FormatSpecificWriterOptions
{
public:
  WriterOptions () = default;

  db::FormatSpecificWriterOptions *clone () const override
  {
    return new WriterOptions (*this);
  }

  const std::string &format_name () const override
  {
    static const std::string name ("CORE");
    return name;
  }
};

}

#endif
