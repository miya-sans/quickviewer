#include <QtWidgets>
#ifndef QV_WITHOUT_OPENGL
#  include <QtOpenGL>
#endif

#include "imageview.h"
#include "qvapplication.h"

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_renderer(Native)
    , m_hoverState(Qt::AnchorHorizontalCenter)
    , m_loupeCursor(QCursor(QPixmap(":/icons/loupe_cursor"), 20, 23))
    , m_pageManager(nullptr)
    , m_effectManager(this)
    , m_slideshowTimer(nullptr)
    , m_beginScaleFactor(1.0)
    , m_beginRotateFactor(0.0)
    , m_loupeFactor(3.0)
    , m_isMouseDown(false)
    , m_wideImage(false)
    , m_skipResizeEvent(false)
    , m_isFullScreen(false)
    , m_scrollMode(false)
    , m_pageBacking(false)
    , m_loupeEnable(false)
    , m_beforeScale(0)
    , m_readyStack(0)
    , m_lastScreenPixelRatio(1.0)
{
    m_zoomAnchor = QPoint(-1, -1);

    QGraphicsScene* scene = new QGraphicsScene(this);
    setScene(scene);
//    setTransformationAnchor(AnchorUnderMouse);
//    setDragMode(ScrollHandDrag);
//    setViewportUpdateMode(FullViewportUpdate);
    setAcceptDrops(false);
//    setDragMode(DragDropMode::InternalMove);
#ifdef QV_WITHOUT_OPENGL
    setRenderer(Native);
#else
    if(qApp->Effect() > qvEnums::UsingFixedShader)
        setRenderer(OpenGL);
#endif
    // made after the viewport, so they are over the images, and before the overlays of MainWindow, which are over them
    for(QLabel*& signage : m_signages) {
        signage = new QLabel(this);
        signage->setTextFormat(Qt::PlainText);
        signage->setAttribute(Qt::WA_TransparentForMouseEvents);
        signage->setStyleSheet("QLabel { color: #dddddd; background-color: rgba(0, 0, 0, 128); border: none; padding: 4px; }");
        signage->hide();
    }

    setMouseTracking(true);
    resetBackgroundColor();
    setAttribute(Qt::WA_AcceptTouchEvents);

}

#ifdef QV_WITHOUT_OPENGL
QWidget* widgetEngine = nullptr;
#else
QGLWidget* widgetEngine = nullptr;
#endif

void ImageView::setRenderer(RendererType type)
{
    m_renderer = type;
    if(widgetEngine)
        return;
#ifdef QV_WITHOUT_OPENGL
    type = RendererType::Native;
    QWidget* w = new QWidget;
    widgetEngine = w;
    setViewport(w);
#else
    if (m_renderer == OpenGL) {
        QGLWidget* w = new QGLWidget(QGLFormat(QGL::SampleBuffers));
        widgetEngine = w;
        setViewport(w);
    } else {
        setViewport(new QWidget);
    }
#endif
}


void ImageView::setPageManager(PageManager *manager)
{
    m_pageManager = manager;
    m_pageManager->setImageView(this);
    connect(manager, SIGNAL(pagesNolongerNeeded()), this, SLOT(on_clearImages_triggered()));
    connect(manager, SIGNAL(readyForPaint()), this, SLOT(readyForPaint()));
    connect(manager, SIGNAL(volumeChanged(QString)), this, SLOT(on_volumeChanged_triggered(QString)));
    connect(manager, SIGNAL(pageAdded(ImageContent, bool)), this, SLOT(on_addImage_triggered(ImageContent, bool)));
    connect(this, SIGNAL(slideShowStarted()), manager, SLOT(onSlideShowStarted()));
    connect(this, SIGNAL(slideShowStopped()), manager, SLOT(onSlideShowStopped()));
}

void ImageView::toggleSlideShow()
{
    if(!m_pageManager)
        return;
    if(m_slideshowTimer) {
        delete m_slideshowTimer;
        m_slideshowTimer = nullptr;
        emit slideShowStopped();
        return;
    }
    emit slideShowStarted();
    m_slideshowTimer = new QTimer();
    connect(m_slideshowTimer, SIGNAL(timeout()), this, SLOT(on_slideShowChanging_triggered()));
    m_slideshowTimer->start(qApp->SlideShowWait());
}

void ImageView::resetBackgroundColor()
{
//    QColor bg = qApp->BackgroundColor();
//    setStyleSheet(QString("background-color:") + bg.name(QColor::HexArgb));
    if(!qApp->UseCheckeredPattern()) {
        setBackgroundBrush(QBrush(qApp->BackgroundColor(), Qt::SolidPattern));
        return;
    }
    QPixmap pix(16, 16);
    pix.fill(qApp->BackgroundColor());
    QPainter paint(&pix);
    QBrush brush2(qApp->BackgroundColor2(), Qt::SolidPattern);
    paint.fillRect(QRect(0, 0, 8, 8), brush2);
    paint.fillRect(QRect(8, 8, 8, 8), brush2);
    QBrush brush(pix);
    setBackgroundBrush(brush);
}


void ImageView::on_volumeChanged_triggered(QString )
{
    m_pageRotations = QVector<int>(m_pageManager->size());
}

