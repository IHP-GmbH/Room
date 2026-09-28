#pragma once

#include "block.h"
#include "instance.h"
#include "primitive_resolver.h"

namespace room {

bool isAnonymousNetLabel(const std::string &label);

/*! Propagate Port / supply / symbol-pin names onto wire shape labels before netlist export. */
void propagateNetNames(Block &block, const PrimitiveResolver *resolver, double dbuPerEditorUnit);

/*! Store analogLib-canonical qucs.type / core.primitive; editors map to tool-native symbols on read. */
void canonicalizeBlockPrimitives(Block &block, const PrimitiveResolver *resolver);

} // namespace room
