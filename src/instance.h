#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief The Instance class places a reference to another cell with a transform and properties.
 *****************************************************************************************/
class Instance {
public:
    Instance(std::string cellName, Transform transform);

    const std::string &                                 cellName() const { return m_cellName; }
    Transform &                                         transform() { return m_transform; }
    const Transform &                                   transform() const { return m_transform; }

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

private:
    std::string                                         m_cellName;
    Transform                                           m_transform;
    std::vector<Property>                               m_properties;
};

} // namespace room