bool ImageView::on_addImage_triggered(ImageContent ic, bool pageNext)
{
    if(m_pageManager == nullptr) return false;
    m_ptLeftTop.reset();
    QGraphicsScene *s = scene();
    QSize size = ic.Image.size();
    PageContent pgi(this, s, ic);
    if(m_pageBacking && pgi.Separation == PageContent::FirstSeparated)
        pgi.Separation = PageContent::SecondSeparated;

    if(pageNext) {
        m_pages.push_back(pgi);
        connect(&(m_pages.last()), SIGNAL(resizeFinished()), this, SLOT(readyForPaint()));
    } else {
        m_pages.push_front(pgi);
        connect(&(m_pages.first()), SIGNAL(resizeFinished()), this, SLOT(readyForPaint()));
    }

    m_effectManager.prepareInitialize();

    return size.width() > size.height();
}

void ImageView::on_clearImages_triggered()
{
    if(m_pageManager == nullptr) return;
//    QGraphicsScene *s = scene();
    for(int i = 0; i < m_pages.length(); i++) {
        m_pages[i].dispose();
    }

    m_pages.resize(0);
    updateSignages();
    // horizontalScrollBar()->setValue(0);
    // verticalScrollBar()->setValue(0);
}
//static int paintCnt=0;
void ImageView::readyForPaint() {
//    qDebug() << "readyForPaint " << paintCnt++;
    if(qApp->Effect() > qvEnums::UsingFixedShader)
        setRenderer(OpenGL);
    if(!m_pages.empty()) {
        // another image (or the other half of a wide image) puts back the images dragged freely
        if(m_freePan && freePanKey() != m_freePanKey)
            leaveFreePan();
        int pageCount = m_pageManager->currentPage();
        QRect sceneRect;
        qvEnums::FitMode fitMode = qApp->Fitting() ? qApp->ImageFitMode() : qvEnums::NoFitting;
        for(int i = 0; i < m_pages.size(); i++) {
            if(qApp->SeparatePagesWhenWideImage() && m_pages[i].Ic.wideImage()) {
                if(m_pages[i].Separation == PageContent::NoSeparated && viewport()->width() < viewport()->height())
                    m_pages[i].Separation = PageContent::FirstSeparated;
                if(m_pages[i].Separation != PageContent::NoSeparated && viewport()->width() > viewport()->height())
                    m_pages[i].Separation = PageContent::NoSeparated;
            }
            PageContent::PageAlign pageAlign = PageContent::PageCenter;
            QRect pageRect = QRect(QPoint(), viewport()->size());
            // if(m_lastScreenPixelRatio != 1.0) {
            //     pageRect = QRect(pageRect.left()*m_lastScreenPixelRatio,pageRect.top()*m_lastScreenPixelRatio,
            //                      pageRect.width()*m_lastScreenPixelRatio,pageRect.height()*m_lastScreenPixelRatio);
            // }
            if(m_pages.size() == 2) {
                pageAlign = ((i==0 && !qApp->RightSideBook()) || (i==1 && qApp->RightSideBook()))
                            ? PageContent::PageLeft : PageContent::PageRight;
                pageRect = QRect(QPoint(pageAlign==PageContent::PageRight ? pageRect.width()/2 : 0 , 0), QSize(pageRect.width()/2,pageRect.height()));
            }
            QRect drawRect;
            qreal scalefactor = m_loupeEnable ? m_loupeFactor : 1.0;
            if(fitMode != qvEnums::NoFitting) {
                drawRect = m_pages[i].setPageLayoutFitting(
                            pageRect, pageAlign, fitMode, scalefactor,
                            m_pageRotations.isEmpty() ? 0 : m_pageRotations[pageCount+i]);
            } else {
                drawRect = m_pages[i].setPageLayoutManual(
                            pageRect, pageAlign, getZoomScale() / m_pages[i].sourceRatio() * scalefactor,
                            m_pageRotations.isEmpty() ? 0 : m_pageRotations[pageCount+i],
                            m_loupeEnable);
            }
            m_effectManager.prepare(dynamic_cast<QGraphicsPixmapItem*>(m_pages[i].GrItem), m_pages[i].Ic, drawRect.size());
            sceneRect = sceneRect.united(drawRect);
        }
        // if Size of Image overs Size of View, use Image's size
        setSceneRectMode(
          !(qApp->Fitting() && qApp->ImageFitMode() == qvEnums::FitToRect)
          || m_loupeEnable
          || m_lastScreenPixelRatio > 1.0, sceneRect);
    }
    updateSignages();
    // QGraphicsView updates the cursor internally,
    // but QV cannot trap this event, so it forcibly clears the cursor.
    if(m_isFullScreen && qApp->HideMouseCursorInFullscreen())
        setCursor(Qt::BlankCursor);
    m_effectManager.prepareFinished();
}

void ImageView::updateSignages()
{
    bool visible = qApp->ShowFullscreenSignage() && m_isFullScreen && m_pageManager;
    QRect area = viewport()->geometry();
    for(int i = 0; i < 2; i++) {
        QLabel* signage = m_signages[i];
        QString text = visible && i < m_pages.size() ? m_pageManager->pageSignage(i) : QString();
        if(text.isEmpty()) {
            signage->hide();
            continue;
        }
        // at the top right, and in dual view at the top left of the left page
        bool right = m_pages.size() == 1 || (i == 1) != qApp->RightSideBook();
        signage->ensurePolished();
        // the padding and a gap between the pages
        signage->setText(signage->fontMetrics().elidedText(text, Qt::ElideMiddle, area.width()/m_pages.size() - 16));
        signage->adjustSize();
        signage->move(right ? area.right()+1-signage->width() : area.left(), area.top());
        signage->show();
    }
}

