#include <QtWidgets>

#include "captionoverlay.h"
#include "qvapplication.h"

// distance from the edges of the view, the same as the notifications
#define OVERLAY_MARGIN 12
#define OVERLAY_DEFAULT_WIDTH 600
#define OVERLAY_MIN_WIDTH 240
// the frame and the margin around the contents are the edges for resizing
#define OVERLAY_GRIP 5
#define OVERLAY_CORNER_GRIP 14
#define SECTION_MARGIN 6
#define ROW_HEIGHT 20
// the lines of each text when the editor fits the text (one caption, or each of several candidates)
#define CAPTION_LINES 8
#define CANDIDATE_LINES 5
// an editor dropped this near the center stays centered
#define CENTER_SNAP 24

static const QColor ICON_COLOR(0xc8, 0xc8, 0xc8);

// The same appearance as the prompt overlay
static const char* OVERLAY_STYLESHEET =
    "#CaptionOverlay { background-color: rgba(0, 0, 0, 190); border: 1px solid rgba(255, 255, 255, 40); }"
    "#CaptionOverlay[hovered=\"true\"] { background-color: rgba(0, 0, 0, 225); }"
    "#CaptionOverlay QWidget { background: transparent; border: none; color: #dddddd; }"
    "#CaptionOverlay QLabel#captionLabel { color: #8c8c8c; }"
    "#CaptionOverlay QWidget#captionBox[first=\"false\"] { border-top: 1px solid rgba(255, 255, 255, 26); }"
    "#CaptionOverlay QWidget#captionBox[focused=\"true\"] { background-color: rgba(255, 255, 255, 14); }"
    "#CaptionOverlay QToolButton { padding: 0px; color: #8c8c8c; }"
    "#CaptionOverlay QToolButton:hover { background-color: rgba(255, 255, 255, 40); }"
    "#CaptionOverlay QToolButton:pressed { background-color: rgba(255, 255, 255, 80); }"
    "#CaptionOverlay QLineEdit { padding: 0px; margin: 0px; color: #8c8c8c; selection-background-color: #5c5c5c; selection-color: #ffffff; }"
    "#CaptionOverlay QLineEdit:focus { color: #dddddd; }"
    "#CaptionOverlay QTextEdit { padding: 0px; margin: 0px; selection-background-color: #5c5c5c; selection-color: #ffffff; }"
    "#CaptionOverlay QScrollBar:vertical { background: transparent; width: 6px; margin: 0px; }"
    "#CaptionOverlay QScrollBar::handle:vertical { background-color: rgba(255, 255, 255, 60); min-height: 16px; }"
    "#CaptionOverlay QScrollBar::handle:vertical:hover { background-color: rgba(255, 255, 255, 110); }"
    "#CaptionOverlay QScrollBar::add-line:vertical, #CaptionOverlay QScrollBar::sub-line:vertical { height: 0px; }"
    "#CaptionOverlay QScrollBar::add-page:vertical, #CaptionOverlay QScrollBar::sub-page:vertical { background: transparent; }";

namespace {

/**
 * @brief execMenu shows a context menu of an edit.
 * The menu belongs to the main window so that it has the style of the theme
 */
void execMenu(QMenu* menu, QWidget* edit, const QPoint& globalPos)
{
    menu->setParent(edit->window(), menu->windowFlags());
    menu->exec(globalPos);
    delete menu;
}

/**
 * @brief The CaptionTextEdit class
 * The plain text of a caption. The wheel scrolls it, but never zooms it
 */
class CaptionTextEdit : public QTextEdit
{
public:
    explicit CaptionTextEdit(QWidget* parent)
        : QTextEdit(parent)
    {
        setAcceptRichText(false);
        setFocusPolicy(Qt::ClickFocus);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document()->setDocumentMargin(0);
        viewport()->setCursor(Qt::IBeamCursor);
    }
    /**
     * @brief contentHeight height of the first lines of the text in the width (all of them when lines is 0).
     * The lines are measured in the layout, because fallback fonts (e.g. Japanese) make them taller than the font
     */
    int contentHeight(int width, int lines = 0) const
    {
        QScopedPointer<QTextDocument> doc(document()->clone());
        doc->setDefaultFont(font());
        doc->setDefaultTextOption(document()->defaultTextOption());
        doc->setDocumentMargin(0);
        doc->setTextWidth(width);
        // lays out the whole document
        const int height = qCeil(doc->size().height());
        int count = 0;
        for(QTextBlock block = doc->begin(); lines > 0 && block.isValid(); block = block.next()) {
            const QTextLayout* layout = block.layout();
            for(int i = 0; i < layout->lineCount(); i++) {
                if(++count == lines) {
                    const QTextLine line = layout->lineAt(i);
                    return qMin(height, qCeil(layout->position().y() + line.y() + line.height()));
                }
            }
        }
        return height;
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        // Ctrl+Wheel zooms the image, so the text is not zoomed
        if(event->modifiers() & Qt::ControlModifier) {
            event->ignore();
            return;
        }
        QTextEdit::wheelEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        execMenu(createStandardContextMenu(event->pos()), this, event->globalPos());
        event->accept();
    }
};

/**
 * @brief The CaptionNameEdit class
 * The name of a caption candidate, which looks like a label
 */
class CaptionNameEdit : public QLineEdit
{
public:
    explicit CaptionNameEdit(QWidget* parent)
        : QLineEdit(parent)
    {
        setFrame(false);
        setFocusPolicy(Qt::ClickFocus);
    }

protected:
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        execMenu(createStandardContextMenu(), this, event->globalPos());
        event->accept();
    }
};

