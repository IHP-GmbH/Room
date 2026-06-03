#pragma once

#include "term.h"
#include "types.h"

#include <string>
#include <vector>

namespace cdb {

class Net {
public:
    explicit Net(std::string name, SigType sigType = SigType::Signal);

    const std::string &name() const { return name_; }
    SigType sigType() const { return sigType_; }
    void setSigType(SigType type) { sigType_ = type; }

    std::vector<Term> &terms() { return terms_; }
    const std::vector<Term> &terms() const { return terms_; }

private:
    std::string name_;
    SigType sigType_;
    std::vector<Term> terms_;
};

} // namespace cdb