static bool s_lastLoupeMode;

void ImageView::setSceneRectMode(bool scrolled, const QRect &sceneRect)
{
    // readyForPaint() and setSceneRectMode() may be called multiple times, and the scroll value from the second time onwards will not be accurate.
    // Therefore, the original scroll value is traced the first time, and the scroll value is corrected when the last call is completed.
    int sx = horizontalScrollBar()->value();
    int sy = verticalScrollBar()->value();
    m_readyStack++;

    if(!m_loupeEnable) {
        m_sceneRect = sceneRect;
    }
    bool afterLoupe = !m_loupeEnable && s_lastLoupeMode;
    if(m_loupeEnable && !s_lastLoupeMode) {
        m_scrollBaseValues = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    }
    s_lastLoupeMode = m_loupeEnable;
    // if Size of Image overs Size of View, use Image's size
    bool newMode = scrolled && (size().width() < sceneRect.width() || size().height() < sceneRect.height());
    QRectF oldrect = scene()->sceneRect();
    // the images dragged with the mouse stay where they were put
    bool freePan = m_freePan && !m_loupeEnable;
    if(freePan) {
        applyFreePan();
    } else {
        QRectF rect = newMode ? QRectF(QPoint(qMin(0, sceneRect.left()), 0), QSize(qMax(size().width(), sceneRect.width()), qMax(size().height(), sceneRect.height())))
                              : QRectF(QPoint(), size());
        scene()->setSceneRect(rect);
        if(newMode) {
            if(m_loupeEnable) {
                m_loupeBasePos = mapFromGlobal(QCursor::pos());
                setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                setDragMode(QGraphicsView::NoDrag);
                scrollOnLoupeMode();
            } else if(qApp->ScrollWithCursorWhenZooming()) {
                setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                setDragMode(QGraphicsView::NoDrag);
                scrollOnZoomMode();
            } else {
                // Since Qt :: ScrollBarAsNeeded does not work correctly, judge the display state on its own and switch.
                bool willBeHide = m_isFullScreen && qApp->HideScrollBarInFullscreen();
                setHorizontalScrollBarPolicy(!willBeHide && size().width() < sceneRect.width()+verticalScrollBar()->width() ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff );
                setVerticalScrollBarPolicy(!willBeHide && size().height() < sceneRect.height()+horizontalScrollBar()->height() ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff );
                // the left button drags the images freely (mousePressEvent) instead of the hand drag of QGraphicsView
                setDragMode(QGraphicsView::NoDrag);
                if(afterLoupe) {
                    horizontalScrollBar()->setValue(m_scrollBaseValues.x());
                    verticalScrollBar()->setValue(m_scrollBaseValues.y());
                }
            }
        } else {
            setDragMode(QGraphicsView::NoDrag);
        }
    }
    QRectF newrect = scene()->sceneRect();

    // Correct the scroll bar so that it keep at the center of the viewport
    // when the image display magnification is changed.
    // With 'Scroll with cursor when zooming', scrollOnZoomMode() above has already placed the images where the mouse
    // pans them, so a zoom goes to where the cursor points and does not jump at the next mouse move.
    // Without the mouse over the images (zooming with the keys), the zoom keeps the center
    bool pannedByCursor = newMode && !m_loupeEnable && qApp->ScrollWithCursorWhenZooming()
            && viewport()->rect().contains(viewport()->mapFromGlobal(QCursor::pos()));
    // Free panning places the images itself (applyFreePan), and the first layout after it puts them back
    // without the scroll position of the free panning
    bool afterFreePan = !freePan && m_afterFreePan && m_readyStack == 1;
    bool scaleChanged = false;
    if (m_readyStack == 1) {
        qreal newScale = m_pages[0].DrawScale;
        scaleChanged = m_beforeScale != newScale;
        if(!qApp->Fitting() && !freePan && !afterFreePan) {
            // zoomBy() keeps the point under the mouse in place itself when zooming with the wheel
            if (!m_loupeEnable && !pannedByCursor && m_zoomAnchor.x() < 0 && m_beforeScale > 0 && m_beforeScale != newScale) {
                int vw = 0.5*viewport()->width();
                int vh = 0.5*viewport()->height();
                int sx2 = (sx+vw)/m_beforeScale*newScale-vw;
                int sy2 = (sy+vh)/m_beforeScale*newScale-vh;

                horizontalScrollBar()->setValue(sx2);
                verticalScrollBar()->setValue(sy2);
            }
        }
        m_beforeScale = newScale;
    }
    if(afterFreePan) {
        m_afterFreePan = false;
        // 'Scroll with cursor when zooming' has placed them where the mouse pans them (scrollOnZoomMode),
        // otherwise they start at the top left, like a newly opened image
        if(newMode && !m_loupeEnable && !qApp->ScrollWithCursorWhenZooming()) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->minimum());
            verticalScrollBar()->setValue(verticalScrollBar()->minimum());
        }
    }

    if(m_scrollMode != newMode)
        emit scrollModeChanged(m_scrollMode = newMode);
    // the zoom in the status text changes even when the image stays inside the view
    if(oldrect != newrect || scaleChanged)
        emit zoomingChanged();

    m_readyStack--;
}

