#!/usr/bin/env python3
"""Port LibMan oasReader.cpp from Qt to std C++ for CORE."""

from __future__ import annotations

import pathlib
import re
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = pathlib.Path(r"C:\Users\anton\Documents\LibMan\oas\oasReader.cpp")
if not SRC.is_file():
    SRC = ROOT / "utils" / "oas" / "oasReader.cpp"
DST = ROOT / "utils" / "oas_reader.cpp"

body = SRC.read_text(encoding="utf-8")

body = re.sub(r'#include\s+"oasReader\.h"\s*\n', "", body)
for inc in ("QFile", "QDebug", "QFileInfo", "QByteArray", "QElapsedTimer"):
    body = re.sub(rf"#include\s*<{inc}>\s*\n", "", body)

body = re.sub(r"^\s*qDebug\(\)[^\n]*\n", "", body, flags=re.MULTILINE)
body = re.sub(r"^\s*qDebug\(\)\.noquote\(\)[^\n]*\n", "", body, flags=re.MULTILINE)

body = re.sub(
    r"static inline QString hexDumpN\([\s\S]*?^}\s*\n",
    "",
    body,
    count=1,
    flags=re.MULTILINE,
)
body = re.sub(
    r"static inline QString hexAround\([\s\S]*?^}\s*\n",
    "",
    body,
    count=1,
    flags=re.MULTILINE,
)
body = re.sub(
    r"static QString dumpU16\([\s\S]*?^}\s*\n",
    "",
    body,
    count=1,
    flags=re.MULTILINE,
)

body = re.sub(
    r"oasReader::oasReader\(const QString &fileName\)\s*:[\s\S]*?\n}\s*\n",
    "",
    body,
    count=1,
)
body = re.sub(
    r"QStringList oasReader::getErrors\(void\) const\s*\{[\s\S]*?\n}\s*\n",
    "",
    body,
    count=1,
)
body = re.sub(
    r"QStringList oasReader::getErrors\(\) const\s*\{[\s\S]*?\n}\s*\n",
    "",
    body,
    count=1,
)
body = re.sub(
    r"bool oasReader::readHierarchy\(LayoutHierarchy &out\)\s*\{[\s\S]*?\n}\s*\n?\Z",
    "",
    body,
    count=1,
)

replacements = [
    ("LayoutHierarchy", "OasHierarchy"),
    ("QStringList", "StringList"),
    ("QHash<quint64, QString>", "CellNameTable"),
    ("QSet<QString>", "CellNameSet"),
    ("QByteArray", "ByteBuffer"),
    ("QString", "std::string"),
    ("quint64", "std::uint64_t"),
    ("qint64", "std::int64_t"),
    ("uchar", "std::uint8_t"),
    ("QElapsedTimer guardTimer;", "std::chrono::steady_clock::time_point guardStart{};"),
    ("st.guardTimer.invalidate();", "st.guardStart = {};"),
    ("st.guardTimer.isValid()", "false"),
    ("st.guardTimer.start();", "(void)st.guardStart;"),
    ("st.guardTimer.elapsed()", "0"),
    (".isEmpty()", ".empty()"),
    ("out.topCells <<", "out.topCells.push_back("),
    ("] << placedCell;", "].push_back(placedCell);"),
    ("out.topCells.sort();", "std::sort(out.topCells.begin(), out.topCells.end());"),
    ("it.value()", "it->second"),
    ("st.cellNameByRef.value(", "mapValue(st.cellNameByRef, "),
    ("raw.constData()", "raw.data()"),
    ("dec.constData()", "dec.data()"),
    ("QString::fromLatin1(b)", "bytesToLatin1(b)"),
    ("QString::fromLatin1(raw.constData(), raw.size())", "bytesToLatin1(raw)"),
    ("QString::fromUtf8(raw.constData(), raw.size())", "bytesToUtf8(raw)"),
    ("QString::fromUtf8(b.constData(), b.size())", "bytesToUtf8(b)"),
    ("for (QChar ch : s)", "for (unsigned char ch : s)"),
    ("ushort u = ch.unicode();", "unsigned char u = ch;"),
    ("errors <<", "errors.push_back("),
    ("referenced.contains(cname)", "referenced.find(cname) == referenced.end()"),
]

for old, new in replacements:
    body = body.replace(old, new)

body = body.replace(
    "std::string nameUtf8 = bytesToUtf8(raw);\n"
    "        std::string nameLat1 = bytesToLatin1(raw);\n"
    "        std::string name = nameUtf8.contains(QChar(0xFFFD)) ? nameLat1 : nameUtf8;",
    "const std::string name = decodeOasName(raw);",
)
body = body.replace(
    "std::string utf8 = bytesToUtf8(raw);\n"
    "        std::string lat1 = bytesToLatin1(raw);\n"
    "        std::string name = utf8.contains(QChar(0xFFFD)) ? lat1 : utf8;",
    "const std::string name = decodeOasName(raw);",
)
body = body.replace(
    "out = ByteBuffer(reinterpret_cast<const char*>(c.p), int(n));",
    "out.assign(c.p, c.p + static_cast<std::size_t>(n));",
)
body = body.replace("c.p += int(n);", "c.p += static_cast<std::ptrdiff_t>(n);")
body = body.replace("out.resize(expectedOutLen);", "out.resize(static_cast<std::size_t>(expectedOutLen));")
body = body.replace("StringList &errors", "std::vector<std::string> &errors")

