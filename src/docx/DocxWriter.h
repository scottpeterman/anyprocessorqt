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
    static bool saveFile(const QString &path, QTextDocument *doc, Package &pkg, QString *error);
};

} // namespace docx
