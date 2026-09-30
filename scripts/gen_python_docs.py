from pathlib import Path

ROOT = Path(r"C:\Users\anton\Documents\CommonDB\docs\html")


def sidebar(active: str) -> str:
    items = {
        "overview": ("python.html", "Overview"),
        "ex_gds": ("python_example.html", "GDS import example"),
        "ex_qucs": ("python_example_qucs.html", "Qucs import example"),
        "ex_xschem": ("python_example_xschem.html", "Xschem import example"),
        "classes": ("python_classes.html", "Class index"),
        "box": ("python_types.html#box", "Box"),
        "rect": ("python_types.html#rect", "Rect"),
        "inst": ("python_types.html#instance", "Instance"),
        "layer": ("python_types.html#layer", "Layer"),
        "fileinfo": ("python_types.html#fileinfo", "FileInfo"),
        "enums": ("python_types.html#enums", "Enums"),
        "database": ("python_database.html", "Database"),
        "lib": ("python_lib.html", "Lib"),
        "cell": ("python_cell.html#cell", "Cell"),
        "content": ("python_cell.html#cellcontent", "CellContent"),
        "block": ("python_block.html", "Block"),
        "shape": ("python_shape.html", "Shape"),
        "sniff": ("python_types.html#sniff", "File summary / sniff"),
        "gds": ("python_gds.html", "GDS import/export"),
        "qucs": ("python_qucs.html", "Qucs import/export"),
        "xschem": ("python_xschem.html", "Xschem import/export"),
        "oas": ("python_oas.html", "OASIS import/export"),
    }

    def li(key: str) -> str:
        href, label = items[key]
        cls = ' class="active"' if key == active else ""
        return f'        <li><a href="{href}"{cls}>{label}</a></li>'

    gs = "\n".join(li(k) for k in ("overview", "ex_gds", "ex_qucs", "ex_xschem", "classes"))
    types = "\n".join(li(k) for k in ("box", "rect", "inst", "layer", "fileinfo", "enums"))
    room = "\n".join(li(k) for k in ("database", "lib", "cell", "content", "block", "shape", "inst", "sniff"))
    util = "\n".join(li(k) for k in ("gds", "qucs", "xschem", "oas"))
    return f"""    <aside class="sidebar">
      <h3>Getting Started</h3>
      <ul>
{gs}
      </ul>
      <h3>Types</h3>
      <ul>
{types}
      </ul>
      <h3>ROOM</h3>
      <ul>
{room}
      </ul>
      <h3>Utilities</h3>
      <ul>
{util}
      </ul>
    </aside>"""


def page(title: str, active: str, body: str, toc: str = "") -> str:
    if toc:
        toc_html = f"""    <aside class="page-toc">
      <h3>On this page</h3>
      <ul>
{toc}
      </ul>
    </aside>"""
        wrap_open = '<div class="content-with-toc">'
        wrap_close = "</div>"
    else:
        toc_html = ""
        wrap_open = ""
        wrap_close = ""
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{title} | ROOM Python</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <header class="site-header">
    <a class="logo" href="index.html">ROOM</a>
    <nav>
      <a href="index.html">C++</a>
      <a href="python.html">Python</a>
      <a href="python_example.html">Example</a>
      <a href="python_classes.html">All Classes</a>
    </nav>
  </header>
  <div class="layout">
{sidebar(active)}
    {wrap_open}
    <main class="content">
{body}
    </main>
{toc_html}
    {wrap_close}
  </div>
