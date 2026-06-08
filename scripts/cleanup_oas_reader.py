#!/usr/bin/env python3
import pathlib
import re

path = pathlib.Path(__file__).resolve().parents[1] / "utils" / "oas_reader.cpp"
body = path.read_text(encoding="utf-8")

body = re.sub(r"#if OAS_DEBUG.*?#endif\s*\n", "", body, flags=re.DOTALL)
body = re.sub(r"#ifdef OAS_DEBUG.*?#endif\s*\n", "", body, flags=re.DOTALL)

body = re.sub(
    r"/\*!\*+[\s\S]*?bool oasReader::readHierarchy\(OasHierarchy[\s\S]*?return true;\s*\n}\s*\n",
    "",
    body,
    count=1,
)

replacements = [
    (
        "std::string nameUtf8 = std::string::fromUtf8(raw.data(), raw.size());\n"
        "        std::string nameLat1 = std::string::fromLatin1(raw.data(), raw.size());\n"
        "        std::string name = nameUtf8.contains(QChar(0xFFFD)) ? nameLat1 : nameUtf8;",
        "const std::string name = decodeOasName(raw);",
    ),
    (
        "std::string utf8 = std::string::fromUtf8(raw.data(), raw.size());\n"
        "        std::string lat1 = std::string::fromLatin1(raw.data(), raw.size());\n"
        "        std::string name = utf8.contains(QChar(0xFFFD)) ? lat1 : utf8;",
        "const std::string name = decodeOasName(raw);",
    ),
    ("dec.constData()", "dec.data()"),
    ("subSt.guardTimer.invalidate()", "subSt.guardStart = {}"),
    ("referenced.contains(cname)", "referenced.find(cname) == referenced.end()"),
]

for old, new in replacements:
    body = body.replace(old, new)

# Remove orphan .arg lines if any remain
body = re.sub(r"^\s*\.arg\([^\n]*\n", "", body, flags=re.MULTILINE)

path.write_text(body, encoding="utf-8", newline="\n")
print(f"cleaned {path}, {body.count(chr(10))} lines")
