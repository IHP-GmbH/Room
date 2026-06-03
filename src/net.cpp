#include "net.h"

namespace cdb {

Net::Net(std::string name, SigType sigType)
    : name_(std::move(name)), sigType_(sigType) {}

} // namespace cdb
