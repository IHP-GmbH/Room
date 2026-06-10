#pragma once

#include "term.h"
#include "types.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The Net class represents a connectivity net with a signal type and terminal list.
 *****************************************************************************************/
class Net {
public:
    explicit Net(std::string name, SigType sigType = SigType::Signal);

    const std::string &                                 name() const { return m_name; }
    SigType                                             sigType() const { return m_sigType; }
    void                                                setSigType(SigType type) { m_sigType = type; }

    std::vector<Term> &                                 terms() { return m_terms; }
    const std::vector<Term> &                           terms() const { return m_terms; }

private:
    std::string                                         m_name;
    SigType                                             m_sigType;
    std::vector<Term>                                   m_terms;
};

} // namespace core
