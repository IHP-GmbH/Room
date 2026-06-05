#pragma once

#include <cstdint>

namespace core {

class Box {
public:
    std::int64_t llx = 0;
    std::int64_t lly = 0;
    std::int64_t urx = 0;
    std::int64_t ury = 0;

    Box() = default;
    Box(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
        : llx(a), lly(b), urx(c), ury(d) {}

    void expand(std::int64_t x, std::int64_t y);
    void expand(const Box &other);
    bool empty() const;
};

} // namespace core
