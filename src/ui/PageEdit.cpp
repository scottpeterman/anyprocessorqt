#include "ui/PageEdit.h"
#include "docx/Units.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QUrl>
#include <QUuid>

using namespace docx;

PageEdit::PageEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptRichText(true);
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0xE6, 0xE6, 0xE6));
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::Text, Qt::black);
    setPalette(pal);
    viewport()->setAutoFillBackground(true);
    setPageSetup(PageSetup());
}

int PageEdit::pageWidthPx() const
{
    return int(qRound(twipsToPx(m_page.width)));
}

void PageEdit::setPageSetup(const PageSetup &page)
{
    m_page = page;
    setLineWrapColumnOrWidth(pageWidthPx());
    updateMargins();
}

void PageEdit::updateMargins()
{
    const int sb = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0;
    const int avail = width() - sb;
    const int pw = pageWidthPx();
    const int left = qMax(0, (avail - pw) / 2);
    const int right = qMax(0, avail - pw - left);
    setViewportMargins(left, 24, right, 0);
}

void PageEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    updateMargins();
}

void PageEdit::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    QPainter p(viewport());
    const QPointF off(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    p.translate(off);
    paintDecorations(p, document(), QRectF(viewport()->rect()).translated(-off), true);
}

static bool parseBorder(const QString &spec, QColor *color, double *width, double *space)
{
    const QStringList f = spec.split(QLatin1Char('|'));
    if (f.size() != 4)
        return false;
    *width = qMax(1.0, ptToPx(f[1].toDouble() / 8.0));
    *space = ptToPx(f[2].toDouble());
    *color = QColor(QLatin1Char('#') + f[3]);
    return true;
}

void PageEdit::paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks)
{
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    const QTextFrameFormat rf = doc->rootFrame()->frameFormat();
    const double rootLeft = rf.leftMargin();
    const double rootRight = doc->textWidth() - rf.rightMargin();

    QRectF prevRect;
    QString prevLeft;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QRectF r = layout->blockBoundingRect(b);
        const QTextBlockFormat bf = b.blockFormat();
        const QString leftSpec = bf.stringProperty(BorderLeft);
        const bool inRoot = doc->frameAt(b.position()) == doc->rootFrame();
        const double contentLeft = inRoot ? rootLeft + bf.leftMargin() + bf.indent() * doc->indentWidth() : r.left();
        const double contentRight = inRoot ? rootRight - bf.rightMargin() : r.right();

        if (r.bottom() >= clip.top() - 50 && r.top() <= clip.bottom() + 50) {
            QColor c;
            double w = 0, space = 0;
            if (!leftSpec.isEmpty() && parseBorder(leftSpec, &c, &w, &space)) {
                const bool joined = prevLeft == leftSpec && prevRect.isValid();
                const double top = joined ? prevRect.bottom() : r.top() - 1;
                const double x = contentLeft - space - w / 2.0;
                p.fillRect(QRectF(x - w / 2.0, top, w, r.bottom() + 1 - top), c);
            }
            const QString rightSpec = bf.stringProperty(BorderRight);
            if (!rightSpec.isEmpty() && parseBorder(rightSpec, &c, &w, &space))
                p.fillRect(QRectF(contentRight + space, r.top() - 1, w, r.height() + 2), c);
            const QString topSpec = bf.stringProperty(BorderTop);
            if (!topSpec.isEmpty() && parseBorder(topSpec, &c, &w, &space))
                p.fillRect(QRectF(contentLeft, r.top() - space - w, contentRight - contentLeft, w), c);
            const QString bottomSpec = bf.stringProperty(BorderBottom);
            if (!bottomSpec.isEmpty() && parseBorder(bottomSpec, &c, &w, &space))
                p.fillRect(QRectF(contentLeft, r.bottom() + space, contentRight - contentLeft, w), c);

            if (showBreaks && (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) && b != doc->begin()) {
                // Dashed rule in the gap above the paragraph, label out in the right margin.
                const double gapTop = prevRect.isValid() ? prevRect.bottom() : r.top() - bf.topMargin();
                const double y = (gapTop + r.top()) / 2.0;
                p.setPen(QPen(QColor(0x9A, 0x9A, 0x9A), 1, Qt::DashLine));
                p.drawLine(QPointF(rootLeft, y), QPointF(rootRight, y));
                QFont f = p.font();
                f.setPointSizeF(6.5);
                p.setFont(f);
                p.drawText(QRectF(rootRight + 4, y - 6, 60, 12), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("break"));
            }
        }
        prevRect = r;
        prevLeft = leftSpec;
    }
}

bool PageEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || QTextEdit::canInsertFromMimeData(source);
}

void PageEdit::insertFromMimeData(const QMimeData *source)
{
    if (source->hasImage() && !source->hasHtml()) {
        QImage img = qvariant_cast<QImage>(source->imageData());
        if (img.isNull())
            return;
        const double maxW = twipsToPx(m_page.width - m_page.left - m_page.right);
        double w = img.width(), h = img.height();
        if (w > maxW) {
            h = h * maxW / w;
            w = maxW;
        }
        const QString name = QStringLiteral("image:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
        QTextImageFormat f;
        f.setName(name);
        f.setWidth(w);
        f.setHeight(h);
        textCursor().insertImage(f);
        return;
    }
    QTextEdit::insertFromMimeData(source);
}