</body>
</html>
"""


pages = {
    "python_classes.html": (
        "Python Class Index",
        "classes",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Class index</p>
      <h1>Python class index</h1>
      <p class="subtitle">Same model as the C++ API &mdash; available after <code>import room</code>.</p>
      <h2>Types</h2>
      <ul class="class-list">
        <li><a href="python_types.html#box">Box</a></li>
        <li><a href="python_types.html#rect">Rect</a></li>
        <li><a href="python_types.html#instance">Instance</a></li>
        <li><a href="python_types.html#layer">Layer</a></li>
        <li><a href="python_types.html#fileinfo">FileInfo</a></li>
        <li><a href="python_types.html#enums">Enums</a></li>
      </ul>
      <h2>ROOM</h2>
      <ul class="class-list">
        <li><a href="python_database.html">Database</a></li>
        <li><a href="python_lib.html">Lib</a></li>
        <li><a href="python_cell.html#cell">Cell</a></li>
        <li><a href="python_cell.html#cellcontent">CellContent</a></li>
        <li><a href="python_block.html">Block</a></li>
        <li><a href="python_shape.html">Shape</a></li>
        <li><a href="python_types.html#sniff">File summary / sniff</a></li>
      </ul>
      <h2>Utilities</h2>
      <ul class="class-list">
        <li><a href="python_gds.html">GDS import/export</a></li>
        <li><a href="python_qucs.html">Qucs import/export</a></li>
        <li><a href="python_xschem.html">Xschem import/export</a></li>
        <li><a href="python_oas.html">OASIS import/export</a></li>
      </ul>
      <h2>Module helpers</h2>
      <table class="memlist">
        <tr><td><code>room.open(path)</code></td><td>Load <code>.room</code>.</td></tr>
        <tr><td><code>room.create()</code></td><td>Empty database.</td></tr>
        <tr><td><code>room.from_gds / from_qucs / from_xschem / from_oas</code></td><td>Importers.</td></tr>
        <tr><td><code>room.sniff(path)</code></td><td>Header-only metadata.</td></tr>
        <tr><td><code>room.has_oas()</code></td><td>OASIS availability.</td></tr>
      </table>""",
        "",
    ),
    "python_database.html": (
        "Database (Python)",
        "database",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; ROOM &raquo; Database</p>
      <h1 class="title">Database Class</h1>
      <p class="brief">Root object of a ROOM design. Python counterpart of C++ <a href="database.html"><code>room::Database</code></a>.</p>
      <table class="props">
        <tr><th>Import:</th><td><code>import room</code></td></tr>
        <tr><th>Factories:</th><td><code>room.open</code>, <code>room.create</code>, <code>room.from_gds</code>, &hellip;</td></tr>
      </table>
      <h2 id="factories">Factories</h2>
      <table class="memlist">
        <tr><td><code>Database.create()</code> / <code>room.create()</code></td><td>Empty database.</td></tr>
        <tr><td><code>Database.open(path)</code> / <code>room.open(path)</code></td><td>Load <code>.room</code>.</td></tr>
        <tr><td><code>from_gds / from_qucs / from_xschem / from_oas</code></td><td>Import foreign formats.</td></tr>
      </table>
      <h2 id="props">Properties</h2>
      <table class="memlist">
        <tr><td><code>version</code>, <code>generator</code>, <code>technology</code></td><td>File metadata (str).</td></tr>
        <tr><td><code>lib_name</code></td><td>Library name (see <a href="python_lib.html">Lib</a>).</td></tr>
        <tr><td><code>file_view</code></td><td><code>ViewType</code>.</td></tr>
        <tr><td><code>cells</code></td><td><code>list[Cell]</code>.</td></tr>
        <tr><td><code>layers</code></td><td><code>list[Layer]</code>.</td></tr>
        <tr><td><code>summary_cell_count</code>, <code>summary_primary_cell</code></td><td>Header summary.</td></tr>
      </table>
      <h2 id="methods">Methods</h2>
      <table class="memlist">
        <tr><td><code>cell_names()</code></td><td>All cell names.</td></tr>
        <tr><td><code>find_cell(name)</code></td><td><code>Cell | None</code>.</td></tr>
        <tr><td><code>get_or_create_cell(name)</code></td><td>Create if missing.</td></tr>
        <tr><td><code>add_layer(...)</code></td><td>Append layer; returns index.</td></tr>
        <tr><td><code>info()</code></td><td>Summary <code>dict</code>.</td></tr>
        <tr><td><code>save(path, view="layout")</code></td><td>Write <code>.room</code>.</td></tr>
        <tr><td><code>to_gds / to_qucs / to_xschem / to_oas</code></td><td>Export.</td></tr>
        <tr><td><code>close()</code></td><td>Free handle (<code>with</code> supported).</td></tr>
      </table>
      <h2>Example</h2>
      <pre class="example"><code>import room
with room.open("design.room") as db:
    print(db.info())
    print(db.find_cell(db.cell_names()[0]).layout.bbox)</code></pre>""",
        """        <li><a href="#factories">Factories</a></li>
        <li><a href="#props">Properties</a></li>
        <li><a href="#methods">Methods</a></li>""",
    ),
    "python_lib.html": (
        "Lib (Python)",
        "lib",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; ROOM &raquo; Lib</p>
      <h1 class="title">Lib</h1>
      <p class="brief">No separate <code>Lib</code> class in Python. Library fields live on <a href="python_database.html"><code>Database</code></a> (same data as C++ <a href="lib.html"><code>room::Lib</code></a>).</p>
      <table class="members">
        <tr><th>C++</th><th>Python</th></tr>
        <tr><td><code>db.lib().name()</code></td><td><code>db.lib_name</code></td></tr>
        <tr><td><code>db.lib().cells()</code></td><td><code>db.cells</code> / <code>db.cell_names()</code></td></tr>
        <tr><td><code>db.lib().layers()</code></td><td><code>db.layers</code> / <code>db.add_layer(...)</code></td></tr>
        <tr><td><code>findCell / getOrCreateCell</code></td><td><code>db.find_cell</code> / <code>db.get_or_create_cell</code></td></tr>
      </table>
      <pre class="example"><code>db.lib_name = "stdcells"
for layer in db.layers:
    print(layer.layer_num, layer.data_type, layer.name)
cell = db.get_or_create_cell("INVX1")</code></pre>""",
        "",
    ),
    "python_cell.html": (
        "Cell / CellContent (Python)",
        "cell",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; ROOM &raquo; Cell</p>
      <h1 class="title" id="cell">Cell Class</h1>
      <p class="brief">Named design cell. Counterpart of C++ <a href="cell.html"><code>room::Cell</code></a>.</p>
      <table class="memlist">
        <tr><td><code>name</code></td><td>Cell name (str).</td></tr>
        <tr><td><code>contents</code></td><td><code>list[CellContent]</code>.</td></tr>
        <tr><td><code>layout</code></td><td>Layout <code>CellContent</code> or <code>None</code>.</td></tr>
        <tr><td><code>content(view="layout")</code></td><td>Existing view or <code>None</code>.</td></tr>
        <tr><td><code>ensure_content(view="layout", dbu_per_micron=1000.0)</code></td><td>Get or create view.</td></tr>
      </table>
      <h1 class="title" id="cellcontent">CellContent Class</h1>
      <p class="brief">One view body. Counterpart of C++ <a href="cellcontent.html"><code>room::CellContent</code></a>.</p>
      <table class="memlist">
        <tr><td><code>view</code></td><td><code>ViewType</code>.</td></tr>
        <tr><td><code>dbu_per_micron</code></td><td>float.</td></tr>
        <tr><td><code>bbox</code></td><td><a href="python_types.html#box">Box</a>.</td></tr>
        <tr><td><code>shapes</code></td><td>See <a href="python_shape.html">Shape</a>.</td></tr>
        <tr><td><code>instances</code></td><td><code>list[Instance]</code>.</td></tr>
        <tr><td><code>add_rect(...)</code> / <code>add_instance(...)</code></td><td>Mutators.</td></tr>
      </table>
      <pre class="example"><code>cell = db.find_cell("TOP")
layout = cell.ensure_content("layout")
print(layout.bbox, len(layout.shapes), len(layout.instances))</code></pre>""",
        "",
    ),
    "python_block.html": (
        "Block (Python)",
        "block",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; ROOM &raquo; Block</p>
      <h1 class="title">Block</h1>
      <p class="brief">Block topology is exposed on <a href="python_cell.html#cellcontent"><code>CellContent</code></a> (same data as C++ <a href="block.html"><code>room::Block</code></a>).</p>
      <table class="members">
        <tr><th>C++</th><th>Python</th></tr>
        <tr><td><code>content.block().shapes()</code></td><td><code>content.shapes</code></td></tr>
        <tr><td><code>content.block().instances()</code></td><td><code>content.instances</code></td></tr>
        <tr><td><code>content.block().bbox()</code></td><td><code>content.bbox</code></td></tr>
        <tr><td><code>block().nets()</code></td><td><em>not yet exposed</em></td></tr>
      </table>""",
        "",
    ),
    "python_shape.html": (
        "Shape (Python)",
        "shape",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; ROOM &raquo; Shape</p>
      <h1 class="title">Shape</h1>
      <p class="brief">Rectangles as <a href="python_types.html#rect"><code>room.Rect</code></a>; other kinds currently as <code>dict</code> with <code>type</code> / <code>index</code>.</p>
      <table class="memlist">
        <tr><td><code>content.shapes</code></td><td><code>list[Rect | dict]</code></td></tr>
        <tr><td><code>content.add_rect(layer_id, llx, lly, urx, ury)</code></td><td>Append rectangle.</td></tr>
      </table>
      <pre class="example"><code>for s in layout.shapes:
    if isinstance(s, room.Rect):
        print(s.layer_id, s.llx, s.lly, s.urx, s.ury)
    else:
        print(s["type"], s["index"])</code></pre>""",
        "",
    ),
    "python_types.html": (
        "Types &amp; Enums (Python)",
        "box",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Types</p>
      <h1 class="title">Types &amp; enums</h1>
      <div class="fn-block" id="sniff">
        <h3 class="fn-title">room.sniff(path) &rarr; FileInfo</h3>
        <p class="fn-desc">Header-only read. See C++ <a href="filesummary.html">sniffRoomFile</a>.</p>
      </div>
      <div class="fn-block" id="has_oas">
        <h3 class="fn-title">room.has_oas() &rarr; bool</h3>
        <p class="fn-desc">Whether OASIS support is linked into <code>room_c</code>.</p>
      </div>
      <h2 id="fileinfo">FileInfo</h2>
      <table class="members">
        <tr><th>Field</th><th>Type</th></tr>
        <tr><td>version, generator, technology, lib_name, primary_cell</td><td>str</td></tr>
        <tr><td>view</td><td>ViewType</td></tr>
        <tr><td>cell_count</td><td>int</td></tr>
      </table>
      <h2 id="box">Box</h2>
      <p><code>llx, lly, urx, ury: int</code>, <code>empty: bool</code>.</p>
      <h2 id="rect">Rect</h2>
      <p><code>layer_id, llx, lly, urx, ury: int</code>.</p>
      <h2 id="instance">Instance</h2>
      <table class="members">
        <tr><th>Field</th><th>Type</th></tr>
        <tr><td>cell_name</td><td>str</td></tr>
        <tr><td>x, y</td><td>int</td></tr>
        <tr><td>orient</td><td>Orient</td></tr>
        <tr><td>mag</td><td>float</td></tr>
      </table>
      <h2 id="layer">Layer</h2>
      <p>Library layer (C++ LayerSpec): <code>layer_num</code>, <code>data_type</code>, <code>name</code>, <code>purpose</code>.</p>
      <h2 id="enums">Enums</h2>
      <table class="members">
        <tr><th>Enum</th><th>Values</th></tr>
        <tr><td>ViewType</td><td>LAYOUT, SCHEMATIC, SYMBOL, ABSTRACT</td></tr>
        <tr><td>Orient</td><td>R0, R90, R180, R270, MY, MX, MX90, MY90</td></tr>
        <tr><td>LayerPurpose</td><td>DRAWING, PIN, LABEL, BOUNDARY, BLOCKAGE, WIRE, FILL, OTHER</td></tr>
        <tr><td>ShapeType</td><td>RECT, POLYGON, PATH, TEXT, ARC</td></tr>
      </table>""",
        "",
    ),
    "python_example.html": (
        "GDS import example (Python)",
        "ex_gds",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; GDS import example</p>
      <h1>GDS import example</h1>
      <pre class="example"><code>import room

with room.from_gds("testdata/sample.gds") as db:
    print(db.lib_name, db.cell_names())
    for cell in db.cells:
        layout = cell.layout
        if layout:
            print(cell.name, "shapes=", len(layout.shapes), "bbox=", layout.bbox)
    db.save("output/sample.room")

# room convert testdata/sample.gds output/sample.room</code></pre>
      <p>See <a href="python_gds.html">GDS import/export</a> and C++ <a href="example.html">example.html</a>.</p>""",
        "",
    ),
    "python_example_qucs.html": (
        "Qucs import example (Python)",
        "ex_qucs",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Qucs import example</p>
      <h1>Qucs import example</h1>
      <pre class="example"><code>import room

with room.from_qucs("examples/qucs_to_room/data/rc_lowpass.sch") as db:
    print(db.cell_names())
    db.save("output/rc_lowpass.room", view="schematic")
    db.to_qucs("output/rc_lowpass_out.sch")

# room convert --tool qucs rc_lowpass.sch rc_lowpass.room --view schematic</code></pre>""",
        "",
    ),
    "python_example_xschem.html": (
        "Xschem import example (Python)",
        "ex_xschem",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Xschem import example</p>
      <h1>Xschem import example</h1>
      <pre class="example"><code>import room

with room.from_xschem("examples/xschem_to_room/data/test.sch") as db:
    print(db.cell_names())
    db.save("output/xschem.room", view="schematic")
    db.to_xschem("output/test_out.sch")

# room convert --tool xschem test.sch test.room --view schematic</code></pre>""",
        "",
    ),
    "python_gds.html": (
        "GDS import/export (Python)",
        "gds",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Utilities &raquo; GDS</p>
      <h1 class="title">GDS import/export</h1>
      <p class="brief">Wrappers around C++ <a href="gdsimporter.html">GdsImporter</a> / GdsExporter.</p>
      <table class="memlist">
        <tr><td><code>room.from_gds(path) -&gt; Database</code></td><td>Import GDSII.</td></tr>
        <tr><td><code>db.to_gds(path)</code></td><td>Export GDSII.</td></tr>
        <tr><td><code>room convert in.gds out.room</code></td><td>CLI.</td></tr>
      </table>""",
        "",
    ),
    "python_qucs.html": (
        "Qucs import/export (Python)",
        "qucs",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Utilities &raquo; Qucs</p>
      <h1 class="title">Qucs import/export</h1>
      <p class="brief">Wrappers around <a href="qucsimporter.html">QucsImporter</a> / <a href="qucsexporter.html">QucsExporter</a>.</p>
      <table class="memlist">
        <tr><td><code>room.from_qucs(path)</code></td><td>Import <code>.sch</code>.</td></tr>
        <tr><td><code>db.to_qucs(path, cell=None)</code></td><td>Export schematic cell.</td></tr>
      </table>""",
        "",
    ),
    "python_xschem.html": (
        "Xschem import/export (Python)",
        "xschem",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Utilities &raquo; Xschem</p>
      <h1 class="title">Xschem import/export</h1>
      <p class="brief">Wrappers around <a href="xschemimporter.html">XschemImporter</a> / <a href="xschemexporter.html">XschemExporter</a>.</p>
      <table class="memlist">
        <tr><td><code>room.from_xschem(path)</code></td><td>Import <code>.sch</code>/<code>.sym</code>.</td></tr>
        <tr><td><code>db.to_xschem(path, cell=None)</code></td><td>Export cell or directory.</td></tr>
      </table>""",
        "",
    ),
    "python_oas.html": (
        "OASIS import/export (Python)",
        "oas",
        """      <p class="doc-path"><a href="python.html">Python</a> &raquo; Utilities &raquo; OASIS</p>
      <h1 class="title">OASIS import/export</h1>
      <p class="brief">Available when <code>room.has_oas()</code> is true.</p>
      <table class="memlist">
        <tr><td><code>room.has_oas()</code></td><td>Capability check.</td></tr>
        <tr><td><code>room.from_oas(path)</code></td><td>Import OASIS.</td></tr>
        <tr><td><code>db.to_oas(path)</code></td><td>Export OASIS.</td></tr>
      </table>""",
        "",
    ),
}

for name, (title, active, body, toc) in pages.items():
    (ROOT / name).write_text(page(title, active, body, toc), encoding="utf-8")
    print("wrote", name)

# Align python.html sidebar with the same structure (already written manually; refresh sidebar only)
print("done", len(pages))