QFont smallFont(QFont font)
{
    if(font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * 0.9);
    else
        font.setPixelSize(qMax(8, font.pixelSize() * 9 / 10));
    return font;
}

/**
 * @brief setStyleProperty sets a property used by the style sheet, and applies the style again when it changes
 */
void setStyleProperty(QWidget* widget, const char* name, bool value)
{
    if(widget->property(name).isValid() && widget->property(name).toBool() == value)
        return;
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

CaptionOverlay::CaptionOverlay(QAbstractScrollArea *view)
    : QFrame(view)
    , m_view(view)
    , m_enabled(qApp->ShowCaptionOverlay())
    , m_customSize(qApp->CaptionOverlaySize())
    , m_alignment(Qt::Alignment(qApp->CaptionOverlayAlignment()))
    , m_offset(qApp->CaptionOverlayOffset())
    , m_minimumHeight(0)
    , m_dragMode(NoDrag)
    , m_resizeEdges()
{
    setObjectName("CaptionOverlay");
    setStyleSheet(OVERLAY_STYLESHEET);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);

    // the header shows the file. Dragging it moves the editor
    m_header = new QWidget(this);
    m_header->setFixedHeight(ROW_HEIGHT);
    m_header->setCursor(Qt::ArrowCursor);
    m_header->setToolTip(tr("Drag to move the caption editor, double-click to put it back at the bottom center"));
    m_fileLabel = new QLabel(m_header);
    m_fileLabel->setObjectName("captionLabel");
    m_fileLabel->setFont(smallFont(font()));
    m_saveButton = createButton("save", tr("Save the caption (Ctrl+S)"), [this] { save(); });
    QHBoxLayout* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(SECTION_MARGIN, 0, 0, 0);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(m_fileLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(createButton("plus", tr("Add a caption candidate (a \"%1\" line)").arg(CaptionFile::Separator),
                                         [this] { addCandidate(); }));
    headerLayout->addWidget(m_saveButton);
    headerLayout->addWidget(createButton("close", tr("Hide the caption editor"), [this] { emit hideRequested(); }));

    // a box for each caption candidate
    m_boxes = new QWidget(this);
    m_boxesLayout = new QVBoxLayout(m_boxes);
    m_boxesLayout->setContentsMargins(0, 0, 0, 0);
    m_boxesLayout->setSpacing(0);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(OVERLAY_GRIP - 1, OVERLAY_GRIP - 1, OVERLAY_GRIP - 1, OVERLAY_GRIP - 1);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_boxes);
    layout->addStretch(1);

    // the viewport is resized with the window and when scroll bars appear.
    // It is replaced when the renderer changes, so the view is also watched
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    hide();
}

void CaptionOverlay::setImage(const QString &imagePath)
{
    if(imagePath == m_imagePath) {
        // the same image (e.g. the folder was reloaded) keeps the changes in the editor,
        // and shows the file again when it was changed by another application
        if(m_enabled && !imagePath.isEmpty() && !isModified()) {
            CaptionFile file;
            file.load(imagePath);
            if(file.exists() != m_file.exists() || file.savedText() != m_file.savedText())
                load();
        }
        updateVisibility();
        return;
    }
    saveChanges();
    m_imagePath = imagePath;
    if(m_enabled && !imagePath.isEmpty())
        load();
    updateVisibility();
}

