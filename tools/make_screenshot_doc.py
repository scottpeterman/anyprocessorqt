#!/usr/bin/env python3
"""Generate the sample document used for the README screenshots.

A short, generic change plan: title, headings, lists, a shaded table, a code
block in the app's own "Code" style, inline code, a highlight, a link, and a
header/footer with page numbers.

Usage: python3 tools/make_screenshot_doc.py [output.docx]
Then, for each screenshot:
  anyprocessorqt --theme light --paper white --screenshot docs/screenshots/light.png sample.docx
Requires: pip install python-docx
"""
import sys

import docx
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_COLOR_INDEX
from docx.oxml import OxmlElement, parse_xml
from docx.oxml.ns import nsdecls, qn
from docx.shared import Inches, Pt, RGBColor


def shade(el, fill):
    el.append(parse_xml(f'<w:shd {nsdecls("w")} w:val="clear" w:color="auto" w:fill="{fill}"/>'))


def field(paragraph, instr, result):
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), f" {instr} ")
    r = OxmlElement("w:r")
    t = OxmlElement("w:t")
    t.text = result
    r.append(t)
    fld.append(r)
    paragraph._p.append(fld)


def link(paragraph, url, text):
    r_id = paragraph.part.relate_to(url, docx.opc.constants.RELATIONSHIP_TYPE.HYPERLINK, is_external=True)
    h = OxmlElement("w:hyperlink")
    h.set(qn("r:id"), r_id)
    r = OxmlElement("w:r")
    rpr = OxmlElement("w:rPr")
    color = OxmlElement("w:color")
    color.set(qn("w:val"), "0563C1")
    rpr.append(color)
    u = OxmlElement("w:u")
    u.set(qn("w:val"), "single")
    rpr.append(u)
    r.append(rpr)
    t = OxmlElement("w:t")
    t.text = text
    r.append(t)
    h.append(r)
    paragraph._p.append(h)


def main(out):
    d = docx.Document()
    sec = d.sections[0]
    sec.left_margin = sec.right_margin = Inches(1)
    d.styles["Normal"].font.name = "Calibri"
    d.styles["Normal"].font.size = Pt(11)
    for name in ("Title", "Heading 1", "Header", "Footer"):
        d.styles[name].font.name = "Calibri Light" if name != "Header" and name != "Footer" else "Calibri"
    for name in ("Header", "Footer"):
        d.styles[name].font.size = Pt(9)
        d.styles[name].font.color.rgb = RGBColor(0x59, 0x59, 0x59)

    # The app's code styles: monospace, shaded, left rule.
    code = d.styles.add_style("Code", WD_STYLE_TYPE.PARAGRAPH)
    code.base_style = d.styles["Normal"]
    code.font.name = "Consolas"
    code.font.size = Pt(9.5)
    code.paragraph_format.space_after = Pt(0)
    ppr = code.element.get_or_add_pPr()
    ppr.append(parse_xml(f'<w:pBdr {nsdecls("w")}><w:left w:val="single" w:sz="18" w:space="6" w:color="C9CED6"/></w:pBdr>'))
    shade(ppr, "F3F4F6")
    inline = d.styles.add_style("Inline Code", WD_STYLE_TYPE.CHARACTER)
    inline.font.name = "Consolas"
    shade(inline.element.get_or_add_rPr(), "F3F4F6")

    hp = sec.header.paragraphs[0]
    hp.add_run("Change Plan · CHG-1042\t\tPage ")
    field(hp, "PAGE", "1")
    hp.add_run(" of ")
    field(hp, "NUMPAGES", "2")
    sec.footer.paragraphs[0].add_run("Internal — network operations")

    d.add_heading("Edge Router Software Upgrade", 0)
    p = d.add_paragraph()
    r = p.add_run("Maintenance window: Saturday 02:00–04:00 · Owner: Network Engineering")
    r.italic = True
    r.font.color.rgb = RGBColor(0x59, 0x59, 0x59)

    d.add_heading("Summary", 1)
    p = d.add_paragraph("Upgrade both edge routers to the current long-term release, one at a time, with "
                        "traffic drained to the peer router first. Each step has a verification gate and a "
                        "rollback that ")
    r = p.add_run("restores the previous image within five minutes")
    r.font.highlight_color = WD_COLOR_INDEX.YELLOW
    p.add_run(". Release notes are in the ")
    link(p, "https://example.com/release-notes", "vendor portal")
    p.add_run(".")

    d.add_heading("Scope", 1)
    for t in ("edge-01 and edge-02 in the primary site",
              "Transit and IX sessions on both routers",
              "No change to routing policy or addressing"):
        d.add_paragraph(t, style="List Bullet")

    d.add_heading("Steps", 1)
    for t in ("Capture the baseline: sessions, received prefixes, interface errors.",
              "Drain edge-01 and confirm traffic has moved to edge-02.",
              "Install the new image, reboot, and verify the checks below.",
              "Restore edge-01, then repeat for edge-02."):
        d.add_paragraph(t, style="List Number")

    p = d.add_paragraph("Verification commands, run before and after each router (")
    p.add_run("| no-more", style="Inline Code")
    p.add_run(" keeps the output on one screen):")
    for line in ("show bgp summary | no-more",
                 "show route summary | no-more",
                 "show interfaces extensive | match errors"):
        d.add_paragraph(line, style="Code")

    d.add_heading("Checks", 1)
    rows = [("Check", "Expected", "Result"),
            ("BGP sessions", "All Established", "Pass"),
            ("Received prefixes", "Within 1% of baseline", "Pass"),
            ("Interface errors", "No new input errors", "Pending")]
    table = d.add_table(rows=len(rows), cols=3)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, row in enumerate(rows):
        for j, text in enumerate(row):
            cell = table.cell(i, j)
            cell.text = text
            if i == 0:
                cell.paragraphs[0].runs[0].bold = True
                shade(cell._tc.get_or_add_tcPr(), "DDEBF7")
    for row in table.rows:
        for j, w in enumerate((Inches(2.0), Inches(2.6), Inches(1.4))):
            row.cells[j].width = w

    d.add_heading("Rollback", 1)
    d.add_paragraph("If any gate fails, boot the previous image from the alternate partition and re-run "
                    "the baseline checks. Do not start the second router until the first is back to its "
                    "baseline.")

    d.add_heading("Appendix: Baseline Output", 1)
    d.add_paragraph("Captured from edge-01 before the window; the post-change output is compared "
                    "against this line by line.")
    for i in range(1, 25):
        d.add_paragraph(f"203.0.113.{i:<3}  AS6450{i % 10}  Established  {1200 - i * 17:>5} / {80 + i:>3}", style="Code")
    d.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "sample.docx")
