#include "box.h"

#include <algorithm>

namespace core {

void Box::expand(std::int64_t x, std::int64_t y)
{
    if (empty()) {
        llx = urx = x;
        lly = ury = y;
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
    if (empty()) {
        llx = other.llx;
        lly = other.lly;
        urx = other.urx;
        ury = other.ury;
        return;
    }
    llx = std::min(llx, other.llx);
    lly = std::min(lly, other.lly);
    urx = std::max(urx, other.urx);
    ury = std::max(ury, other.ury);
}

bool Box::empty() const
{
    return (llx >= urx) || (lly >= ury);
}

} // namespace core
