#include "box.h"

#include <algorithm>

namespace core {

Box::Box(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
    : llx(a), lly(b), urx(c), ury(d), m_valid(a <= c && b <= d)
{
}

void Box::expand(std::int64_t x, std::int64_t y)
{
    if (!m_valid) {
        llx = urx = x;
        lly = ury = y;
        m_valid = true;
        return;
    }
    llx = std::min(llx, x);
    lly = std::min(lly, y);
    urx = std::max(urx, x);
    ury = std::max(ury, y);
}

void Box::expand(const Box &other)
{
    if (other.empty()) {
        return;
    }
    if (!m_valid) {
        llx = other.llx;
        lly = other.lly;
        urx = other.urx;
        ury = other.ury;
        m_valid = true;
        return;
    }
    llx = std::min(llx, other.llx);
    lly = std::min(lly, other.lly);
    urx = std::max(urx, other.urx);
    ury = std::max(ury, other.ury);
}

bool Box::empty() const
{
    return !m_valid;
}

} // namespace core