void CaptionOverlay::setOverlayEnabled(bool enabled)
{
    if(!enabled)
        saveChanges();
    m_enabled = enabled;
    // the file may have been changed while the editor was hidden
    if(enabled && !m_imagePath.isEmpty())
        load();
    updateVisibility();
}

bool CaptionOverlay::isModified() const
{
    if(m_imagePath.isEmpty() || m_file.imagePath() != m_imagePath || m_candidates.isEmpty())
        return false;
    return CaptionFile::serialize(editedCandidates()) != m_file.savedText();
}

void CaptionOverlay::saveChanges()
{
    // the caption of a deleted image is not written again
    if(isModified() && QFileInfo::exists(m_file.imagePath()))
        save();
}

bool CaptionOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == m_view || obj == m_view->viewport()) {
        // the viewport is laid out after the view is resized
        if(event->type() == QEvent::Resize && isVisible())
            QMetaObject::invokeMethod(this, [this] { relayout(); }, Qt::QueuedConnection);
        return false;
    }
    QWidget* widget = qobject_cast<QWidget*>(obj);
    // the buttons of a box are shown under the mouse
    if((event->type() == QEvent::Enter || event->type() == QEvent::Leave) && indexOf(widget) >= 0) {
        updateButtons(widget, event->type() == QEvent::Enter);
        return false;
    }
    const bool isText = qobject_cast<QTextEdit*>(obj) != nullptr;
    const bool isName = qobject_cast<QLineEdit*>(obj) != nullptr;
    if(!(isText || isName) || !widget->parentWidget())
        return QFrame::eventFilter(obj, event);
    // the box of the text or the name
    QWidget* box = isText ? widget->parentWidget() : widget->parentWidget()->parentWidget();
    const int index = indexOf(box);
    switch(event->type()) {
    case QEvent::ShortcutOverride: {
        // the keys are typed into the caption instead of turning pages. Only the function keys stay shortcuts
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if(key->key() >= Qt::Key_F1 && key->key() <= Qt::Key_F35)
            break;
        key->accept();
        return true;
    }
    case QEvent::KeyPress: {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        // Esc gives the keys back to the image
        if(key->key() == Qt::Key_Escape) {
            m_view->setFocus(Qt::OtherFocusReason);
            return true;
        }
        if(key->matches(QKeySequence::Save)) {
            save();
            return true;
        }
        if(index < 0 || (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
            break;
        // Enter in the name goes to its text, Tab to the next caption
        if(isName && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Tab)) {
            focusText(index);
            return true;
        }
        if(key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            const int count = m_candidates.size();
            focusText((index + (key->key() == Qt::Key_Backtab ? count - 1 : (isName ? 0 : 1))) % count);
            return true;
        }
        break;
    }
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        // the box being edited is lighter
        if(isText && index >= 0)
            setStyleProperty(box, "focused", event->type() == QEvent::FocusIn);
        break;
    default:
        break;
    }
    return QFrame::eventFilter(obj, event);
}

void CaptionOverlay::mousePressEvent(QMouseEvent *event)
{
    // nothing reaches the image view under the editor
    event->accept();
    if(event->button() != Qt::LeftButton)
        return;
    const QPoint pos = mapFromGlobal(event->globalPosition().toPoint());
    m_resizeEdges = edgesAt(pos);
    if(m_resizeEdges)
        m_dragMode = Resizing;
    else if(m_header->geometry().contains(pos))
        m_dragMode = Moving;
    else
        return;
    m_pressGlobalPos = event->globalPosition().toPoint();
    m_pressGeometry = geometry();
}

