#pragma once

#include <QGraphicsView>

class PageEdit;
class QGraphicsProxyWidget;
class QTimer;

// Scales the page: the editor sits in a graphics scene and the view applies the
// zoom, so line breaks stay exactly as in Page view at any size. The editor is
// sized to the whole document; this view does the scrolling and centring.
class ZoomView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ZoomView(PageEdit *edit, QWidget *parent = nullptr);

    bool fitWidth() const { return m_fit; }
    double zoom() const { return m_zoom; }  // effective factor
    void setFitWidth();
    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void setDeskColor(const QColor &c);
    // Show the top of the first page (desk margin included) once the new
    // document has been laid out. Call after loading or creating a document.
    void scrollToTop();

    static constexpr double kMin = 0.5;
    static constexpr double kMax = 4.0;

signals:
    void zoomChanged(double factor, bool fit);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void relayout();
    void followCursor();
    void autoScroll();

    PageEdit *m_edit;
    QGraphicsProxyWidget *m_proxy;
    QTimer *m_relayout;
    QTimer *m_autoScroll;
    QPoint m_dragPos;
    bool m_dragging = false;
    bool m_pendingTop = false;
    bool m_fit = true;
    double m_zoom = 1.0;
    double m_userZoom = 1.0;
};
