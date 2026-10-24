#include <QtWidgets>

#include "captionoverlay.h"
#include "qvapplication.h"

// distance from the edges of the view, the same as the notifications
#define OVERLAY_MARGIN 12
#define OVERLAY_DEFAULT_WIDTH 600
#define OVERLAY_MIN_WIDTH 240
// the frame and the margin around the contents are the left and right edges for resizing
#define OVERLAY_GRIP 5
#define SECTION_MARGIN 6
#define ROW_HEIGHT 20
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
    "#CaptionOverlay QTextEdit { padding: 0px; margin: 0px; selection-background-color: #5c5c5c; selection-color: #ffffff; }";

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
        // no scroll bars: the editor fits the text, and a text taller than the view scrolls with the wheel and the cursor
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document()->setDocumentMargin(0);
        viewport()->setCursor(Qt::IBeamCursor);
    }
    struct Heights {
        int line;
        int lines;
        int all;
    };
    /**
     * @brief contentHeights heights of the text in the width: its first line, its first lines and all of it.
     * The lines are measured in the layout, because fallback fonts (e.g. Japanese) make them taller than the font.
     * One layout gives all of them: laying out a copy of the text three times was slow when the images are turned fast
     */
    Heights contentHeights(int width, int lines) const
    {
        QScopedPointer<QTextDocument> doc(document()->clone());
        doc->setDefaultFont(font());
        doc->setDefaultTextOption(document()->defaultTextOption());
        doc->setDocumentMargin(0);
        doc->setTextWidth(width);
        // lays out the whole document
        const int all = qCeil(doc->size().height());
        Heights heights{all, all, all};
        int count = 0;
        for(QTextBlock block = doc->begin(); block.isValid() && count < lines; block = block.next()) {
            const QTextLayout* layout = block.layout();
            for(int i = 0; i < layout->lineCount() && count < lines; i++) {
                const QTextLine line = layout->lineAt(i);
                const int bottom = qMin(all, qCeil(layout->position().y() + line.y() + line.height()));
                if(++count == 1)
                    heights.line = bottom;
                if(count == lines)
                    heights.lines = bottom;
            }
        }
        return heights;
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
    m_header->setToolTip(tr("Drag to move the caption editor, double-click to put it back at the bottom left"));
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
    // only the width is resized, the height always fits the text
    QRect rect = m_pressGeometry;
    const int minWidth = qMin(OVERLAY_MIN_WIDTH, bounds.width());
    if(m_resizeEdges & Qt::LeftEdge)
        rect.setLeft(qMin(qMax(bounds.left(), rect.left() + delta.x()), rect.right() + 1 - minWidth));
    if(m_resizeEdges & Qt::RightEdge)
        rect.setRight(qMax(qMin(bounds.right(), rect.right() + delta.x()), rect.left() + minWidth - 1));
    m_customSize = QSize(rect.width(), -1);
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
    if(!m_icons.contains(icon))
        m_icons.insert(icon, qApp->flatIcon(icon, ICON_COLOR));
    button->setIcon(m_icons.value(icon));
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
    // the texts always have fixed heights, set by relayout()
    c.text->setFixedHeight(ROW_HEIGHT);
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

void CaptionOverlay::appendCandidate()
{
    Candidate c = m_spareCandidates.isEmpty() ? createCandidate() : m_spareCandidates.takeLast();
    m_candidates << c;
    m_boxesLayout->addWidget(c.box);
    // children added to a visible widget are shown later, and the layout would not count them yet
    c.box->show();
}

void CaptionOverlay::releaseCandidate(int index)
{
    Candidate c = m_candidates.takeAt(index);
    m_boxesLayout->removeWidget(c.box);
    c.box->hide();
    QSignalBlocker textBlocker(c.text);
    QSignalBlocker nameBlocker(c.name);
    c.name->clear();
    c.text->clear();
    m_spareCandidates << c;
}

void CaptionOverlay::addCandidate()
{
    appendCandidate();
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
    releaseCandidate(index);
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
    while(m_candidates.size() > candidates.size())
        releaseCandidate(m_candidates.size() - 1);
    while(m_candidates.size() < candidates.size())
        appendCandidate();
    for(int i = 0; i < candidates.size(); i++) {
        const Candidate& c = m_candidates[i];
        QSignalBlocker textBlocker(c.text);
        QSignalBlocker nameBlocker(c.name);
        c.name->setText(candidates[i].name);
        // also clears the undo history of the previous image
        c.text->setPlainText(candidates[i].text);
        c.text->moveCursor(QTextCursor::Start);
    }
    // the callers lay it out with updateVisibility()
    updateState();
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
        m_fileLabel->setText(name);
    m_fileLabel->setToolTip(QDir::toNativeSeparators(m_file.path()));

    // the titles are needed for several candidates, or for the name of the first one
    const bool titles = m_candidates.size() > 1 || !m_candidates.first().name->text().isEmpty();
    const bool mouseOver = underMouse();
    for(int i = 0; i < m_candidates.size(); i++) {
        const Candidate& c = m_candidates[i];
        c.titleRow->setVisible(titles);
        c.box->layout()->setContentsMargins(SECTION_MARGIN, titles ? 0 : 2, SECTION_MARGIN, SECTION_MARGIN);
        c.name->setPlaceholderText(tr("Caption %1").arg(i + 1));
        c.text->setPlaceholderText(tr("Empty caption"));
        setStyleProperty(c.box, "first", i == 0);
        // (the cursor position is asked only under the mouse: it is slow on X11)
        updateButtons(c.box, mouseOver && c.box->isVisible() && c.box->rect().contains(c.box->mapFromGlobal(QCursor::pos())));
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
    // only images with a caption file, like the prompt only for images with generation data
    if(m_enabled && !m_imagePath.isEmpty() && m_file.imagePath() == m_imagePath && m_file.exists()) {
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
    int width = m_customSize.width() > 0 ? m_customSize.width() : OVERLAY_DEFAULT_WIDTH;
    width = qMin(qMax(width, OVERLAY_MIN_WIDTH), bounds.width());
    const int textWidth = qMax(40, width - 2 * (OVERLAY_GRIP + SECTION_MARGIN));

    struct Text {
        QTextEdit* edit;
        int line;
        int preferred;
        int height;
    };
    QList<Text> texts;
    for(const Candidate& c : m_candidates) {
        CaptionTextEdit* text = static_cast<CaptionTextEdit*>(c.text);
        const CaptionTextEdit::Heights heights = text->contentHeights(textWidth, 1);
        texts << Text{text, heights.line, qMax(heights.line, heights.all), 0};
    }

    // the height of everything but the texts, whose current fixed heights are in the size hint.
    // The nested layouts update their size hints later (LayoutRequest), so they are invalidated here.
    // (The texts are not shrunk to 0 for this: that resized them at once, and they were drawn so under load)
    for(QWidget* child : findChildren<QWidget*>()) {
        if(!child->layout())
            continue;
        child->layout()->invalidate();
        child->updateGeometry();
    }
    layout()->invalidate();
    int chrome = sizeHint().height();
    for(const Text& text : texts)
        chrome -= text.edit->maximumHeight();

    // all the text is shown. Only when it is taller than the view, each text has at least a line
    // and the room is shared by how much each text lacks (the rest scrolls with the wheel and the cursor)
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
    for(Text& text : texts) {
        text.height = text.preferred;
        height += text.height;
    }
    if(height > bounds.height()) {
        height = bounds.height();
        distribute(height - chrome);
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

    // The invalidated layouts would place the children only with the posted LayoutRequest. When the images are
    // turned fast, the editor was painted before it with the new size and the old texts (one squeezed line).
    // So they are laid out now, the parents before their children
    layout()->activate();
    for(QWidget* child : findChildren<QWidget*>()) {
        if(child->layout() && !child->isHidden())
            child->layout()->activate();
    }
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
    // the height fits the text, so only the left and right edges resize
    if(pos.x() < OVERLAY_GRIP)
        edges |= Qt::LeftEdge;
    else if(pos.x() >= width() - OVERLAY_GRIP)
        edges |= Qt::RightEdge;
    return edges;
}

void CaptionOverlay::updateEdgeCursor(Qt::Edges edges)
{
    setCursor(edges & (Qt::LeftEdge | Qt::RightEdge) ? Qt::SizeHorCursor : Qt::ArrowCursor);
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
    m_alignment = Qt::AlignLeft | Qt::AlignBottom;
    m_offset = QPoint(OVERLAY_MARGIN, OVERLAY_MARGIN);
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
