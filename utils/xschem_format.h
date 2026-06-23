#pragma once



#include "enums.h"



#include <string>



namespace core::xschem {



constexpr const char *kPropSourcePath = "editor.sourcePath";

constexpr const char *kPropSourceExt = "editor.sourceExt";

constexpr const char *kSectionPrefix = "section.";



ViewType viewTypeForExtension(const std::string &extension);

std::string extensionForViewType(ViewType viewType);



Orient orientFromXschem(int rotate, int mirror);

void xschemFromOrient(Orient orient, int &rotate, int &mirror);



} // namespace core::xschem