void ImageView::scrollOnLoupeMode()
{
    QPoint cursorPos = QCursor::pos();
//    QPoint cursorPos0 = QCursor::pos();
    cursorPos = mapFromGlobal(cursorPos);
    const QRectF sceneRect = scene()->sceneRect();

    // The scrolling of the enlarged image is completed by moving the cursor
    // at a distance of half the distance from the first clicked coordinate to the edge of the window
    QRectF L = sceneRect;
    QRect  K = m_sceneRect;
    QPoint V = m_scrollBaseValues;
    K.moveTo(K.left()-V.x(), K.top()-V.y());
    QPoint S = m_loupeBasePos;
    QPoint R((S.x()-K.left())*L.width()/K.width()+L.left(),
             (S.y()-K.top())*L.height()/K.height()+L.top());
    QPoint Q = R-S;

    horizontalScrollBar()->setValue(cursorPos.x() < S.x()
        ? L.left() + (Q.x() - L.left()) * (2*cursorPos.x() - S.x()) / S.x()
        : L.right() - (L.right() - Q.x()) * (S.x() + width() - 2*cursorPos.x()) / (width() - S.x())
    );
    verticalScrollBar()->setValue(cursorPos.y() < S.y()
        ? L.top() + (Q.y() - L.top()) * (2*cursorPos.y() - S.y()) / S.y()
        : L.bottom() - (L.bottom() - Q.y()) * (S.y() + height() - 2*cursorPos.y()) / (height() - S.y())
    );
//    qDebug() << "S" << S << "K" << K << "L" << L << "R" << R << "scroolBase" << m_scrollBaseValues;
}

void ImageView::scrollOnZoomMode()
{
    QPoint cursorPos = QCursor::pos();
    cursorPos = mapFromGlobal(cursorPos);
    cursorPos = QPoint(cursorPos.x() < width()/4 ? 0 : (cursorPos.x()- width()/4)*4/2,
                       cursorPos.y() < height()/4 ? 0 : (cursorPos.y()- height()/4)*4/2);
    const QRectF sceneRect = scene()->sceneRect();
    horizontalScrollBar()->setValue(horizontalScrollBar()->minimum()+cursorPos.x()*(horizontalScrollBar()->maximum()-horizontalScrollBar()->minimum())/width());
    verticalScrollBar()->setValue(horizontalScrollBar()->minimum()+cursorPos.y()*(verticalScrollBar()->maximum()-horizontalScrollBar()->minimum())/height());
}

QRectF ImageView::pagesRect() const
{
    QRectF rect;
    for(const PageContent& page : m_pages)
        if(page.GrItem)
            rect = rect.united(page.GrItem->sceneBoundingRect());
    return rect;
}

// Free panning, like PureRef: the left button drags the images anywhere, also beyond the edges of the view.
// This much of the images (in viewport pixels) stays in the view, so that they cannot be lost
#define FREE_PAN_MARGIN 48

QString ImageView::freePanKey() const
{
    QString key;
    for(const PageContent& page : m_pages)
        key += QString("%1|%2\n").arg(page.Ic.Path).arg(int(page.Separation));
    return key;
}

void ImageView::startDragPending(QMouseEvent *event)
{
    // not on the left and right edges, where a click turns the page (MainWindow::eventFilter)
    if(event->button() != Qt::LeftButton || event->buttons() != Qt::LeftButton || m_pages.isEmpty()
            || qApp->LoupeTool() || m_hoverState == Qt::AnchorLeft || m_hoverState == Qt::AnchorRight)
        return;
    m_dragPending = true;
    m_dragStartPos = event->position();
}

bool ImageView::enterFreePan()
{
    if(m_freePan)
        return true;
    QRectF pages = pagesRect();
    if(pages.isEmpty())
        return false;
    // it starts where the images are now, so that they do not jump
    m_freeOffset = viewportTransform().mapRect(pages).center() - viewportCenter();
    m_freePan = true;
    m_freePanKey = freePanKey();
    m_afterFreePan = false;
    applyFreePan();
    return true;
}

