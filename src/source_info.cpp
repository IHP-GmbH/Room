#include "source_info.h"

#include <algorithm>

namespace core {
namespace {

void addProperty(std::vector<Property> &props, const std::string &name, const std::string &value)
{
    if (value.empty()) {
        return;
    }
    props.push_back({name, value});
}

void removeProperty(std::vector<Property> &props, const std::string &name)
{
    props.erase(std::remove_if(props.begin(), props.end(),
                             [&](const Property &prop) { return prop.name == name; }),
                props.end());
}

} // namespace

bool SourceInfo::empty() const
{
    return m_format.empty() && m_toolVersion.empty() && m_fileVersion.empty() && m_comments.empty();
}

void appendSourceInfoProperties(const SourceInfo &info, std::vector<Property> &properties)
{
    addProperty(properties, kSourceFormatKey, info.format());
    addProperty(properties, kSourceToolVersionKey, info.toolVersion());
    addProperty(properties, kSourceFileVersionKey, info.fileVersion());
    addProperty(properties, kSourceCommentsKey, info.comments());
}

SourceInfo extractSourceInfo(std::vector<Property> &properties)
{
    SourceInfo info;
    for (const Property &prop : properties) {
        if (prop.name == kSourceFormatKey) {
            info.setFormat(prop.value);
        } else if (prop.name == kSourceToolVersionKey) {
            info.setToolVersion(prop.value);
        } else if (prop.name == kSourceFileVersionKey) {
            info.setFileVersion(prop.value);
        } else if (prop.name == kSourceCommentsKey) {
            info.setComments(prop.value);
        }
    }
    removeProperty(properties, kSourceFormatKey);
    removeProperty(properties, kSourceToolVersionKey);
    removeProperty(properties, kSourceFileVersionKey);
    removeProperty(properties, kSourceCommentsKey);
    return info;
}

} // namespace core