void CaptionOverlay::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    const QPoint globalPos = event->globalPosition().toPoint();
    if(m_dragMode == NoDrag) {
        updateEdgeCursor(edgesAt(mapFromGlobal(globalPos)));
        return;
    }
    const QPoint delta = globalPos - m_pressGlobalPos;
    const QRect bounds = viewArea();
    if(m_dragMode == Moving) {
        QRect rect = m_pressGeometry.translated(delta);
        rect.moveLeft(qMax(bounds.left(), qMin(rect.left(), bounds.right() + 1 - rect.width())));
        rect.moveTop(qMax(bounds.top(), qMin(rect.top(), bounds.bottom() + 1 - rect.height())));
        move(rect.topLeft());
        return;
    }
    QRect rect = m_pressGeometry;
    const int minWidth = qMin(OVERLAY_MIN_WIDTH, bounds.width());
    const int minHeight = qMin(m_minimumHeight, bounds.height());
    if(m_resizeEdges & Qt::LeftEdge)
        rect.setLeft(qMin(qMax(bounds.left(), rect.left() + delta.x()), rect.right() + 1 - minWidth));
    if(m_resizeEdges & Qt::RightEdge)
        rect.setRight(qMax(qMin(bounds.right(), rect.right() + delta.x()), rect.left() + minWidth - 1));
    if(m_resizeEdges & Qt::TopEdge)
        rect.setTop(qMin(qMax(bounds.top(), rect.top() + delta.y()), rect.bottom() + 1 - minHeight));
    if(m_resizeEdges & Qt::BottomEdge)
        rect.setBottom(qMax(qMin(bounds.bottom(), rect.bottom() + delta.y()), rect.top() + minHeight - 1));
    m_customSize = rect.size();
    updateAnchor(rect, false);
    relayout();
}

void CaptionOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if(m_dragMode == NoDrag)
        return;
    const bool moved = m_dragMode == Moving;
    m_dragMode = NoDrag;
    updateAnchor(geometry(), moved);
    storeGeometry();
    relayout();
}

void CaptionOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    event->accept();
    if(event->button() == Qt::LeftButton && m_header->geometry().contains(mapFromGlobal(event->globalPosition().toPoint())))
        resetGeometry();
}

void CaptionOverlay::wheelEvent(QWheelEvent *event)
{
    // the wheel over the editor never turns pages or zooms
    event->accept();
}

void CaptionOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    event->accept();
}

void CaptionOverlay::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QFrame::enterEvent(event);
}

void CaptionOverlay::leaveEvent(QEvent *event)
{
    setHovered(false);
    if(m_dragMode == NoDrag)
        setCursor(Qt::ArrowCursor);
    QFrame::leaveEvent(event);
}

QToolButton *CaptionOverlay::createButton(const QString &icon, const QString &toolTip, std::function<void()> clicked)
{
    QToolButton* button = new QToolButton(this);
    button->setIcon(qApp->flatIcon(icon, ICON_COLOR));
    button->setIconSize(QSize(12, 12));
    button->setFixedSize(ROW_HEIGHT, ROW_HEIGHT);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    connect(button, &QToolButton::clicked, this, clicked);
    return button;
}

