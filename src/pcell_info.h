#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief Parametric cell metadata: master cell name and typed parameters.
 *****************************************************************************************/
class PCellInfo {
public:
    const std::string &                                 masterName() const { return m_masterName; }
    void                                                setMasterName(std::string name) { m_masterName = std::move(name); }

    std::vector<Property> &                             parameters() { return m_parameters; }
    const std::vector<Property> &                       parameters() const { return m_parameters; }

    bool                                                isPCell() const { return !m_masterName.empty(); }

private:
    std::string                                         m_masterName;
    std::vector<Property>                               m_parameters;
};

} // namespace room
