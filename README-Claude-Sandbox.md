# README-Claude-Sandbox

Setup notes for a Claude sandbox session (Ubuntu 24.04 container, network limited to package registries and GitHub).

## Packages

```bash
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y qt6-base-dev qt6-base-dev-tools libgl-dev cmake g++ fonts-crosextra-carlito fonts-crosextra-caladea fonts-liberation2 poppler-utils
pip install --break-system-packages python-docx pillow
```

- Ubuntu 24.04's Qt is 6.4.2, the same floor as ChromeOS bookworm, so a clean build here means the Chromebook build compiles.
- `libgl-dev` is needed for Qt6Gui's CMake config; `libgl1-mesa-dev` doesn't exist on 24.04.
- Fonts matter: without Carlito/Caladea, Calibri/Cambria documents reflow and render checks are meaningless.
- Optional second consumer for saved files: `sudo apt-get install -y --no-install-recommends libreoffice-writer-nogui` (several minutes), then `soffice --headless --convert-to pdf file.docx`.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/anyprocessorqt_tests tests/fixtures/features.docx tests/fixtures/headers.docx
```

Every CLI mode (`--roundtrip`, `--render`, `--pdf`, `--inspect`, `--dump`, `--screenshot`) sets `QT_QPA_PLATFORM=offscreen` itself; there is no display. The `XDG_RUNTIME_DIR not set` warning is harmless.

## Checking work visually

- `--render in.docx out.png` draws the page view (including paragraph borders and page-break markers) at 96 DPI, one tall image. Crop with Pillow before viewing long documents.
- `--screenshot out.png in.docx` grabs the real main window. There's no icon theme offscreen, so toolbars show text fallbacks.
- Pixel-diff a render of the original against a render of `--roundtrip` output; identical images mean the reader sees no difference.

## Quirks learned the hard way

- `QString::arg` has no `%%` escape, and chained `.arg()` calls re-substitute placeholders found in inserted text. Use one multi-argument `arg()` when inserted values can contain `%` (numbering `lvlText`, URLs).
- A Qt table cell's format is the char format of the cell's first block: `setBlockCharFormat` there wipes cell shading. Apply cell formats after cell content; merge block char formats inside tables.
- Qt rounds root-frame margins to whole pixels, so a right tab exactly on the margin wraps when the margin isn't a whole number of px (0.55in = 52.8px). Tabs and contextual spacing are adjusted for display and the original docx values kept on the block (`TabsOrig`, `SpacingOrig`) for save.
- Changing the root frame's format (e.g. margins) is an undoable edit and sets the modified flag. Draft view therefore never touches it: it widens the wrap width and shifts the viewport with negative margins so the page margins sit off-screen.
- `QSyntaxHighlighter::rehighlight()` sets the document's modified flag (no undo step). Use `SpellHighlighter::recheck()`, which restores it.
- Zoom is a `QGraphicsView` scaling the editor inside a `QGraphicsProxyWidget` (QTextEdit has no real zoom; `zoomIn()` only scales fonts and moves line breaks). Only top-level widgets can be embedded, so `ZoomView` detaches the editor first; the embedded editor is sized to the whole document and the view scrolls. Use `qgraphicsitem_cast`, not `qobject_cast`, on scene items.
- Page view paginates by giving the live document a page height (`setPageSize`), which Qt's layout honours exactly as in print. `QTextEdit` (on every resize/relayout) and `QTextDocument::setTextWidth()` both reset the height to -1, so width changes go through `PageEdit::setDocumentWidth()`, and `PageEdit` re-applies it after `QTextEdit::resizeEvent`. `setPageSize` is layout only: no undo step, no modified flag.
- With pagination, `documentSize().height()` is the content height, not the last page's bottom: size the embedded editor to `pageCount() * pageHeight`.
- Paragraph shading is drawn by Qt from the document. Dark paper repaints light neutral shading in `PageEdit::paintDarkCards` (fill, then `QTextBlock::layout()->draw` with selection/find ranges and a steady cursor). Qt fills shading between the paragraph's own margins, not `blockBoundingRect`.
- Headers/footers are separate `QTextDocument`s (`docx::HeaderFooters`, built by `DocxReader::buildPart` with the part's own rels). PAGE/NUMPAGES results carry the `FieldInstr` char property and are filled per page on a clone. Print and PDF go through `PageEdit::printPages` (not `QTextDocument::print`) so headers and paragraph borders are drawn. `tests/fixtures/headers.docx` comes from `tools/make_headers_fixture.py`.
- Table borders are drawn per cell (`QTextTableCellFormat` top/left/bottom/right with `borderCollapse`); the table's own uniform border stays 0. The source of truth is the table format's `TableBorderSpec` (`docx::TblBorders`); `docx::applyTableCellFormats()` re-derives every cell's borders and left/right padding from it and must run after reading a table and after any row/column/merge/split edit (MainWindow wraps those in one edit block).
- `QTextDocument::availableUndoSteps()` counts internal commands, not user-visible steps: an edit block of several changes reports more than 1 but undoes in one `undo()`. Test single-step undo with `undo()` then `!isUndoAvailable()`.
- A Qt table cell's format and a block's format live in the document; display-only state (find highlights, spell underlines) goes in `QTextEdit::setExtraSelections` and `QSyntaxHighlighter`.
- The tests run with `QStandardPaths::setTestModeEnabled(true)`, so the personal dictionary they write lands under `~/.qttest`, not the real app data.
- QDom on Qt 6.4 drops whitespace-only text nodes (single-space `<w:t>` runs). The reader uses its own tree on `QXmlStreamReader` (`src/docx/Xml.h`).

## Private sample documents

The owner's real documents are not committed (public repo). Ask for them as attachments when a fidelity question needs them; use `tests/fixtures/features.docx` otherwise.