CaptionOverlay::Candidate CaptionOverlay::createCandidate()
{
    Candidate c;
    c.box = new QWidget(m_boxes);
    c.box->setObjectName("captionBox");
    c.box->setAttribute(Qt::WA_StyledBackground);
    c.box->setCursor(Qt::ArrowCursor);
    QWidget* box = c.box;

    // the title row: the name and the buttons shown under the mouse
    c.titleRow = new QWidget(c.box);
    c.titleRow->setFixedHeight(ROW_HEIGHT);
    c.name = new CaptionNameEdit(c.titleRow);
    c.name->setFont(smallFont(font()));
    c.name->setToolTip(tr("The name of this caption, saved as a comment on its \"%1\" line").arg(CaptionFile::Separator));
    c.up = createButton("chevron-up", tr("Move up"), [this, box] { moveCandidate(box, -1); });
    c.down = createButton("chevron-down", tr("Move down"), [this, box] { moveCandidate(box, 1); });
    c.copy = createButton("copy", tr("Copy this caption"), [this, box] {
        const int index = indexOf(box);
        if(index < 0)
            return;
        QGuiApplication::clipboard()->setText(m_candidates[index].text->toPlainText());
        emit notify(tr("Copied the caption"));
    });
    c.remove = createButton("trash", tr("Remove this caption"), [this, box] { removeCandidate(box); });
    QHBoxLayout* titleLayout = new QHBoxLayout(c.titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(c.name, 1);
    for(QToolButton* button : {c.up, c.down, c.copy, c.remove}) {
        // hidden without moving the others
        QSizePolicy policy = button->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        button->setSizePolicy(policy);
        button->hide();
        titleLayout->addWidget(button);
    }

    c.text = new CaptionTextEdit(c.box);
    QVBoxLayout* layout = new QVBoxLayout(c.box);
    layout->setContentsMargins(SECTION_MARGIN, 0, SECTION_MARGIN, SECTION_MARGIN);
    layout->setSpacing(0);
    layout->addWidget(c.titleRow);
    layout->addWidget(c.text);

    connect(c.text, &QTextEdit::textChanged, this, [this] {
        updateState();
        relayout();
    });
    connect(c.name, &QLineEdit::textEdited, this, [this] { updateState(); });
    c.box->installEventFilter(this);
    c.text->installEventFilter(this);
    c.name->installEventFilter(this);
    return c;
}

int CaptionOverlay::indexOf(QWidget *box) const
{
    for(int i = 0; i < m_candidates.size(); i++) {
        if(m_candidates[i].box == box)
            return i;
    }
    return -1;
}

void CaptionOverlay::addCandidate()
{
    Candidate c = createCandidate();
    m_candidates << c;
    m_boxesLayout->addWidget(c.box);
    // children added to a visible widget are shown later, and the layout would not count them yet
    c.box->show();
    updateState();
    relayout();
    focusText(m_candidates.size() - 1);
}

void CaptionOverlay::removeCandidate(QWidget *box)
{
    const int index = indexOf(box);
    if(index < 0 || m_candidates.size() <= 1)
        return;
    const bool focused = hasEditFocus();
    Candidate c = m_candidates.takeAt(index);
    m_boxesLayout->removeWidget(c.box);
    c.box->hide();
    // the clicked button belongs to the box
    c.box->deleteLater();
    updateState();
    relayout();
    if(focused)
        focusText(qMin(index, int(m_candidates.size()) - 1));
}

void CaptionOverlay::moveCandidate(QWidget *box, int delta)
{
    const int index = indexOf(box);
    const int to = index + delta;
    if(index < 0 || to < 0 || to >= m_candidates.size())
        return;
    m_candidates.move(index, to);
    m_boxesLayout->removeWidget(box);
    m_boxesLayout->insertWidget(to, box);
    updateState();
    relayout();
}

QList<CaptionCandidate> CaptionOverlay::editedCandidates() const
{
    QList<CaptionCandidate> candidates;
    for(const Candidate& c : m_candidates)
        candidates << CaptionCandidate{c.name->text().simplified(), c.text->toPlainText()};
    return candidates;
}

void CaptionOverlay::load()
{
    const bool focused = hasEditFocus();
    m_file.load(m_imagePath);
    const QList<CaptionCandidate>& candidates = m_file.candidates();
    while(m_candidates.size() > candidates.size()) {
        Candidate c = m_candidates.takeLast();
        m_boxesLayout->removeWidget(c.box);
        c.box->hide();
        c.box->deleteLater();
    }
    while(m_candidates.size() < candidates.size()) {
        Candidate c = createCandidate();
        m_candidates << c;
        m_boxesLayout->addWidget(c.box);
        c.box->show();
    }
    for(int i = 0; i < candidates.size(); i++) {
        const Candidate& c = m_candidates[i];
        QSignalBlocker textBlocker(c.text);
        QSignalBlocker nameBlocker(c.name);
        c.name->setText(candidates[i].name);
        // also clears the undo history of the previous image
        c.text->setPlainText(candidates[i].text);
        c.text->moveCursor(QTextCursor::Start);
    }
    updateState();
    relayout();
    // the editing goes on with the next image
    if(focused && !hasEditFocus())
        focusText(0);
}

void CaptionOverlay::save()
{
    if(m_imagePath.isEmpty() || m_file.imagePath() != m_imagePath)
        return;
    const QString name = QFileInfo(m_file.path()).fileName();
    if(m_file.save(editedCandidates()))
        emit notify(tr("Saved %1").arg(name));
    else
        emit notify(tr("Could not save %1").arg(name));
    updateState();
}

void CaptionOverlay::updateState()
{
    if(m_candidates.isEmpty())
        return;
    const QString name = QFileInfo(m_file.path()).fileName();
    if(isModified())
        m_fileLabel->setText(QString("%1  %2").arg(name, QChar(0x2022)));
    else
        m_fileLabel->setText(m_file.exists() ? name : tr("%1 (new)").arg(name));
    m_fileLabel->setToolTip(QDir::toNativeSeparators(m_file.path()));

    // the titles are needed for several candidates, or for the name of the first one
    const bool titles = m_candidates.size() > 1 || !m_candidates.first().name->text().isEmpty();
    for(int i = 0; i < m_candidates.size(); i++) {
        const Candidate& c = m_candidates[i];
        c.titleRow->setVisible(titles);
        c.box->layout()->setContentsMargins(SECTION_MARGIN, titles ? 0 : 2, SECTION_MARGIN, SECTION_MARGIN);
        c.name->setPlaceholderText(tr("Caption %1").arg(i + 1));
        c.text->setPlaceholderText(i == 0 && !m_file.exists()
                                   ? tr("No caption file yet. Type a caption, Ctrl+S saves it")
                                   : tr("Empty caption"));
        setStyleProperty(c.box, "first", i == 0);
        updateButtons(c.box, c.box->isVisible() && c.box->rect().contains(c.box->mapFromGlobal(QCursor::pos())));
    }
}

void CaptionOverlay::updateButtons(QWidget *box, bool hovered)
{
    const int index = indexOf(box);
    if(index < 0)
        return;
    const Candidate& c = m_candidates[index];
    const int count = m_candidates.size();
    c.up->setVisible(hovered && index > 0);
    c.down->setVisible(hovered && index < count - 1);
    c.copy->setVisible(hovered);
    c.remove->setVisible(hovered && count > 1);
}

void CaptionOverlay::updateVisibility()
{
    if(m_enabled && !m_imagePath.isEmpty()) {
        relayout();
        show();
        raise();
    } else {
        // the keys go back to the image
        if(hasEditFocus())
            m_view->setFocus(Qt::OtherFocusReason);
        hide();
    }
}

bool CaptionOverlay::hasEditFocus() const
{
    QWidget* focus = QApplication::focusWidget();
    return focus && isAncestorOf(focus);
}

void CaptionOverlay::focusText(int index)
{
    if(index >= 0 && index < m_candidates.size())
        m_candidates[index].text->setFocus(Qt::OtherFocusReason);
}

void CaptionOverlay::relayout()
{
    if(m_imagePath.isEmpty() || m_candidates.isEmpty())
        return;
    // installing it again keeps one filter
    m_view->viewport()->installEventFilter(this);
    const QRect bounds = viewArea();
    if(bounds.width() < 40 || bounds.height() < 40)
        return;
    ensurePolished();
    int width = m_customSize.isValid() ? m_customSize.width() : OVERLAY_DEFAULT_WIDTH;
    width = qMin(qMax(width, OVERLAY_MIN_WIDTH), bounds.width());
    const int textWidth = qMax(40, width - 2 * (OVERLAY_GRIP + SECTION_MARGIN));
    const int maxLines = m_candidates.size() > 1 ? CANDIDATE_LINES : CAPTION_LINES;

    struct Text {
        QTextEdit* edit;
        int line;
        int fitted;
        int preferred;
        int height;
    };
    QList<Text> texts;
    for(const Candidate& c : m_candidates) {
        CaptionTextEdit* text = static_cast<CaptionTextEdit*>(c.text);
        const int line = text->contentHeight(textWidth, 1);
        texts << Text{text, line, text->contentHeight(textWidth, maxLines), qMax(line, text->contentHeight(textWidth)), 0};
    }

    // the height of everything but the texts.
    // The nested layouts update their size hints later (LayoutRequest), so they are invalidated here
    for(Text& text : texts)
        text.edit->setFixedHeight(0);
    for(QWidget* child : findChildren<QWidget*>()) {
        if(!child->layout())
            continue;
        child->layout()->invalidate();
        child->updateGeometry();
    }
    layout()->invalidate();
    const int chrome = sizeHint().height();
    m_minimumHeight = chrome;
    for(const Text& text : texts)
        m_minimumHeight += qMin(text.preferred, text.line);

    // at least a line of each text, then the room is shared by how much each text lacks
    auto distribute = [&](int available) {
        int used = 0, lack = 0;
        for(Text& text : texts) {
            text.height = qMin(text.preferred, text.line);
            used += text.height;
            lack += text.preferred - text.height;
        }
        int rest = available - used;
        if(rest <= 0 || lack <= 0)
            return;
        for(Text& text : texts) {
            int add = rest >= lack ? text.preferred - text.height : int(qint64(rest) * (text.preferred - text.height) / lack);
            text.height += add;
        }
    };
    int height = chrome;
    if(m_customSize.isValid()) {
        height = qMin(qMax(m_customSize.height(), m_minimumHeight), bounds.height());
        distribute(height - chrome);
    } else {
        for(Text& text : texts) {
            text.height = qMin(text.preferred, text.fitted);
            height += text.height;
        }
        if(height > bounds.height()) {
            height = bounds.height();
            distribute(height - chrome);
        }
    }
    for(const Text& text : texts)
        text.edit->setFixedHeight(text.height);

    int x;
    if(m_alignment & Qt::AlignLeft)
        x = bounds.left() + m_offset.x();
    else if(m_alignment & Qt::AlignRight)
        x = bounds.right() + 1 - m_offset.x() - width;
    else
        x = bounds.left() + (bounds.width() - width) / 2 + m_offset.x();
    int y = (m_alignment & Qt::AlignTop) ? bounds.top() + m_offset.y() : bounds.bottom() + 1 - m_offset.y() - height;
    x = qMax(bounds.left(), qMin(x, bounds.right() + 1 - width));
    y = qMax(bounds.top(), qMin(y, bounds.bottom() + 1 - height));
    setGeometry(x, y, width, height);
}

QRect CaptionOverlay::viewArea() const
{
    // the scroll bars are not covered
    return m_view->viewport()->geometry();
}

Qt::Edges CaptionOverlay::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if(!rect().contains(pos))
        return edges;
    if(pos.x() < OVERLAY_GRIP)
        edges |= Qt::LeftEdge;
    else if(pos.x() >= width() - OVERLAY_GRIP)
        edges |= Qt::RightEdge;
    if(pos.y() < OVERLAY_GRIP)
        edges |= Qt::TopEdge;
    else if(pos.y() >= height() - OVERLAY_GRIP)
        edges |= Qt::BottomEdge;
    // the corners are easier to catch
    if(edges & (Qt::LeftEdge | Qt::RightEdge)) {
        if(pos.y() < OVERLAY_CORNER_GRIP)
            edges |= Qt::TopEdge;
        else if(pos.y() >= height() - OVERLAY_CORNER_GRIP)
            edges |= Qt::BottomEdge;
    }
    if(edges & (Qt::TopEdge | Qt::BottomEdge)) {
        if(pos.x() < OVERLAY_CORNER_GRIP)
            edges |= Qt::LeftEdge;
        else if(pos.x() >= width() - OVERLAY_CORNER_GRIP)
            edges |= Qt::RightEdge;
    }
    return edges;
}

