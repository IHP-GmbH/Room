#include "net.h"

namespace core {

Net::Net(std::string name, SigType sigType)
    : m_name(std::move(name)), m_sigType(sigType) {}

} // namespace core
