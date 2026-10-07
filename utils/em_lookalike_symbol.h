/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace room {

/*! One EM port → symbol pin (Touchstone index = pinnumber). */
struct EmLookalikePort {
    std::string   name;
    std::uint16_t index = 0; //!< 1-based; written as pinnumber
    double        xUm = 0.0;
    double        yUm = 0.0;
    bool          hasPosition = false;
};

/*!
 * Layout geometry (µm) plus EM ports for a layout-lookalike Symbol view.
 * When outlinePolysUm is empty, a bbox rectangle is used as the body.
 */
struct EmLookalikeInput {
    std::string cellName;
    std::string outputPath; //!< `<cell>.symbol.room`

    double bboxLlxUm = 0.0;
    double bboxLlyUm = 0.0;
    double bboxUrxUm = 0.0;
    double bboxUryUm = 0.0;
    bool   hasBBox = false;

    /*! Optional lookalike body: each polygon is a ring of (x,y) in µm. */
    std::vector<std::vector<std::pair<double, double>>> outlinePolysUm;

    std::vector<EmLookalikePort> ports;
};

/*!
 * Writes a ViewType::Symbol ROOM file: scaled layout outline + pins.
 * Pin order / pinnumber follows EmLookalikePort::index (Touchstone).
 * \return Empty string on success; otherwise an error message.
 */
std::string writeLookalikeSymbolRoom(const EmLookalikeInput &in);

/*!
 * Fills bbox and outlinePolysUm from a `.layout.room` (top cell or sole cell).
 * Does not clear ports. Caps the number of copied shapes for dense layouts.
 * \return true when a usable bbox was obtained.
 */
bool fillLookalikeOutlineFromLayoutRoom(const std::string &layoutPath,
                                        const std::string &topCell,
                                        EmLookalikeInput &inout,
                                        std::string *error = nullptr);

} // namespace room
