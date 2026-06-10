#include "term.h"

namespace core {

/*!****************************************************************************************
 * \brief Constructs a net terminal.
 * \param name         Terminal name.
 * \param layerId      Layer index for the connection point.
 * \param position     Location in database units.
 *****************************************************************************************/
Term::Term(std::string name, std::uint32_t layerId, Point position)
    : m_name(std::move(name)), m_layerId(layerId), m_position(position) {}

} // namespace core
