#include "coreReader.h"
#include "coreWriter.h"
#include "coreFormat.h"

#include "dbStream.h"

#include "tlClassRegistry.h"
#include "tlStream.h"

namespace coredb
{

class CoreFormatDeclaration
  : public db::StreamFormatDeclaration
{
  std::string format_name () const override { return "CORE"; }
  std::string format_desc () const override { return "IHP CommonDB CORE"; }
  std::string format_title () const override { return "CommonDB CORE"; }
  std::string file_format () const override
  {
    return "CORE layout (*.layout.core);;CORE legacy (*.core)";
  }

  bool detect (tl::InputStream &stream) const override
  {
    return tl::match_filename_to_format (stream.filename (), file_format ());
  }

  db::ReaderBase *create_reader (tl::InputStream &stream) const override
  {
    return new Reader (stream);
  }

  db::WriterBase *create_writer () const override
  {
    return new Writer ();
  }

  bool can_read () const override { return true; }
  bool can_write () const override { return true; }
  bool supports_context () const override { return false; }
};

static tl::RegisteredClass<db::StreamFormatDeclaration>
  format_decl (new CoreFormatDeclaration (), 2100, "CORE");

}