void ImageView::leaveFreePan()
{
    m_dragPending = false;
    if(m_dragging) {
        m_dragging = false;
        setCursor(m_isFullScreen && qApp->HideMouseCursorInFullscreen() ? Qt::BlankCursor : Qt::ArrowCursor);
    }
    if(!m_freePan)
        return;
    m_freePan = false;
    m_afterFreePan = true;
    // Without 'Scroll with cursor when zooming' the scroll bars come back. They are shown now, while the scene
    // is still large: shown during the next layout, they resize the viewport, and resizeEvent() shrinks the scene under it
    if(!qApp->ScrollWithCursorWhenZooming() && !(m_isFullScreen && qApp->HideScrollBarInFullscreen())) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void ImageView::applyFreePan()
{
    // the policies first: hiding the scroll bars resizes the viewport
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    QRectF pages = pagesRect();
    if(pages.isEmpty())
        return;
    // the images in view coordinates (viewport pixels, before scrolling)
    QRectF images = transform().mapRect(pages);
    QSizeF view = viewport()->size();
    // a part of the images stays in the view
    qreal limitX = qMax(0.0, (view.width() + images.width()) / 2 - qMin<qreal>(FREE_PAN_MARGIN, images.width()));
    qreal limitY = qMax(0.0, (view.height() + images.height()) / 2 - qMin<qreal>(FREE_PAN_MARGIN, images.height()));
    m_freeOffset = QPointF(qBound(-limitX, m_freeOffset.x(), limitX), qBound(-limitY, m_freeOffset.y(), limitY));
    // the scene reaches one view beyond the images on each side, so that the view can scroll them anywhere
    QRectF area = images.adjusted(-view.width(), -view.height(), view.width(), view.height());
    scene()->setSceneRect(transform().inverted().mapRect(area));
    // the value of a scroll bar is the view coordinate at the left (top) edge of the viewport
    horizontalScrollBar()->setValue(qRound(images.center().x() - view.width() / 2 - m_freeOffset.x()));
    verticalScrollBar()->setValue(qRound(images.center().y() - view.height() / 2 - m_freeOffset.y()));
}

void ImageView::scrollBy(int dx, int dy)
{
    if(m_freePan) {
        m_freeOffset -= QPointF(dx, dy);
        applyFreePan();
        return;
    }
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void ImageView::updateViewportOffset(QPointF moved)
{
    setTransform(
        QTransform()
                .scale(m_beginScaleFactor,m_beginScaleFactor)
                .rotate(m_beginRotateFactor)
                .translate(moved.x(), moved.y())
    );
}
static qreal s_lastScale;
static qreal s_lastRotate;

void ImageView::updateViewportFactors(qreal currentScale, qreal currentRotate)
{
    s_lastScale = currentScale;
    s_lastRotate = currentRotate;
    setTransform(
        QTransform()
           .scale(m_beginScaleFactor*currentScale,m_beginScaleFactor*currentScale)
           .rotate(m_beginRotateFactor+currentRotate)
    );
}

void ImageView::commitViewportFactors()
{
    m_beginScaleFactor *= s_lastScale;
    m_beginRotateFactor += s_lastRotate;
}

void ImageView::resetViewportFactors()
{
    m_beginScaleFactor = 1.0;
    m_beginRotateFactor = 0.0;
    setTransform(QTransform());
}

void ImageView::setCursor(const QCursor &cursor)
{
    // QGraphicsView is made up of layers of widgets, views, and items, all of which have setCursor()
    QGraphicsView::setCursor(cursor);
    if (m_isFullScreen && qApp->HideMouseCursorInFullscreen()){
        viewport()->setCursor(cursor);
        for (auto& page : m_pages) {
            if(page.GrItem != nullptr){
                page.GrItem->setCursor(cursor);
            }
        }
    }
}

//void ImageView::paintEvent(QPaintEvent *event)
//{
////    readyForPaint();
//    QGraphicsView::paintEvent(event);
//}

void ImageView::resizeEvent(QResizeEvent *event)
{
    static int resizeCount = 0;
    if(scene() && !m_isFullScreen && !m_freePan) {
        scene()->setSceneRect(QRect(QPoint(), event->size()));
    }
    QGraphicsView::resizeEvent(event);
    // also while resizing skips readyForPaint(), so that the text of the right page stays at the right edge
    updateSignages();
    if (resizeCount == 0) {
        resizeCount++;
        qreal newRatio = screen()->devicePixelRatio();
        if (m_lastScreenPixelRatio != newRatio) {

            QTransform scaling(1.0/newRatio, 0, 0, 1.0/newRatio, 0, 0);
            setTransform(scaling);
            m_lastScreenPixelRatio = newRatio;
        }
        if(!m_skipResizeEvent) {
            readyForPaint();
            m_pageManager->pageChanged();
        }
        resizeCount--;
    }
}

void ImageView::on_nextPage_triggered()
{
    if(qApp->SeparatePagesWhenWideImage() && m_pages[0].Separation == PageContent::FirstSeparated) {
        m_pages[0].Separation = PageContent::SecondSeparated;
        readyForPaint();
        return;
    }
    if(m_pageManager)
        m_pageManager->nextPage();
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_prevPage_triggered()
{
    if(qApp->SeparatePagesWhenWideImage() && m_pages[0].Separation == PageContent::SecondSeparated) {
        m_pages[0].Separation = PageContent::FirstSeparated;
        readyForPaint();
        return;
    }
    m_pageBacking = true;
    if(m_pageManager)
        m_pageManager->prevPage();
    if(isSlideShow())
        toggleSlideShow();
    m_pageBacking = false;
}

void ImageView::onActionNextPageOrVolume_triggered()
{
    if(qApp->SeparatePagesWhenWideImage() && m_pages[0].Separation == PageContent::FirstSeparated) {
        m_pages[0].Separation = PageContent::SecondSeparated;
        readyForPaint();
        return;
    }
    if(m_pageManager)
        if(!m_pageManager->nextPage()) {
            m_pageManager->nextVolume();
            m_pageManager->firstPage();
        }
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::onActionPrevPageOrVolume_triggered()
{
    if(qApp->SeparatePagesWhenWideImage() && m_pages[0].Separation == PageContent::SecondSeparated) {
        m_pages[0].Separation = PageContent::FirstSeparated;
        readyForPaint();
        return;
    }
    if(m_pageManager)
        if(!m_pageManager->prevPage()) {
            m_pageManager->prevVolume();
            m_pageManager->lastPage();
        }
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_fastForwardPage_triggered()
{
    if(m_pageManager)
        m_pageManager->fastForwardPage();
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_fastBackwardPage_triggered()
{
    if(m_pageManager)
        m_pageManager->fastBackwardPage();
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_firstPage_triggered()
{
    if(m_pageManager)
        m_pageManager->firstPage();
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_lastPage_triggered()
{
    if(m_pageManager)
        m_pageManager->lastPage();
    if(isSlideShow())
        toggleSlideShow();
}

void ImageView::on_nextOnlyOnePage_triggered()
{
    if(m_pageManager)
        m_pageManager->nextOnlyOnePage();
}

void ImageView::on_prevOnlyOnePage_triggered()
{
    if(m_pageManager)
        m_pageManager->prevOnlyOnePage();
}

void ImageView::on_rotatePage_triggered()
{
    if(!m_pageManager || m_pageRotations.empty())
        return;
    m_pageRotations[m_pageManager->currentPage()] += 90;
    readyForPaint();
}

void ImageView::on_showSubfolders_triggered(bool enable)
{
    qApp->setShowSubfolders(enable);
    if(m_pageManager->isFolder()) {
        m_pageManager->reloadVolume();
    }
}

void ImageView::on_slideShowChanging_triggered()
{
    int page = m_pageManager->currentPage();
    m_pageManager->nextPage();
    if(page == m_pageManager->currentPage())
        m_pageManager->firstPage();
}


void ImageView::on_nextVolume_triggered()
{
    if(m_pageManager)
        m_pageManager->nextVolume();
}

void ImageView::on_prevVolume_triggered()
{
    if(m_pageManager)
        m_pageManager->prevVolume();
}

void ImageView::onActionShowFullscreenSignage_triggered(bool enable)
{
    qApp->setShowFullscreenSignage(enable);
    readyForPaint();
}

void ImageView::onActionHideMouseCursorInFullscreen_triggered(bool enable)
{
    qApp->setHideMouseCursorInFullscreen(enable);
}

#define HOVER_BORDER 20
//#define NOT_HOVER_AREA 100

void ImageView::mouseMoveEvent(QMouseEvent *e)
{
    QGraphicsView::mouseMoveEvent(e);
    // dragging the images with the left button
    if(m_dragPending || m_dragging) {
        if(!(e->buttons() & Qt::LeftButton)) {
            // the release was missed
            m_dragPending = m_dragging = false;
        } else {
            QPointF moved = e->position() - m_dragStartPos;
            if(!m_dragging && moved.manhattanLength() >= QApplication::startDragDistance() && enterFreePan()) {
                m_dragging = true;
                m_dragStartOffset = m_freeOffset;
                if(!(m_isFullScreen && qApp->HideMouseCursorInFullscreen()))
                    setCursor(Qt::ClosedHandCursor);
            }
            if(m_dragging) {
                // the point of the images under the cursor follows it
                m_freeOffset = m_dragStartOffset + moved;
                applyFreePan();
                return;
            }
        }
    }
//    qDebug() << "qApp->HideMouseCursorInFullscreen()" << qApp->HideMouseCursorInFullscreen();
    int NOT_HOVER_AREA = width() / 3;
    int hover_border = qApp->LargeToolbarIcons() ? 3 * HOVER_BORDER : HOVER_BORDER;
    if(e->pos().x() < hover_border && e->pos().y() < height()- hover_border) {
        if(m_hoverState != Qt::AnchorLeft)
            emit anchorHovered(Qt::AnchorLeft);
        m_hoverState = Qt::AnchorLeft;
        if(m_isFullScreen && qApp->HideMouseCursorInFullscreen())
            setCursor(Qt::BlankCursor);
        else
            setCursor(Qt::PointingHandCursor);
        return;
    }
    if(e->pos().x() > width()- hover_border) {
        if(m_hoverState != Qt::AnchorRight)
            emit anchorHovered(Qt::AnchorRight);
        m_hoverState = Qt::AnchorRight;
        if(m_isFullScreen && qApp->HideMouseCursorInFullscreen())
            setCursor(Qt::BlankCursor);
        else
            setCursor(Qt::PointingHandCursor);

        return;
    }
    if(m_isFullScreen && qApp->HideMouseCursorInFullscreen())
        setCursor(Qt::BlankCursor);
    else if(qApp->LoupeTool()) {
        setCursor(m_loupeCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
//    qDebug() << qApp->ScrollWithCursorWhenZooming() << scene()->sceneRect() << size();
    if(m_loupeEnable)
        scrollOnLoupeMode();
    // the images dragged freely are not panned by the mouse
    else if(!m_freePan && qApp->ScrollWithCursorWhenZooming() && (scene()->sceneRect().width() > width() || scene()->sceneRect().height() > height())) {
        scrollOnZoomMode();
    }

    if(e->pos().y() < hover_border) {
        if(m_hoverState != Qt::AnchorTop)
           emit anchorHovered(Qt::AnchorTop);
        m_hoverState = Qt::AnchorTop;
        return;
    }
    if(e->pos().y() > height()- hover_border && e->pos().x() > NOT_HOVER_AREA) {
        if(m_hoverState != Qt::AnchorBottom)
           emit anchorHovered(Qt::AnchorBottom);
        m_hoverState = Qt::AnchorBottom;
        return;
    }
    if(m_hoverState != Qt::AnchorHorizontalCenter)
       emit anchorHovered(Qt::AnchorHorizontalCenter);
    m_hoverState = Qt::AnchorHorizontalCenter;
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    int delta_y = event->angleDelta().y();
    int delta = delta_y < 0 ? -Q_MOUSE_DELTA : delta_y > 0 ? Q_MOUSE_DELTA : 0;
    QMouseValue mv(QKeySequence(qApp->keyboardModifiers()), event->buttons(), delta);
    QAction* action = qApp->mouseActions().getActionByValue(mv);
    if(action != nullptr) {
        QString text = action->objectName();
        if (text == "actionZoomIn" || text == "actionZoomOut") {
            zoomByWheel(delta_y, text == "actionZoomIn", event->position().toPoint());
            event->accept();
            return;
        }
    }
    if(m_loupeEnable) {
        if(delta_y < 0)
            m_loupeFactor = qMax(1.5, m_loupeFactor-0.5);
        if(delta_y > 0)
            m_loupeFactor += 0.5;
        readyForPaint();
        return;
    }
    if(qApp->ScrollWithCursorWhenZooming()) {
        QMouseValue mv(QKeySequence(qApp->keyboardModifiers()), event->buttons(), delta);
        QAction* action = qApp->mouseActions().getActionByValue(mv);
        if(action) {
            action->trigger();
            event->accept();
            return;
        }
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if(!qApp->LoupeTool() || (event->buttons() != Qt::LeftButton)) {
        // the press is not accepted, so that MainWindow still gets it for the mouse actions
        startDragPending(event);
        QGraphicsView::mousePressEvent(event);
        return;
    }
    leaveFreePan();
    m_loupeEnable = true;
    readyForPaint();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton && (m_dragPending || m_dragging)) {
        bool dragged = m_dragging;
        m_dragPending = m_dragging = false;
        if(dragged) {
            setCursor(m_isFullScreen && qApp->HideMouseCursorInFullscreen() ? Qt::BlankCursor : Qt::ArrowCursor);
            event->accept();
            return;
        }
    }
    if(!qApp->LoupeTool() || (event->buttons() & Qt::LeftButton)) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    m_loupeEnable = false;
    readyForPaint();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // double-clicking puts back the images dragged freely
    if(m_freePan && event->button() == Qt::LeftButton
            && m_hoverState != Qt::AnchorLeft && m_hoverState != Qt::AnchorRight) {
        leaveFreePan();
        readyForPaint();
        event->accept();
        return;
    }
    // the second press of a double-click can start a drag too
    startDragPending(event);
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::on_fitting_triggered(bool enable)
{
    // M puts back the images dragged freely
    leaveFreePan();
    if (enable) {
        qApp->setFitting(enable);
        readyForPaint();
    } else {
        // When turning off fitting mode, use the scale up event handler instead.
        on_scaleUp_triggered();
    }
}

void ImageView::on_fitToWindow_triggered(bool enable)
{
    if(!enable) {
        return;
    }
    // F puts back the images dragged freely
    leaveFreePan();
    // fitting is on before fittingChanged, so that the Fitting button follows
    qApp->setImageFitMode(qvEnums::FitToRect);
    qApp->setFitting(true);
    emit fittingChanged(qvEnums::FitToRect);
    readyForPaint();
}

void ImageView::on_fitToWidth_triggered(bool enable)
{
    if(!enable) {
        return;
    }
    leaveFreePan();
    qApp->setImageFitMode(qvEnums::FitToWidth);
    qApp->setFitting(true);
    emit fittingChanged(qvEnums::FitToWidth);
    readyForPaint();
}

void ImageView::on_dualView_triggered(bool viewdual)
{
    qApp->setDualView(viewdual);

    m_pageManager->reloadCurrentPage();
    readyForPaint();
}

void ImageView::on_rightSideBook_triggered(bool rightSideBook)
{
    qApp->setRightSideBook(rightSideBook);
    readyForPaint();
}

// Each zoom step multiplies the displayed scale, starting from the fitted scale when fitting is on.
// The scale stops at 100% (of the original image) when a step goes over it, and for the keys also when it ends near it
#define ZOOM_STEP 1.25
#define ZOOM_SNAP_RANGE 0.1
// the wheel zooms finely: 5% for each notch, smooth wheels and touchpads zoom by their fraction of a notch
#define WHEEL_ZOOM_STEP 1.05
#define MIN_ZOOM 0.02
#define MAX_ZOOM 16.0

void ImageView::zoomBy(qreal factor, qreal snapRange, const QPoint& anchor)
{
    if(!m_pages.size())
        return;
    // the displayed scale is relative to the original image, the loaded image may be shrunk
    qreal current = qApp->Fitting() ? m_pages[0].DrawScale * m_pages[0].sourceRatio() : qApp->ZoomScale();
    if(qApp->Fitting()) {
        qApp->setFitting(false);
        emit fittingChanged(qApp->ImageFitMode());
    }
    qreal next = current * factor;
    if(qAbs(current - 1.0) > 0.01 && ((current < 1.0) != (next < 1.0) || qAbs(next - 1.0) < snapRange))
        next = 1.0;
    next = qBound(MIN_ZOOM, next, MAX_ZOOM);
    qApp->setZoomScale(next);

    if(m_freePan && !m_loupeEnable) {
        // The images dragged freely stay where they are: the point under the anchor stays in place,
        // the cursor for the wheel, the center of the view for the keys
        QPointF fixed = anchor.x() >= 0 ? QPointF(anchor) : viewportCenter();
        QRectF shown = viewportTransform().mapRect(pagesRect());
        QPointF relative(0.5, 0.5);
        if(!shown.isEmpty())
            relative = QPointF((fixed.x() - shown.left()) / shown.width(), (fixed.y() - shown.top()) / shown.height());
        readyForPaint();
        QSizeF zoomed = transform().mapRect(pagesRect()).size();
        m_freeOffset = fixed + QPointF((0.5 - relative.x()) * zoomed.width(), (0.5 - relative.y()) * zoomed.height()) - viewportCenter();
        applyFreePan();
        return;
    }

    // the point of the images under the anchor, relative to the rectangle of the images
    QRectF before = pagesRect();
    // with 'Scroll with cursor when zooming', the position of the mouse scrolls the images instead
    bool anchored = anchor.x() >= 0 && !before.isEmpty() && !qApp->ScrollWithCursorWhenZooming();
    QPointF relative;
    if(anchored) {
        QPointF scenePos = mapToScene(anchor);
        relative = QPointF((scenePos.x() - before.left()) / before.width(), (scenePos.y() - before.top()) / before.height());
    }
    m_zoomAnchor = anchored ? anchor : QPoint(-1, -1);
    readyForPaint();
    m_zoomAnchor = QPoint(-1, -1);
    if(anchored) {
        // scroll the same point of the zoomed images back under the anchor
        QRectF after = pagesRect();
        QPointF target(after.left() + relative.x() * after.width(), after.top() + relative.y() * after.height());
        QPoint delta = mapFromScene(target) - anchor;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
    }
}

void ImageView::on_scaleUp_triggered()
{
    zoomBy(ZOOM_STEP, ZOOM_SNAP_RANGE);
}

void ImageView::on_scaleDown_triggered()
{
    zoomBy(1.0 / ZOOM_STEP, ZOOM_SNAP_RANGE);
}

void ImageView::zoomByWheel(int angleDelta, bool zoomIn, const QPoint& anchor)
{
    if(angleDelta == 0)
        return;
    qreal factor = qPow(WHEEL_ZOOM_STEP, qAbs(angleDelta) / qreal(Q_MOUSE_DELTA));
    zoomBy(zoomIn ? factor : 1.0 / factor, 0, anchor);
}

void ImageView::on_wideImageAsOneView_triggered(bool wideImage)
{
    qApp->setWideImageAsOnePageInDualView(wideImage);
    m_pageManager->reloadCurrentPage();
    readyForPaint();
}

void ImageView::on_firstImageAsOneView_triggered(bool firstImage)
{
    qApp->setFirstImageAsOnePageInDualView(firstImage);
    m_pageManager->reloadCurrentPage();
    readyForPaint();
}

void ImageView::on_dontEnlargeSmallImagesOnFitting(bool enable)
{
    qApp->setDontEnlargeSmallImagesOnFitting(enable);
    readyForPaint();
}

void ImageView::onActionSeparatePagesWhenWideImage_triggered(bool enable)
{
    qApp->setSeparatePagesWhenWideImage(enable);
    readyForPaint();
}

void ImageView::onActionLoupe_triggered(bool enable)
{
    qApp->setLoupeTool(enable);
    if(!enable) {
        m_loupeEnable = false;
        readyForPaint();
    }
}

void ImageView::onActionScrollWithCursorWhenZooming_triggered(bool enable)
{
    qApp->setScrollWithCursorWhenZooming(enable);
    readyForPaint();
}

void ImageView::on_openFiler_triggered()
{
    if(!m_pageManager)
        return;
    QString path = m_pageManager->volumePath();
    if(m_pageManager->isFolder()) {
        path = m_pageManager->currentPagePath();
    }
#if defined(Q_OS_WIN)
    const QString explorer = QLatin1String("explorer.exe ");
    QFileInfo fi(path);

    // canonicalFilePath returns empty if the file does not exist
    if( !fi.canonicalFilePath().isEmpty() ) {
        QString nativeArgs;
        if (!fi.isDir()) {
            nativeArgs += QLatin1String("/select,");
        }
        nativeArgs += QLatin1Char('"');
        nativeArgs += QDir::toNativeSeparators(fi.canonicalFilePath());
        nativeArgs += QLatin1Char('"');

        qDebug() << "OO Open explorer commandline:" << explorer << nativeArgs;
        QProcess p;
        // QProcess on Windows tries to wrap the whole argument/program string
        // with quotes if it detects a space in it, but explorer wants the quotes
        // only around the path. Use setNativeArguments to bypass this logic.
        p.setNativeArguments(nativeArgs);
        p.start(explorer);
        p.waitForFinished(5000);
    }
#else
    if(!QFileInfo(path).isDir()) {
        QDir dir(path);
        dir.cdUp();
        path = dir.path();
    }
    QUrl url = QString("file:///%1").arg(path);
    QDesktopServices::openUrl(url);
#endif
}

void ImageView::on_copyPage_triggered()
{
    if(m_pages.empty())
        return;
    QClipboard* clipboard = qApp->clipboard();
    clipboard->setImage(m_pages[0].Ic.Image);
}

void ImageView::on_copyFile_triggered()
{
    QClipboard* clipboard = qApp->clipboard();
    QMimeData* mimeData = new QMimeData();
    QString path = QString("file:///%1").arg(m_pageManager->currentPagePath());
    mimeData->setData("text/uri-list", path.toUtf8());
    clipboard->setMimeData(mimeData);
}

void ImageView::onBrightness_valueChanged(ImageRetouch params)
{
    m_retouchParams = params;
    readyForPaint();
}

qreal ImageView::getZoomScale()
{
    // Some OS allow you to change the display magnification.
    // In this case, the content drawn is automatically scaled by devicePixelRatio,
    // but avoid scaling only the image.
    // QScreen* screen0 = screen();
    return qApp->ZoomScale();
}

