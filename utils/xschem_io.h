#pragma once



#include "block.h"

#include "cell.h"

#include "cell_content.h"

#include "source_info.h"



#include <ostream>

#include <string>

#include <vector>



namespace core::xschem {



std::vector<std::string> readRecords(const std::string &path, std::vector<std::string> &errors);

void importRecords(const std::vector<std::string> &records, Cell &cell, CellContent &content,

                   std::vector<std::string> &warnings);

void exportRecords(std::ostream &out, const Cell &cell, const CellContent &content);



} // namespace core::xschem

