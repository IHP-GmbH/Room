#pragma once

#include "property.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief Describes the original tool format/version for a cell view (format-neutral metadata).
 *****************************************************************************************/
class SourceInfo {
public:
    const std::string &                                 format() const { return m_format; }
    void                                                setFormat(std::string value) { m_format = std::move(value); }

    const std::string &                                 toolVersion() const { return m_toolVersion; }
    void                                                setToolVersion(std::string value) { m_toolVersion = std::move(value); }

    const std::string &                                 fileVersion() const { return m_fileVersion; }
    void                                                setFileVersion(std::string value) { m_fileVersion = std::move(value); }

    const std::string &                                 comments() const { return m_comments; }
    void                                                setComments(std::string value) { m_comments = std::move(value); }

    bool                                                empty() const;

private:
    std::string                                         m_format;
    std::string                                         m_toolVersion;
    std::string                                         m_fileVersion;
    std::string                                         m_comments;
};

constexpr const char *kSourceFormatKey = "core.source.format";
constexpr const char *kSourceToolVersionKey = "core.source.toolVersion";
constexpr const char *kSourceFileVersionKey = "core.source.fileVersion";
constexpr const char *kSourceCommentsKey = "core.source.comments";

void appendSourceInfoProperties(const SourceInfo &info, std::vector<Property> &properties);
SourceInfo extractSourceInfo(std::vector<Property> &properties);

} // namespace core
