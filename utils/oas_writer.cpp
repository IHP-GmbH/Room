#include "oas_writer.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace core {
namespace {

void appendUInt(std::vector<std::uint8_t> &buf, std::uint64_t value)
{
    do {
        std::uint8_t b = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            b |= 0x80;
        }
        buf.push_back(b);
    } while (value != 0);
}

void appendOasString(std::vector<std::uint8_t> &buf, const std::string &s)
{
    appendUInt(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

bool writeBytes(const std::string &path, const std::vector<std::uint8_t> &data, std::vector<std::string> &errors)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errors.push_back("Failed to open file '" + path + "'");
        return false;
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char *>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(out);
}

} // namespace

OasWriter::OasWriter(std::string fileName)
    : m_fileName(std::move(fileName))
{
}

void OasWriter::createMinimalFile(const std::string &cellName)
{
    m_errors.clear();

    std::vector<std::uint8_t> fileData;
    const char magic[] = "%SEMI-OASIS\r\n";
    fileData.insert(fileData.end(), magic, magic + sizeof(magic) - 1);

    std::vector<std::uint8_t> startRec;
    appendUInt(startRec, 1);
    appendOasString(startRec, "1.0");
    appendUInt(startRec, 0);
    appendUInt(startRec, 1000);
    appendUInt(startRec, 0);
    for (int i = 0; i < 12; ++i) {
        appendUInt(startRec, 0);
    }
    fileData.insert(fileData.end(), startRec.begin(), startRec.end());

    std::vector<std::uint8_t> cellNameRec;
    appendUInt(cellNameRec, 3);
    appendOasString(cellNameRec, cellName);
    fileData.insert(fileData.end(), cellNameRec.begin(), cellNameRec.end());

    std::vector<std::uint8_t> cellRec;
    appendUInt(cellRec, 13);
    appendUInt(cellRec, 0);
    fileData.insert(fileData.end(), cellRec.begin(), cellRec.end());

    std::vector<std::uint8_t> endRec;
    appendUInt(endRec, 2);
    constexpr std::uint64_t padLen = 252;
    appendUInt(endRec, padLen);
    endRec.insert(endRec.end(), padLen, 0);
    appendUInt(endRec, 0);
    fileData.insert(fileData.end(), endRec.begin(), endRec.end());

    writeBytes(m_fileName, fileData, m_errors);
}

} // namespace core
