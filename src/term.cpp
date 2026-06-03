#include "term.h"

namespace cdb {

Term::Term(std::string name, std::uint32_t layerId, Point position)
    : name_(std::move(name)), layerId_(layerId), position_(position) {}

} // namespace cdb
