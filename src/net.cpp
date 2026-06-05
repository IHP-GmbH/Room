#include "net.h"

namespace core {

Net::Net(std::string name, SigType sigType)
    : name_(std::move(name)), sigType_(sigType) {}

} // namespace core
