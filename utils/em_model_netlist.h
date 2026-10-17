/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  Resolve layout-lookalike symbols (em.lookalike) to sibling *.emmodel.room
 *  Touchstone metadata for schematic netlisting (Qucs / Xschem).
 ************************************************************************/

#pragma once

#include "cell_content.h"
#include "property.h"

#include <string>
#include <vector>

namespace room {

/*! Published EM model + lookalike flag for Touchstone netlisting. */
struct EmModelNetlistInfo {
    std::string emModelPath;
    std::string snpPath;
    double      z0 = 50.0;
    int         portCount = 0;
    bool        lookalike = false;
};

bool contentHasLookalike(const CellContent &content);

/*! True when a *.symbol.room has property em.lookalike=1. */
bool symbolRoomIsLookalike(const std::string &symbolRoomPath);

/*! Rewrite …/cell.symbol.room → …/cell.emmodel.room (and similar known suffixes). */
std::string siblingRoomViewPath(const std::string &roomPath, const std::string &targetView);

/*! Load .emmodel.room payload next to \p symbolRoomPath; set lookalike from the symbol. */
bool tryLoadEmModelNetlist(const std::string &symbolRoomPath, EmModelNetlistInfo &out,
                           std::string *errorOut = nullptr);

/*!
 * Write S2Spice .SUBCKT for a lookalike cell next to an exported Xschem symbol.
 * Device name defaults to the cell name (matches @symname in the .sym format).
 * \return empty string on success; otherwise an error message.
 */
std::string writeLookalikeSpiceForSymbolRoom(const std::string &symbolRoomPath,
                                             const std::string &spiceOutPath,
                                             const std::string &deviceName = {});

/*! Apply Xschem netlist props for lookalike symbols (trailing ref gnd, no sch descent). */
void applyLookalikeXschemNetlistProps(std::vector<Property> &props);

} // namespace room
