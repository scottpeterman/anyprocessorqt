# anyprocessorqt

A lightweight DOCX word processor in Qt. One native app on Linux, ChromeOS (Linux container), macOS and Windows, reading and writing Word `.docx` directly — no LibreOffice, no conversion step.

Scope is deliberate: everyday word processing — fonts and paragraph formatting, styles, bullets and numbering, tables, embedded images, hyperlinks, page breaks — with clean round-trips to Word.

## Status

v0.1.0 — early. It opens, edits and saves real Word documents. Round-trip tests cover style-heavy resumes, long-form documents with images and callouts, and Word-template fixtures. See [What's preserved](#whats-preserved) before trusting it with documents that use tracked changes or comments.

## Features

- **Text:** font family and size, bold, italic, underline, strikethrough, superscript/subscript, color, highlight, clear formatting
- **Paragraphs:** paragraph styles from the document (Normal, Title, Heading 1–n, List Paragraph, custom), alignment, indent, line spacing, spacing before/after, tab stops, paragraph borders (e.g. left-rule callouts), shading, page breaks
- **Lists:** bulleted and numbered, nested levels, Word style-driven lists (List Bullet / List Number), contextual spacing
- **Tables:** insert, add/remove rows and columns, merge/split cells; horizontal and vertical merges, cell shading and borders survive round-trips
- **Images:** insert or paste PNG/JPEG/GIF/BMP/WebP, resize by % of text width; original image bytes are kept untouched on save
- **Hyperlinks:** external links and internal anchors
- **Output:** save `.docx`, export PDF (paginated), print
- **Fonts:** Calibri, Cambria, Arial, Times New Roman and Courier New fall back to metric-compatible open fonts (Carlito, Caladea, Liberation) when the Microsoft fonts aren't installed, so line breaks match Word. Georgia maps to Gelasio when it's installed.

## How saving works

A `.docx` is a zip of XML parts. anyprocessorqt regenerates only what the editor owns — `word/document.xml`, its relationships, media it added, and numbering definitions for lists you create. Everything else in the package (`styles.xml`, theme, settings, headers/footers, font table, custom properties) is carried through byte-for-byte.

Paragraph and run formatting are written as differences from the paragraph's style, so a document edited here still responds to "modify style" in Word.

## What's preserved

| Content | Behaviour |
|---|---|
| Styles, theme, settings, headers/footers, section setup | Kept unchanged |
| Paragraph/run formatting, lists, tables, inline images, hyperlinks | Edited and written back |
| Headers and footers | Kept in the file; not shown or editable yet |
| Tracked changes | **Insertions are accepted and deletions dropped on save.** Accept or reject changes in Word first. |
| Comments, footnotes, endnotes | Their parts are kept, but the anchors in the text are not, so they detach on save |
| Floating (anchored) images | Read as inline images |
| Shapes, text boxes, charts, SmartArt, equations | Not shown; lost on save |
| Section breaks inside the document | Collapsed to the final section's page setup |

## Install

Download from [Releases](https://github.com/scottpeterman/anyprocessorqt/releases):

| Platform | File |
|---|---|
| Linux / ChromeOS, Intel/AMD | `anyprocessorqt-<version>-x86_64.AppImage` |
| Linux / ChromeOS, ARM | `anyprocessorqt-<version>-aarch64.AppImage` |
| macOS (Apple Silicon) | `anyprocessorqt-<version>-macos-arm64.dmg` |
| Windows | `anyprocessorqt-<version>-windows-x64.zip` |

`uname -m` tells you which Linux file you need.

### Linux and ChromeOS (AppImage)

The AppImage bundles Qt with its X11 and Wayland plugins, so nothing else from Qt is needed. AppImages mount themselves with FUSE, and Office documents need the metric-compatible fonts:

```bash
sudo apt install libfuse2 fonts-crosextra-carlito fonts-crosextra-caladea fonts-liberation2
chmod +x anyprocessorqt-*.AppImage
./anyprocessorqt-0.1.0-x86_64.AppImage
```

Without FUSE, run it with `--appimage-extract-and-run`. Every release is tested in a stock Debian bookworm container (the ChromeOS Linux base) under both X11 and Wayland.

### macOS

Open the DMG and drag anyprocessorqt to Applications. The app is ad-hoc signed, not notarized: the first time, right-click it and choose Open.

### Windows

Unzip anywhere and run `anyprocessorqt.exe`.

## Build

Requirements: CMake ≥ 3.19, a C++17 compiler, Qt ≥ 6.4 (Core, Gui, Widgets, PrintSupport). No other libraries — the zip reader/writer is built in.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/anyprocessorqt [file.docx]
```

### ChromeOS (Linux container, Debian bookworm)

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-wayland fonts-crosextra-carlito fonts-crosextra-caladea fonts-liberation2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/anyprocessorqt
```

Bookworm ships Qt 6.4.2, the minimum supported version. The `fonts-crosextra-*` packages supply Carlito and Caladea, the metric-compatible stand-ins for Calibri and Cambria — without them Word documents reflow.

To open `.docx` files from the ChromeOS Files app, install the app (`sudo cmake --install build`) and add a desktop entry with `MimeType=application/vnd.openxmlformats-officedocument.wordprocessingml.document;`.

## Command-line tools

The same binary has headless modes for testing and scripting:

```bash
anyprocessorqt --roundtrip in.docx out.docx   # read and write back
anyprocessorqt --pdf in.docx out.pdf          # paginated PDF
anyprocessorqt --render in.docx out.png       # one tall image of the page view
anyprocessorqt --inspect in.docx              # paragraphs with style, list, border info
anyprocessorqt --dump in.docx                 # parsed document as Qt HTML
anyprocessorqt --screenshot out.png in.docx   # editor window screenshot
```

## Tests

```bash
./build/anyprocessorqt_tests tests/fixtures/features.docx [more.docx ...]
```

Builds a document the way the editor does (styles, lists, nested levels, tables with merges and shading, images, links, highlight, breaks), saves it twice and checks that everything reloads and that numbering and media don't accumulate. Each extra file is round-tripped and compared block by block. `tools/compare_docx.py` gives an independent comparison through python-docx; `tools/make_fixture.py` regenerates the fixture.

## Roadmap

- Page view with visible page boundaries (PDF export already paginates)
- Header/footer display and editing
- Find and replace dialog, spell check
- Table properties dialog (borders, widths, alignment)

## Releasing

Set the version in `project(anyprocessorqt VERSION x.y.z)` in `CMakeLists.txt`, commit, then tag it:

```bash
git tag v0.1.0
git push origin v0.1.0
```

`.github/workflows/release.yml` builds and tests every package, runs the AppImages in Debian bookworm under X11 and Wayland, and publishes the release with `SHA256SUMS.txt`. The tag must match the CMake version. Packaging scripts live in `packaging/`.

## License

GPL-3.0 — see [LICENSE](LICENSE).
