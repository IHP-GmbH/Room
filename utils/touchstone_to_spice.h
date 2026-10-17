/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  Touchstone → SPICE .SUBCKT (S2Spice algorithm by Dan Dickey / Jim Mittel;
 *  Qucs-S port by Vadim Kuznetsov). Stdlib-only for Room / Xschem / Qucs.
 ************************************************************************/

#pragma once

#include <ostream>
#include <string>

namespace room {

/*! Convert a Touchstone S-parameter file to an ngspice .SUBCKT.
 *
 *  Ports are numbered 1..N plus a trailing reference node N+1 (tie to gnd on
 *  the instance, matching Qucs LibComp lookalike netlisting).
 *
 *  \param snpPath     path to .s1p / .s2p / …
 *  \param deviceName  .SUBCKT name (e.g. cell name or Lib_Cell)
 *  \param z0          reference impedance; if &lt; 0, use Z0 from the file header
 *  \param out         destination stream
 *  \param errorOut    optional human-readable error
 *  \return true on success
 */
bool convertTouchstoneToSpice(const std::string &snpPath, const std::string &deviceName, double z0,
                              std::ostream &out, std::string *errorOut = nullptr);

/*! Write convertTouchstoneToSpice output to \p spicePath. */
bool writeTouchstoneSpiceFile(const std::string &snpPath, const std::string &deviceName, double z0,
                              const std::string &spicePath, std::string *errorOut = nullptr);

} // namespace room
