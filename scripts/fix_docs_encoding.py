#!/usr/bin/env python3
"""Fix corrupted Unicode replacement chars in docs/html/index.html."""
import re
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "docs" / "html" / "index.html"
t = p.read_text(encoding="utf-8", errors="replace")

replacements = [
    ("CORE \ufffd Documentation", "CORE &mdash; Documentation"),
    ("Common Open Repository for EDA \ufffd Cap", "Common Open Repository for EDA &middot; Cap"),
    ("Proto \ufffd C++17", "Proto &middot; C++17"),
    (
        "CORE \ufffd Common Open Repository for EDA \ufffd is",
        "CORE &mdash; Common Open Repository for EDA &mdash; is",
    ),
    ("CellContent.payload</code> \ufffd not", "CellContent.payload</code> &mdash; not"),
    (
        '<a href="example.html">GDS layout import</a> \ufffd',
        '<a href="example.html">GDS layout import</a> &middot;',
    ),
    (
        "Qucs schematic import</a> \ufffd",
        "Qucs schematic import</a> &middot;",
    ),
    (
        ".schematic.core</code> \ufffd see",
        ".schematic.core</code> &mdash; see",
    ),
    ("(Database, Cell, Shape, \ufffd)", "(Database, Cell, Shape, &hellip;)"),
    ("box.cpp</code>, \ufffd</td>", "box.cpp</code>, &hellip;</td>"),
]
for old, new in replacements:
    t = t.replace(old, new)

t = re.sub(r"(\s+)\? (LayerSpec|LibIndex|viewType|ViewPayload|Shape)", r"\1-&gt; \2", t)
t = t.replace("\ufffd    +-- layers", "|    +-- layers")
t = t.replace("\ufffd    +-- block", "|    +-- block")
t = t.replace(
    "<code>Ctrl+Shift+B</code> ? Example",
    "<code>Ctrl+Shift+B</code> &rarr; Example",
)

remaining = t.count("\ufffd")
if remaining:
    raise SystemExit(f"still {remaining} replacement characters in index.html")

p.write_text(t, encoding="utf-8", newline="\n")
print("fixed", p)
