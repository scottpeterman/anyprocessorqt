#include "ui/ZoomView.h"
#include "ui/PageEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr int kDesk = 24;  // grey margin around the page, in view pixels
const double kSteps[] = {0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
}

ZoomView::ZoomView(PageEdit *edit, QWidget *parent) : QGraphicsView(parent), m_edit(edit)
{
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(0xE6, 0xE6, 0xE6));
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setFocusPolicy(Qt::NoFocus);

    m_edit->setEmbedded(true);
    m_edit->setParent(nullptr);  // only top-level widgets can be embedded in a scene
    m_proxy = scene()->addWidget(m_edit);
    m_proxy->setPos(0, 0);
    m_proxy->setFocus();

    m_relayout = new QTimer(this);
    m_relayout->setSingleShot(true);
    m_relayout->setInterval(0);
    connect(m_relayout, &QTimer::timeout, this, &ZoomView::relayout);
    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(30);
    connect(m_autoScroll, &QTimer::timeout, this, &ZoomView::autoScroll);

    connect(m_edit->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, m_relayout,
            qOverload<>(&QTimer::start));
    connect(m_edit, &PageEdit::geometryHintsChanged, m_relayout, qOverload<>(&QTimer::start));
    connect(m_edit, &QTextEdit::cursorPositionChanged, this, [this] { QTimer::singleShot(0, this, &ZoomView::followCursor); });
    m_edit->installEventFilter(this);
    m_edit->viewport()->installEventFilter(this);
    relayout();
}

void ZoomView::relayout()
{
    const QSize vp = maximumViewportSize();
    const int sb = verticalScrollBar()->sizeHint().width();
    const double availW = qMax(200, vp.width() - sb - 2 * kDesk);

    double z = m_userZoom;
    int editW;
    if (m_edit->viewMode() == PageEdit::ViewMode::Page) {
        editW = m_edit->pageWidthPx();
        if (m_fit)
            z = availW / editW;
    } else {
        // Draft: the zoom scales the text; the wrap width fills the window at that scale.
        if (m_fit)
            z = 1.0;
        editW = int(availW / z);
    }
    z = qBound(kMin, z, kMax);
    const bool changed = !qFuzzyCompare(z, m_zoom);
    m_zoom = z;
    setTransform(QTransform::fromScale(z, z));

    // Editor height = whole document, at least the visible area.
    m_edit->setFixedWidth(editW);
    m_edit->setDocumentWidth(editW);
    const bool paged = m_edit->viewMode() == PageEdit::ViewMode::Page;
    QTextDocument *doc = m_edit->document();
    const int contentH = int(std::ceil(doc->documentLayout()->documentSize().height()));
    const int docH = paged ? qMax(contentH, doc->pageCount() * m_edit->pageHeightPx())  // whole sheets
                           : contentH + 2;
    // Page view ends at the last sheet; Draft fills the window.
    const int minH = paged ? 0 : int((vp.height() - 2 * kDesk) / z);
    m_edit->setFixedHeight(qMax(docH, minH));
    // The editor is as tall as its content, so it must never scroll itself.
    m_edit->verticalScrollBar()->setValue(0);
    const double desk = kDesk / z;
    scene()->setSceneRect(QRectF(-desk, -desk, editW + 2 * desk, m_edit->height() + 2 * desk));
    if (m_pendingTop) {
        m_pendingTop = false;
        verticalScrollBar()->setValue(verticalScrollBar()->minimum());
    }
    if (changed)
        emit zoomChanged(m_zoom, m_fit);
}

void ZoomView::setFitWidth()
{
    m_fit = true;
    relayout();
    followCursor();
    emit zoomChanged(m_zoom, m_fit);
}

void ZoomView::setZoom(double factor)
{
    m_fit = false;
    m_userZoom = qBound(kMin, factor, kMax);
    relayout();
    followCursor();
    emit zoomChanged(m_zoom, m_fit);
}

void ZoomView::zoomIn()
{
    for (double s : kSteps)
        if (s > m_zoom + 0.001) {
            setZoom(s);
            return;
        }
    setZoom(kMax);
}

