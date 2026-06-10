#include "net.h"

namespace core {

/*!****************************************************************************************
 * \brief Constructs a net with name and signal type.
 * \param name         Net name.
 * \param sigType      Electrical class (default: signal).
 *****************************************************************************************/
Net::Net(std::string name, SigType sigType)
    : m_name(std::move(name)), m_sigType(sigType) {}

} // namespace core
