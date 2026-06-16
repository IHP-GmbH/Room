#ifndef HDR_coreReader
#define HDR_coreReader

#include "dbPluginCommon.h"
#include "dbCommonReader.h"
#include "dbLayout.h"

#include "tlStream.h"

namespace coredb
{

class DB_PLUGIN_PUBLIC Reader
  : public db::CommonReader
{
public:
  explicit Reader (tl::InputStream &stream);
  ~Reader () noexcept override;

  const char *format () const override { return "CORE"; }

protected:
  void do_read (db::Layout &layout) override;
  void common_reader_error (const std::string &msg) override;
  void common_reader_warn (const std::string &msg, int warn_level = 1) override;

private:
  tl::InputStream &m_stream;
};

}

#endif
