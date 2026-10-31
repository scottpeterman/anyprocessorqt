#pragma once

#include "docx/Package.h"

#include <QByteArray>
#include <QString>

class QTextDocument;

namespace docx {

class DocxWriter {
public:
    // Serialize doc into a .docx, reusing every part of pkg the editor doesn't own.
    // pkg is updated to reflect the saved package (new media, numbering, rels).
    static QByteArray build(QTextDocument *doc, Package &pkg, QString *error);
    // XML for a header (w:hdr) or footer (w:ftr) part from a text-only document.
    // PAGE / NUMPAGES fragments (FieldInstr) are written as fields.
    static QByteArray buildPart(QTextDocument *doc, Package &pkg, bool footer);
    static bool saveFile(const QString &path, QTextDocument *doc, Package &pkg, QString *error);
};

} // namespace docx