void ZoomView::zoomOut()
{
    for (int i = int(std::size(kSteps)) - 1; i >= 0; --i)
        if (kSteps[i] < m_zoom - 0.001) {
            setZoom(kSteps[i]);
            return;
        }
    setZoom(kMin);
}

void ZoomView::scrollToTop()
{
    m_pendingTop = true;
    m_relayout->start();
}

void ZoomView::setDeskColor(const QColor &c)
{
    setBackgroundBrush(c);
}

void ZoomView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    relayout();
}

void ZoomView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const int dy = e->angleDelta().y();
        if (dy > 0)
            zoomIn();
        else if (dy < 0)
            zoomOut();
        e->accept();
        return;
    }
    // Plain wheel scrolls the page (the embedded editor has nothing to scroll).
    QAbstractScrollArea::wheelEvent(e);
}

void ZoomView::followCursor()
{
    const QRect r = m_edit->cursorRect();
    const QPointF tl = m_proxy->mapToScene(QPointF(m_edit->viewport()->mapTo(m_edit, r.topLeft())));
    ensureVisible(QRectF(tl, QSizeF(r.width() + 1, r.height())), 20, 40);
}

bool ZoomView::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_edit && e->type() == QEvent::KeyPress) {
        // PageUp/PageDown: the embedded editor is as tall as the document, so its own
        // paging would jump to the ends. Page by this view's visible height instead.
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_PageDown || ke->key() == Qt::Key_PageUp) {
            const int dir = ke->key() == Qt::Key_PageDown ? 1 : -1;
            const QRect r = m_edit->cursorRect();
            const double step = viewport()->height() / m_zoom * 0.9;
            QScrollBar *v = verticalScrollBar();
            v->setValue(v->value() + dir * int(viewport()->height() * 0.9));
            const QPoint target(r.center().x(), int(r.center().y() + dir * step));
            QTextCursor c = m_edit->cursorForPosition(target);
            if (ke->modifiers() & Qt::ShiftModifier) {
                QTextCursor sel = m_edit->textCursor();
                sel.setPosition(c.position(), QTextCursor::KeepAnchor);
                c = sel;
            }
            m_edit->setTextCursor(c);
            return true;
        }
    }
    if (obj == m_edit->viewport()) {
        // Drag-select past the visible edge: scroll the view (the editor can't).
        if (e->type() == QEvent::MouseButtonPress) {
            m_dragging = true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            m_autoScroll->stop();
        } else if (e->type() == QEvent::MouseMove && m_dragging) {
            m_dragPos = mapFromGlobal(QCursor::pos());
            const bool out = m_dragPos.y() < 0 || m_dragPos.y() > viewport()->height();
            if (out && !m_autoScroll->isActive())
                m_autoScroll->start();
            else if (!out)
                m_autoScroll->stop();
        }
    }
    return QGraphicsView::eventFilter(obj, e);
}

void ZoomView::autoScroll()
{
    if (!m_dragging) {
        m_autoScroll->stop();
        return;
    }
    const QPoint p = mapFromGlobal(QCursor::pos());
    QScrollBar *v = verticalScrollBar();
    if (p.y() < 0)
        v->setValue(v->value() - qMin(40, -p.y() / 2 + 4));
    else if (p.y() > viewport()->height())
        v->setValue(v->value() + qMin(40, (p.y() - viewport()->height()) / 2 + 4));
    else
        return;
    // Extend the selection to the point under the mouse.
    const QPointF scenePt = mapToScene(QPoint(qBound(0, p.x(), viewport()->width() - 1), qBound(0, p.y(), viewport()->height() - 1)));
    const QPoint editPt = m_edit->viewport()->mapFrom(m_edit, m_proxy->mapFromScene(scenePt).toPoint());
    QTextCursor sel = m_edit->textCursor();
    sel.setPosition(m_edit->cursorForPosition(editPt).position(), QTextCursor::KeepAnchor);
    m_edit->setTextCursor(sel);
}
