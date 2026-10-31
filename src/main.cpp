#include "docx/DocxReader.h"
#include "docx/DocxWriter.h"
#include "docx/HeaderFooter.h"
#include "docx/Units.h"
#include "ui/MainWindow.h"
#include "ui/PageEdit.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextStream>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <memory>

// Metric-compatible stand-ins so Office documents keep their line breaks on
// machines without the Microsoft fonts (ChromeOS/Linux). Qt only uses a
// substitute when the requested family isn't installed.
static void installFontSubstitutions()
{
    const QList<QPair<const char *, QStringList>> subs = {
        {"Calibri", {QStringLiteral("Carlito")}},
        {"Calibri Light", {QStringLiteral("Carlito")}},
        {"Cambria", {QStringLiteral("Caladea")}},
        {"Arial", {QStringLiteral("Liberation Sans"), QStringLiteral("Arimo")}},
        {"Helvetica", {QStringLiteral("Liberation Sans"), QStringLiteral("Arimo")}},
        {"Times New Roman", {QStringLiteral("Liberation Serif"), QStringLiteral("Tinos")}},
        {"Courier New", {QStringLiteral("Liberation Mono"), QStringLiteral("Cousine")}},
        {"Georgia", {QStringLiteral("Gelasio")}},
        {"Consolas", {QStringLiteral("Menlo"), QStringLiteral("Cousine"), QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"), QStringLiteral("Inconsolata"), QStringLiteral("Courier New")}},
    };
    for (const auto &[family, list] : subs)
        QFont::insertSubstitutions(QString::fromLatin1(family), list);
}

static int fail(const QString &msg)
{
    QTextStream(stderr) << msg << Qt::endl;
    return 1;
}

static int cliRoundtrip(const QString &in, const QString &out)
{
    QTextDocument doc;
    docx::Package pkg;
    QString err;
    if (!docx::DocxReader::loadFile(in, &doc, &pkg, &err))
        return fail(QStringLiteral("read %1: %2").arg(in, err));
    if (!docx::DocxWriter::saveFile(out, &doc, pkg, &err))
        return fail(QStringLiteral("write %1: %2").arg(out, err));
    QTextStream(stdout) << "ok " << doc.blockCount() << " blocks -> " << out << Qt::endl;
    return 0;
}

static int cliRender(const QString &in, const QString &out)
{
    QTextDocument doc;
    docx::Package pkg;
    QString err;
    if (!docx::DocxReader::loadFile(in, &doc, &pkg, &err))
        return fail(QStringLiteral("read %1: %2").arg(in, err));
    const double pageW = docx::twipsToPx(pkg.page.width);
    doc.setTextWidth(pageW);
    const QSizeF size = doc.documentLayout()->documentSize();
    QImage img(QSize(int(pageW), int(size.height()) + 1), QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    doc.drawContents(&p);
    PageEdit::paintDecorations(p, &doc, QRectF(QPointF(0, 0), img.size()), true);
    p.end();
    if (!img.save(out))
        return fail(QStringLiteral("could not write %1").arg(out));
    QTextStream(stdout) << "ok " << img.width() << "x" << img.height() << " -> " << out << Qt::endl;
    return 0;
}

static int cliPdf(const QString &in, const QString &out)
{
    QTextDocument doc;
    docx::Package pkg;
    QString err;
    if (!docx::DocxReader::loadFile(in, &doc, &pkg, &err))
        return fail(QStringLiteral("read %1: %2").arg(in, err));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(out);
    printer.setPageLayout(QPageLayout(QPageSize(QSizeF(pkg.page.width / 1440.0, pkg.page.height / 1440.0), QPageSize::Inch),
                                      QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    docx::HeaderFooters hf;
    hf.load(pkg);
    PageEdit::printPages(&doc, pkg.page, &hf, &printer);
    doc.setPageSize(QSizeF(docx::twipsToPx(pkg.page.width), docx::twipsToPx(pkg.page.height)));
    QTextStream(stdout) << "ok " << doc.pageCount() << " pages -> " << out << Qt::endl;
    return 0;
}

static int cliInspect(const QString &in)
{
    QTextDocument doc;
    docx::Package pkg;
    QString err;
    if (!docx::DocxReader::loadFile(in, &doc, &pkg, &err))
        return fail(QStringLiteral("read %1: %2").arg(in, err));
    QTextStream o(stdout);
    int n = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next(), ++n) {
        const QTextBlockFormat bf = b.blockFormat();
        o << n << " [" << bf.stringProperty(docx::PStyle) << "]";
        if (b.textList()) {
            const QTextListFormat lf = b.textList()->format();
            o << " list(num=" << lf.intProperty(docx::NumId) << " lvl=" << lf.intProperty(docx::Ilvl) << ")";
        }
        o << " L=" << bf.leftMargin() << " T=" << bf.topMargin() << " B=" << bf.bottomMargin();
        for (int pr : {docx::BorderTop, docx::BorderLeft, docx::BorderBottom, docx::BorderRight})
            if (bf.hasProperty(pr))
                o << " bdr" << (pr - docx::BorderTop) << "=" << bf.stringProperty(pr);
        if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
            o << " PAGEBREAK";
        o << " | " << b.text().left(50).replace(QChar::LineSeparator, QLatin1Char('/')) << "\n";
    }
    return 0;
}

static int cliDump(const QString &in)
{
    QTextDocument doc;
    docx::Package pkg;
    QString err;
    if (!docx::DocxReader::loadFile(in, &doc, &pkg, &err))
        return fail(QStringLiteral("read %1: %2").arg(in, err));
    QTextStream(stdout) << doc.toHtml() << Qt::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    // Headless modes don't need a display server.
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--roundtrip" || a == "--render" || a == "--pdf" || a == "--dump" || a == "--inspect"
            || a == "--screenshot")
            headless = true;
    }
#ifdef Q_OS_LINUX
    // Linux may have no display at all (CI, SSH). macOS and Windows always have their
    // native platform plugin, which handles the headless modes without showing a window.
    if (headless && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("anyprocessorqt"));
    QApplication::setOrganizationName(QStringLiteral("anyprocessorqt"));
    QApplication::setApplicationVersion(QStringLiteral(ANYPROCESSORQT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("anyprocessorqt"));
    {
        QIcon icon;
        for (const char *s : {"32", "64", "256"})
            icon.addFile(QStringLiteral(":/icons/anyprocessorqt-%1.png").arg(QLatin1String(s)));
        QApplication::setWindowIcon(icon);
    }
    installFontSubstitutions();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A lightweight DOCX word processor"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption roundtrip(QStringLiteral("roundtrip"), QStringLiteral("Read <in.docx> and write it to <out.docx>."));
    QCommandLineOption render(QStringLiteral("render"), QStringLiteral("Render <in.docx> to <out.png> (one tall page view)."));
    QCommandLineOption pdf(QStringLiteral("pdf"), QStringLiteral("Export <in.docx> to <out.pdf>."));
    QCommandLineOption dump(QStringLiteral("dump"), QStringLiteral("Print the parsed document as Qt HTML."));
    QCommandLineOption inspect(QStringLiteral("inspect"), QStringLiteral("List paragraphs with style, list and border info."));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Open [file] in the editor window and save a screenshot to <png>."),
                                  QStringLiteral("png"));
    QCommandLineOption viewOpt(QStringLiteral("view"), QStringLiteral("Start in <page|draft> view."), QStringLiteral("mode"));
    QCommandLineOption themeOpt(QStringLiteral("theme"), QStringLiteral("Use the <system|light|dark> theme (saved)."),
                                QStringLiteral("theme"));
    QCommandLineOption paperOpt(QStringLiteral("paper"), QStringLiteral("Use <white|dark> paper (saved)."),
                                QStringLiteral("paper"));
    parser.addOptions({roundtrip, render, pdf, dump, inspect, screenshot, viewOpt, themeOpt, paperOpt});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Document to open, or in/out for CLI modes."));
    parser.process(app);
    const QStringList pos = parser.positionalArguments();

    if (parser.isSet(roundtrip) || parser.isSet(render) || parser.isSet(pdf)) {
        if (pos.size() != 2)
            return fail(QStringLiteral("expected <in.docx> <out>"));
        if (parser.isSet(roundtrip))
            return cliRoundtrip(pos[0], pos[1]);
        if (parser.isSet(render))
            return cliRender(pos[0], pos[1]);
        return cliPdf(pos[0], pos[1]);
    }
    if (parser.isSet(dump) || parser.isSet(inspect)) {
        if (pos.size() != 1)
            return fail(QStringLiteral("expected <in.docx>"));
        return parser.isSet(dump) ? cliDump(pos[0]) : cliInspect(pos[0]);
    }

    if (parser.isSet(viewOpt))
        QSettings().setValue(QStringLiteral("viewMode"), parser.value(viewOpt));
    if (parser.isSet(themeOpt))
        QSettings().setValue(QStringLiteral("theme"), theme::toString(theme::themeFromString(parser.value(themeOpt))));
    if (parser.isSet(paperOpt))
        QSettings().setValue(QStringLiteral("paper"), theme::toString(theme::paperFromString(parser.value(paperOpt))));
    theme::captureSystem();
    theme::apply(theme::themeFromString(QSettings().value(QStringLiteral("theme")).toString()));
    MainWindow w;
    w.show();
    if (!pos.isEmpty())
        w.openFile(pos.first());
    if (parser.isSet(screenshot)) {
        const QString out = parser.value(screenshot);
        QTimer::singleShot(600, &w, [&w, out] {
            w.grab().save(out);
            QApplication::quit();
        });
    }
    return app.exec();
}