void CaptionOverlay::updateEdgeCursor(Qt::Edges edges)
{
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    if(horizontal && vertical) {
        const bool falling = (edges & Qt::LeftEdge) ? bool(edges & Qt::TopEdge) : bool(edges & Qt::BottomEdge);
        setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if(horizontal) {
        setCursor(Qt::SizeHorCursor);
    } else if(vertical) {
        setCursor(Qt::SizeVerCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void CaptionOverlay::updateAnchor(const QRect &rect, bool snap)
{
    // the editor stays at the nearest side of the view (or its center) when the window is resized
    const QRect bounds = viewArea();
    const int third = bounds.width() / 3;
    const int center = rect.center().x() - bounds.left();
    Qt::Alignment horizontal;
    int x;
    if(center < third) {
        horizontal = Qt::AlignLeft;
        x = qMax(0, rect.left() - bounds.left());
    } else if(center >= bounds.width() - third) {
        horizontal = Qt::AlignRight;
        x = qMax(0, bounds.right() - rect.right());
    } else {
        horizontal = Qt::AlignHCenter;
        x = rect.left() - (bounds.left() + (bounds.width() - rect.width()) / 2);
        if(snap && qAbs(x) < CENTER_SNAP)
            x = 0;
    }
    const bool top = rect.center().y() < bounds.center().y();
    m_alignment = horizontal | (top ? Qt::AlignTop : Qt::AlignBottom);
    m_offset = QPoint(x, qMax(0, top ? rect.top() - bounds.top() : bounds.bottom() - rect.bottom()));
}

void CaptionOverlay::storeGeometry()
{
    qApp->setCaptionOverlayGeometry(m_customSize, int(m_alignment), m_offset);
}

void CaptionOverlay::resetGeometry()
{
    m_customSize = QSize();
    m_alignment = Qt::AlignHCenter | Qt::AlignBottom;
    m_offset = QPoint(0, OVERLAY_MARGIN);
    storeGeometry();
    relayout();
}

void CaptionOverlay::setHovered(bool hovered)
{
    // darker under the mouse, so that long text stays readable over bright images
    setProperty("hovered", hovered);
    style()->unpolish(this);
    style()->polish(this);
    update();
}