# Strip QString debug fragments inside disabled OAS_DEBUG blocks.
body = re.sub(
    r'(^\s*)QString\("[^"]*"\)(\s*\.arg\([^\n]+\))+\s*;\s*\n',
    "",
    body,
    flags=re.MULTILINE,
)
body = re.sub(
    r'^\s*\.arg\([^\n]+\)\s*\n',
    "",
    body,
    flags=re.MULTILINE,
)
body = re.sub(
    r'^\s*\.arg\([^\n]+\)(\s*\.arg\([^\n]+\))*\s*;\s*\n',
    "",
    body,
    flags=re.MULTILINE,
)

preamble = textwrap.dedent(
    '''
    #include "oas_reader.h"

    #include <algorithm>
    #include <chrono>
    #include <cstdint>
    #include <cstring>
    #include <fstream>
    #include <string>
    #include <unordered_map>
    #include <unordered_set>
    #include <utility>
    #include <vector>

    #include <zlib.h>

    namespace core {
    namespace {

    using StringList = std::vector<std::string>;
    using ByteBuffer = std::vector<std::uint8_t>;
    using CellNameTable = std::unordered_map<std::uint64_t, std::string>;
    using CellNameSet = std::unordered_set<std::string>;

    constexpr char kUtf8Replacement[] = "\\xEF\\xBF\\xBD";

    static std::string bytesToLatin1(const ByteBuffer &b)
    {
        return std::string(reinterpret_cast<const char *>(b.data()), b.size());
    }

    static std::string bytesToUtf8(const ByteBuffer &b)
    {
        return bytesToLatin1(b);
    }

    static std::string decodeOasName(const ByteBuffer &raw)
    {
        const std::string utf8 = bytesToUtf8(raw);
        if (utf8.find(kUtf8Replacement) != std::string::npos) {
            return bytesToLatin1(raw);
        }
        return utf8;
    }

    static const std::string &mapValue(const CellNameTable &table, std::uint64_t key)
    {
        static const std::string kEmpty;
        const auto it = table.find(key);
        return it == table.end() ? kEmpty : it->second;
    }

    '''
).lstrip("\n")

epilogue = textwrap.dedent(
    '''

    } // namespace

    OasReader::OasReader(std::string fileName)
        : fileName_(std::move(fileName))
    {
    }

    bool OasReader::readHierarchy(OasHierarchy &out)
    {
        out.topCells.clear();
        out.children.clear();
        out.allCells.clear();
        errors_.clear();

        if (fileName_.empty()) {
            errors_.push_back("Empty OASIS filename.");
            return false;
        }

        std::ifstream input(fileName_, std::ios::binary);
        if (!input) {
            errors_.push_back("Failed to open OASIS for read: '" + fileName_ + "'");
            return false;
        }

        input.seekg(0, std::ios::end);
        const std::streamoff szOff = input.tellg();
        if (szOff < 16) {
            errors_.push_back("OASIS file too small: '" + fileName_ + "'");
            return false;
        }
        const auto sz = static_cast<std::size_t>(szOff);
        input.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> fileBytes(sz);
        input.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(sz));
        if (!input) {
            errors_.push_back("Failed to read OASIS: '" + fileName_ + "'");
            return false;
        }

        const std::uint8_t *base = fileBytes.data();
        const std::uint8_t *const fileEnd = base + sz;

        static const char magic[] = "%SEMI-OASIS";
        if (std::memcmp(base, magic, sizeof(magic) - 1) != 0) {
            errors_.push_back("Not an OASIS file (missing %SEMI-OASIS magic).");
            return false;
        }

        const std::uint8_t *p = base + (sizeof(magic) - 1);
        while (p < fileEnd && (*p == '\\r' || *p == '\\n')) {
            ++p;
        }

        OasCursor c;
        c.p = p;
        c.end = fileEnd;

        OasParseState st;
        st.fileBase = base;
        st.fileEnd = fileEnd;

    #if OAS_GUARD
        st.recordCount = 0;
        st.stallCount = 0;
        st.lastOff = static_cast<std::uint64_t>(c.p - st.fileBase);
        st.lastGoodOff = st.lastOff;
        st.guardStart = {};
    #endif

        if (!parseBuffer(c, out, errors_, st)) {
            return false;
        }

        CellNameSet referenced;
        for (const auto &entry : out.children) {
            for (const std::string &child : entry.second) {
                referenced.insert(child);
            }
        }

        out.topCells.clear();
        out.topCells.reserve(out.allCells.size());
        for (const std::string &cellName : out.allCells) {
            if (referenced.find(cellName) == referenced.end()) {
                out.topCells.push_back(cellName);
            }
        }
        std::sort(out.topCells.begin(), out.topCells.end());
        return true;
    }

    } // namespace core
    '''
).lstrip("\n")

out_text = preamble + body + epilogue
out_text = out_text.replace("//#define OAS_DEBUG 1", "#define OAS_DEBUG 0")
out_text = out_text.replace("#define OAS_TRACE 1", "#define OAS_TRACE 0")

DST.write_text(out_text, encoding="utf-8", newline="\n")
print(f"Wrote {DST} ({out_text.count(chr(10))} lines)")
