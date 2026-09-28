# ROOM API documentation style

HTML API reference lives in `docs/html/` and is published to GitHub Pages from that folder.
Each **class page** follows the same layout as [Qt class reference pages](https://doc.qt.io/qt-6/qstring.html).

## Page structure (top to bottom)

1. **Breadcrumb** — `ROOM » Core » C++ Classes » ClassName` (`<p class="doc-path">`)
2. **Title** — `ClassName Class` (`<h1 class="title">`)
3. **Brief** — one sentence + optional `More…` link to Detailed Description
4. **Meta table** — Header (`#include`) and CMake (`find_package` / `target_link_libraries`)
5. **Members index** — link to full class list (optional)
6. **Public Types** — enums, nested structs, typedefs (`<h2 id="public-types">`, `table.memlist`)
7. **Public Functions** — summary table with signature + one-line description (`<h2 id="public-functions">`)
8. **Detailed Description** — prose (`<h2 id="details">`)
9. **Member Function Documentation** — one `<div class="fn-block" id="...">` per function
10. **Private Types / Variables** — for reference only; not part of the public API (`<h2 id="private-members">`)
11. **Example** — `<pre class="example"><code>…</code></pre>`
12. **On this page** — right sidebar (`<aside class="page-toc">`) with section anchors

## Member blocks

Each function documents:

- **Signature** as `<h3 class="fn-title">`
- **Description** (`<p class="fn-desc">`)
- **Parameters** (`table.fn-params`) when non-empty
- **Returns** (`<p class="fn-returns">`) when applicable
- **Throws** (`<p class="fn-throws">`) when applicable

## Naming

| Item | Convention |
|------|------------|
| File | `docs/html/<lowercase>.html` (`database.html`, `shape.html`, `filesummary.html`, `xschemimporter.html`) |
| Title tag | `Database Class \| ROOM Core \| ROOM` |
| Namespace in prose | `room::Database` |
| Header path | `#include "database.h"` (from `src/`) |
| Punctuation | Prefer HTML entities in HTML (`&middot;`, `&mdash;`, `&rarr;`, `-&gt;` in diagrams) so pages render on all Windows browsers without mojibake |

## Template

Copy `docs/html/database.html` or `docs/html/shape.html` when adding a new class page.
Update the left sidebar `class="active"` link and the page TOC anchors.

## Publishing

Push changes under `docs/**` to `main`; the **Documentation (GitHub Pages)** workflow deploys `docs/html/` to the `gh-pages` branch.
