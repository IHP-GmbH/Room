#pragma once

#include "property.h"

#include <optional>
#include <string>
#include <vector>

namespace room {

bool looksLikeQucsEndTimeExpr(const std::string &s);
std::vector<std::string> tokenizePulseBody(const std::string &val);

// Legacy Qucs stored SPICE PER in param.3 (T2). Symmetric bench: PW = PER - 2*TD.
std::string pulseWidthFromLegacyPeriod(const std::string &per, const std::string &td);

// Parse SPICE time token (2u, 10n, 1ms) to seconds.
std::optional<double> parseSpiceTimeValue(const std::string &text);

// Build SPICE PULSE(...) for Xschem value= from Qucs Vpulse param.N and optional canonical value.
// Returns nullopt when required fields are missing (no academy-specific defaults).
std::optional<std::string> buildPulseSpiceValue(const std::vector<Property> &props);

} // namespace room
