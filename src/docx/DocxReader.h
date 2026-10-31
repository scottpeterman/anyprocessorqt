#pragma once

#include "docx/Package.h"

#include <QString>

class QTextDocument;

namespace docx {

class DocxReader {
public:
    // Parse a .docx file into doc; pkg receives the original package for save.
    static bool loadFile(const QString &path, QTextDocument *doc, Package *pkg, QString *error);
    static bool loadData(const QByteArray &data, QTextDocument *doc, Package *pkg, QString *error);

    // Build the document from an already-loaded package (also used for new documents).
    static bool build(const Package &pkg, QTextDocument *doc, QString *error);

    // Page margins, default font and indent width from the package.
    static void applyPageSetup(const Package &pkg, QTextDocument *doc);
};

} // namespace docx
