#include <QtWidgets>

#include "promptoverlay.h"
#include "qvapplication.h"

// distance from the edges of the view, the same as the notifications
#define OVERLAY_MARGIN 12
#define OVERLAY_DEFAULT_WIDTH 440
#define OVERLAY_MIN_WIDTH 240
// the frame and the margin around the contents are the edges for resizing
#define OVERLAY_GRIP 5
#define SECTION_MARGIN 6
#define ROW_HEIGHT 20
// the lines of all the settings (the prompts are shown whole)
#define SETTINGS_LINES 4

static const QColor TEXT_COLOR(0xdd, 0xdd, 0xdd);
static const QColor DIM_COLOR(0xa6, 0xa6, 0xa6);
static const QColor LABEL_COLOR(0x8c, 0x8c, 0x8c);
static const QColor TINT_COLOR(0x6f, 0xa8, 0xdc);
static const QColor BRACKET_COLOR(0x76, 0x76, 0x76);
static const QColor ICON_COLOR(0xc8, 0xc8, 0xc8);

// The same appearance is used for every theme, like the notifications
static const char* OVERLAY_STYLESHEET =
    "#PromptOverlay { background-color: rgba(0, 0, 0, 190); border: 1px solid rgba(255, 255, 255, 40); }"
    "#PromptOverlay[hovered=\"true\"] { background-color: rgba(0, 0, 0, 225); }"
    "#PromptOverlay QWidget { background: transparent; border: none; color: #dddddd; }"
    "#PromptOverlay QLabel#promptLabel { color: #8c8c8c; }"
    "#PromptOverlay QFrame#promptRule { background-color: rgba(255, 255, 255, 26); }"
    "#PromptOverlay QToolButton { padding: 0px; color: #8c8c8c; }"
    "#PromptOverlay QToolButton:hover { background-color: rgba(255, 255, 255, 40); }"
    "#PromptOverlay QToolButton:pressed { background-color: rgba(255, 255, 255, 80); }"
    "#PromptOverlay QToolButton#promptSettingsButton { padding: 0px 4px 0px 0px; }"
    "#PromptOverlay QToolButton#promptSettingsButton:hover,"
    " #PromptOverlay QToolButton#promptSettingsButton:pressed { background: transparent; color: #dddddd; }"
    "#PromptOverlay QTextEdit { padding: 0px; margin: 0px; selection-background-color: #5c5c5c; selection-color: #ffffff; }"
    "#PromptOverlay QScrollBar:vertical { background: transparent; width: 6px; margin: 0px; }"
    "#PromptOverlay QScrollBar::handle:vertical { background-color: rgba(255, 255, 255, 60); min-height: 16px; }"
    "#PromptOverlay QScrollBar::handle:vertical:hover { background-color: rgba(255, 255, 255, 110); }"
    "#PromptOverlay QScrollBar::add-line:vertical, #PromptOverlay QScrollBar::sub-line:vertical { height: 0px; }"
    "#PromptOverlay QScrollBar::add-page:vertical, #PromptOverlay QScrollBar::sub-page:vertical { background: transparent; }";

namespace {

/**
 * @brief tagRange the tag at a position of a line of a prompt: the text between the commas outside brackets,
 * without the spaces around it. Escaped characters like "\(" are not brackets, and unbalanced brackets are ignored
 * @return the start and the end of the tag, or an empty range
 */
QPair<int, int> tagRange(const QString& text, int pos)
{
    auto isOpening = [](QChar c) { return c == '(' || c == '[' || c == '{' || c == '<'; };
    auto isClosing = [](QChar c) { return c == ')' || c == ']' || c == '}' || c == '>'; };
    const int length = text.size();
    bool balanced = true;
    int depth = 0;
    for(int i = 0; i < length && balanced; i++) {
        if(text.at(i) == '\\')
            i++;
        else if(isOpening(text.at(i)))
            depth++;
        else if(isClosing(text.at(i)))
            balanced = --depth >= 0;
    }
    balanced = balanced && depth == 0;

    depth = 0;
    int start = 0;
    for(int i = 0; i <= length; i++) {
        if(i < length) {
            const QChar c = text.at(i);
            if(c == '\\') {
                i++;
                continue;
            }
            if(balanced && isOpening(c))
                depth++;
            else if(balanced && isClosing(c))
                depth--;
            if(c != ',' || depth > 0)
                continue;
        }
        // text[start, i) is a tag. A click on a comma is in the tag before it
        if(pos <= i || i >= length) {
            int end = qMin(i, length);
            while(start < end && text.at(start).isSpace())
                start++;
            while(end > start && text.at(end - 1).isSpace())
                end--;
            return qMakePair(start, end);
        }
        start = i + 1;
    }
    return qMakePair(0, 0);
}

/**
 * @brief The PromptTextEdit class
 * A read-only text which can be selected. Double-clicking selects a tag, and Ctrl+C copies the selection.
 * The other keys go to the image view. The wheel scrolls the text, but never zooms it
 */
class PromptTextEdit : public QTextEdit
{
public:
    explicit PromptTextEdit(QWidget* parent)
        : QTextEdit(parent)
    {
        setReadOnly(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
        // a click gives it the keys for Ctrl+C. The other keys are given back to the image view
        setFocusPolicy(Qt::ClickFocus);
        setFrameShape(QFrame::NoFrame);
        // the prompts are shown whole, like the caption editor. A text taller than the view scrolls with
        // the wheel and the cursor
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
    /**
     * @brief copySelection copies the selected text without its colors
     */
    void copySelection()
    {
        const QString text = textCursor().selection().toPlainText();
        if(!text.isEmpty() && copied)
            copied(text);
    }

    // the selected text to copy
    std::function<void(const QString&)> copied;
    // a key for the image view
    std::function<void(QKeyEvent*)> forwardKey;

protected:
    bool event(QEvent* event) override
    {
        if(event->type() == QEvent::ShortcutOverride) {
            // Every key comes to keyPressEvent(), like the image view, whose keys are not window shortcuts either
            event->accept();
            return true;
        }
        return QTextEdit::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        event->accept();
        // Ctrl+C copies the selection instead of the image (with no selection, it still copies the image)
        if(event->matches(QKeySequence::Copy) && textCursor().hasSelection()) {
            copySelection();
            return;
        }
        if(event->matches(QKeySequence::SelectAll)) {
            selectAll();
            return;
        }
        switch(event->key()) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Meta:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_CapsLock:
        case Qt::Key_NumLock:
            // e.g. Ctrl before C
            return;
        default:
            // the image view gets the keys back and handles this one, e.g. the keys turning pages
            if(forwardKey)
                forwardKey(event);
        }
    }
    void keyReleaseEvent(QKeyEvent* event) override { event->ignore(); }
    bool focusNextPrevChild(bool) override
    {
        // Tab goes to the image view like the other keys
        return false;
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        // a click on the image or another text clears the selection, so that Ctrl+C copies what it shows.
        // (Not for the context menu or another window)
        if(event->reason() != Qt::PopupFocusReason && event->reason() != Qt::ActiveWindowFocusReason) {
            QTextCursor cursor = textCursor();
            if(cursor.hasSelection()) {
                cursor.clearSelection();
                setTextCursor(cursor);
            }
        }
        QTextEdit::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        // A press in the selection would drag the text instead of selecting another part. So it starts a new selection
        // (Shift+click still extends it)
        if(event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier) && textCursor().hasSelection()) {
            QTextCursor cursor = textCursor();
            cursor.clearSelection();
            setTextCursor(cursor);
        }
        QTextEdit::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        // a double-click selects a tag, e.g. "(blue eyes:1.2)" or "<lora:name:0.8>"
        if(event->button() == Qt::LeftButton) {
            QTextCursor cursor = cursorForPosition(event->position().toPoint());
            const QTextBlock block = cursor.block();
            const QPair<int, int> range = tagRange(block.text(), cursor.positionInBlock());
            if(range.first < range.second) {
                cursor.setPosition(block.position() + range.first);
                cursor.setPosition(block.position() + range.second, QTextCursor::KeepAnchor);
                setTextCursor(cursor);
                event->accept();
                return;
            }
        }
        QTextEdit::mouseDoubleClickEvent(event);
    }
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
        // the menu belongs to the main window so that it has the style of the theme
        QMenu menu(window());
        QAction* copy = menu.addAction(QCoreApplication::translate("PromptOverlay", "&Copy"), this, &PromptTextEdit::copySelection);
        copy->setShortcut(QKeySequence::Copy);
        copy->setEnabled(textCursor().hasSelection());
        QAction* all = menu.addAction(QCoreApplication::translate("PromptOverlay", "Select &All"), this, &QTextEdit::selectAll);
        all->setShortcut(QKeySequence::SelectAll);
        menu.exec(event->globalPos());
        event->accept();
    }
};

/**
 * @brief The PromptValueLabel class
 * A value of the settings, shortened with "..." when there is no room. A click copies it
 */
class PromptValueLabel : public QWidget
{
public:
    explicit PromptValueLabel(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    /**
     * @brief set shows a value (the labels are reused for each image)
     */
    void set(const QString& text, const QString& value, std::function<void()> clicked)
    {
        m_text = text;
        m_clicked = clicked;
        setToolTip(QString("%1\n%2").arg(value, QCoreApplication::translate("PromptOverlay", "Click to copy")));
        updateGeometry();
        update();
    }
    QSize sizeHint() const override { return QSize(fontMetrics().horizontalAdvance(m_text) + 8, ROW_HEIGHT); }
    QSize minimumSizeHint() const override { return QSize(16, ROW_HEIGHT); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        if(underMouse())
            painter.fillRect(rect(), QColor(255, 255, 255, 30));
        painter.setPen(TEXT_COLOR);
        QRect textRect = rect().adjusted(4, 0, -4, 0);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(m_text, Qt::ElideRight, textRect.width()));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        event->accept();
        if(event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && m_clicked)
            m_clicked();
    }

private:
    QString m_text;
    std::function<void()> m_clicked;
};

/**
 * @brief appendPrompt adds a prompt with tinted weights like "(word:1.2)" and tags like "<lora:name:0.8>".
 * The brackets are dimmed, and escaped characters like "\(" are plain text
 */
void appendPrompt(QTextCursor& cursor, const QString& text, const QColor& color)
{
    static const QRegularExpression tagExp(R"(<(?:lora|lyco|lycoris|locon|loha|hypernet|hypernetwork|embedding|ti):[^<>]*>)",
                                           QRegularExpression::CaseInsensitiveOption);
    // ":1.2" just before an unescaped closing bracket
    static const QRegularExpression weightExp(R"(:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s*(?=[)\]]))");
    // NovelAI: "1.2::text::"
    static const QRegularExpression novelAIWeightExp(R"(-?\d+(?:\.\d+)?(?=::))");
    QTextCharFormat normal, tint, bracket;
    normal.setForeground(color);
    tint.setForeground(TINT_COLOR);
    bracket.setForeground(BRACKET_COLOR);

    QString plain;
    auto flush = [&] {
        if(!plain.isEmpty())
            cursor.insertText(plain, normal);
        plain.clear();
    };
    auto matchAt = [&](const QRegularExpression& exp, qsizetype pos) {
        return exp.match(text, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    };
    const qsizetype length = text.size();
    for(qsizetype i = 0; i < length;) {
        const QChar c = text.at(i);
        if(c == '\\' && i + 1 < length) {
            plain += text.mid(i, 2);
            i += 2;
            continue;
        }
        if(c == '<') {
            QRegularExpressionMatch match = matchAt(tagExp, i);
            if(match.hasMatch()) {
                flush();
                cursor.insertText(match.captured(), tint);
                i = match.capturedEnd();
                continue;
            }
        }
        if(c == ':') {
            QRegularExpressionMatch match = matchAt(weightExp, i);
            if(match.hasMatch()) {
                plain += text.mid(i, match.capturedStart(1) - i);
                flush();
                cursor.insertText(match.captured(1), tint);
                i = match.capturedEnd(1);
                continue;
            }
            if(i + 1 < length && text.at(i + 1) == ':') {
                flush();
                cursor.insertText("::", bracket);
                i += 2;
                continue;
            }
        }
        if((c.isDigit() || c == '-') && (i == 0 || !(text.at(i - 1).isLetterOrNumber() || text.at(i - 1) == '.' || text.at(i - 1) == '_'))) {
            QRegularExpressionMatch match = matchAt(novelAIWeightExp, i);
            if(match.hasMatch()) {
                flush();
                cursor.insertText(match.captured(), tint);
                i = match.capturedEnd();
                continue;
            }
        }
        if(c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
            flush();
            cursor.insertText(QString(c), bracket);
            i++;
            continue;
        }
        plain += c;
        i++;
    }
    flush();
}

} // namespace

PromptOverlay::PromptOverlay(QAbstractScrollArea *view)
    : QFrame(view)
    , m_view(view)
    , m_enabled(qApp->ShowPromptOverlay())
    , m_customSize(qApp->PromptOverlaySize())
    , m_corner(Qt::Corner(qApp->PromptOverlayCorner()))
    , m_offset(qApp->PromptOverlayOffset())
    , m_dragMode(NoDrag)
    , m_resizeEdges()
{
    setObjectName("PromptOverlay");
    setStyleSheet(OVERLAY_STYLESHEET);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);

    QFont smallFont = font();
    if(smallFont.pointSizeF() > 0)
        smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);
    else
        smallFont.setPixelSize(qMax(8, smallFont.pixelSize() * 9 / 10));

    // the header shows the tool. Dragging it moves the overlay
    m_header = new QWidget(this);
    m_header->setFixedHeight(ROW_HEIGHT);
    m_header->setCursor(Qt::ArrowCursor);
    m_header->setToolTip(tr("Drag to move the prompt, double-click to put it back in the corner"));
    m_toolLabel = new QLabel(m_header);
    m_toolLabel->setObjectName("promptLabel");
    m_toolLabel->setFont(smallFont);
    QHBoxLayout* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(SECTION_MARGIN, 0, 0, 0);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(m_toolLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(createButton("close", tr("Hide the prompt"), [this] { emit hideRequested(); }));

    QLabel* positiveTitle = new QLabel(tr("Prompt"));
    positiveTitle->setObjectName("promptLabel");
    positiveTitle->setFont(smallFont);
    m_positiveBox = createSection(positiveTitle, tr("Copy the prompt"), [this] {
        copyText(m_prompt.Positive, tr("Copied the prompt"));
    }, m_positiveText);

    QLabel* negativeTitle = new QLabel(tr("Negative prompt"));
    negativeTitle->setObjectName("promptLabel");
    negativeTitle->setFont(smallFont);
    m_negativeBox = createSection(negativeTitle, tr("Copy the negative prompt"), [this] {
        copyText(m_prompt.Negative, tr("Copied the negative prompt"));
    }, m_negativeText);

    m_rule = new QWidget(this);
    QFrame* line = new QFrame(m_rule);
    line->setObjectName("promptRule");
    line->setFixedHeight(1);
    QHBoxLayout* ruleLayout = new QHBoxLayout(m_rule);
    ruleLayout->setContentsMargins(SECTION_MARGIN, 0, SECTION_MARGIN, 0);
    ruleLayout->addWidget(line);

    // the main settings, a click on a value copies it
    m_parameters = new QWidget(this);
    m_parameters->setCursor(Qt::ArrowCursor);
    m_parametersLayout = new QGridLayout(m_parameters);
    m_parametersLayout->setContentsMargins(SECTION_MARGIN, 6, SECTION_MARGIN, 2);
    m_parametersLayout->setHorizontalSpacing(6);
    m_parametersLayout->setVerticalSpacing(0);
    m_parametersLayout->setColumnStretch(1, 17);
    m_parametersLayout->setColumnStretch(3, 10);

    // "All settings" opens the whole settings text
    m_settingsButton = new QToolButton;
    m_settingsButton->setObjectName("promptSettingsButton");
    m_settingsButton->setText(tr("All settings"));
    m_settingsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsButton->setIconSize(QSize(8, 8));
    m_settingsButton->setFont(smallFont);
    m_settingsButton->setFocusPolicy(Qt::NoFocus);
    m_settingsButton->setCursor(Qt::ArrowCursor);
    connect(m_settingsButton, &QToolButton::clicked, this, [this] { setAllSettingsVisible(!qApp->PromptOverlayAllSettings()); });
    m_settingsBox = createSection(m_settingsButton, tr("Copy all settings"), [this] {
        copyText(m_prompt.SettingText, tr("Copied all settings"));
    }, m_settingsText);
    m_settingsText->setVisible(qApp->PromptOverlayAllSettings());
    m_settingsText->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_settingsButton->setIcon(qApp->flatIcon(qApp->PromptOverlayAllSettings() ? "chevron-down" : "chevron-right", LABEL_COLOR));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(OVERLAY_GRIP - 1, OVERLAY_GRIP - 1, OVERLAY_GRIP - 1, OVERLAY_GRIP - 1);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_positiveBox);
    layout->addWidget(m_negativeBox);
    // the settings stay at the bottom when the overlay is taller than the text
    layout->addStretch(1);
    layout->addWidget(m_rule);
    layout->addWidget(m_parameters);
    layout->addWidget(m_settingsBox);

    // the viewport is resized with the window and when scroll bars appear.
    // It is replaced when the renderer changes, so the view is also watched
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    hide();
}

void PromptOverlay::setPrompt(const PromptInfo &prompt)
{
    // the same data (e.g. the next image of a batch) keeps the scroll positions
    const bool same = prompt.isValid() && prompt.Tool == m_prompt.Tool && prompt.toA1111Text() == m_prompt.toA1111Text();
    m_prompt = prompt;
    if(prompt.isValid() && !same) {
        m_toolLabel->setText(prompt.Tool);
        fillPrompt(m_positiveText, prompt.Positive, prompt.PositiveParts, TEXT_COLOR);
        fillPrompt(m_negativeText, prompt.Negative, prompt.NegativeParts, DIM_COLOR);
        m_positiveBox->setVisible(!prompt.Positive.isEmpty());
        m_negativeBox->setVisible(!prompt.Negative.isEmpty());

        m_settingsText->clear();
        QTextCharFormat format;
        format.setForeground(DIM_COLOR);
        QTextCursor(m_settingsText->document()).insertText(prompt.SettingText, format);
        m_settingsText->moveCursor(QTextCursor::Start);
        m_settingsBox->setVisible(!prompt.SettingText.isEmpty());
        rebuildParameters();
    }
    updateVisibility();
}

void PromptOverlay::setOverlayEnabled(bool enabled)
{
    m_enabled = enabled;
    updateVisibility();
}

bool PromptOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == m_view || obj == m_view->viewport()) {
        // the viewport is laid out after the view is resized
        if(event->type() == QEvent::Resize && isVisible())
            QMetaObject::invokeMethod(this, [this] { relayout(); }, Qt::QueuedConnection);
        return false;
    }
    // the copy button of a section is shown under the mouse
    if(event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        QWidget* button = qobject_cast<QWidget*>(obj->property("promptCopyButton").value<QObject*>());
        QWidget* text = qobject_cast<QWidget*>(obj->property("promptText").value<QObject*>());
        if(button && text)
            button->setVisible(event->type() == QEvent::Enter && !text->isHidden());
    }
    return QFrame::eventFilter(obj, event);
}

void PromptOverlay::mousePressEvent(QMouseEvent *event)
{
    // nothing reaches the image view under the overlay
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

void PromptOverlay::mouseMoveEvent(QMouseEvent *event)
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
    // the height fits the text, so only the width changes
    QRect rect = m_pressGeometry;
    const int minWidth = qMin(OVERLAY_MIN_WIDTH, bounds.width());
    if(m_resizeEdges & Qt::LeftEdge)
        rect.setLeft(qMin(qMax(bounds.left(), rect.left() + delta.x()), rect.right() + 1 - minWidth));
    if(m_resizeEdges & Qt::RightEdge)
        rect.setRight(qMax(qMin(bounds.right(), rect.right() + delta.x()), rect.left() + minWidth - 1));
    m_customSize = QSize(rect.width(), -1);
    updateAnchor(rect);
    relayout();
}

void PromptOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if(m_dragMode == NoDrag)
        return;
    m_dragMode = NoDrag;
    updateAnchor(geometry());
    storeGeometry();
}

void PromptOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    event->accept();
    if(event->button() == Qt::LeftButton && m_header->geometry().contains(mapFromGlobal(event->globalPosition().toPoint())))
        resetGeometry();
}

void PromptOverlay::wheelEvent(QWheelEvent *event)
{
    // the wheel over the overlay never turns pages or zooms
    event->accept();
}

void PromptOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    event->accept();
}

void PromptOverlay::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QFrame::enterEvent(event);
}

void PromptOverlay::leaveEvent(QEvent *event)
{
    setHovered(false);
    if(m_dragMode == NoDrag)
        setCursor(Qt::ArrowCursor);
    QFrame::leaveEvent(event);
}

QToolButton *PromptOverlay::createButton(const QString &icon, const QString &toolTip, std::function<void()> clicked)
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

QWidget *PromptOverlay::createSection(QWidget *title, const QString &copyToolTip, std::function<void()> copy, QTextEdit *&text)
{
    QWidget* box = new QWidget(this);
    box->setCursor(Qt::ArrowCursor);
    QWidget* titleRow = new QWidget(box);
    titleRow->setFixedHeight(ROW_HEIGHT);
    QToolButton* copyButton = createButton("copy", copyToolTip, copy);
    // hidden without moving the title
    QSizePolicy policy = copyButton->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    copyButton->setSizePolicy(policy);
    copyButton->hide();
    QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(title);
    titleLayout->addStretch(1);
    titleLayout->addWidget(copyButton);

    PromptTextEdit* edit = new PromptTextEdit(box);
    edit->copied = [this](const QString& selection) {
        QString shown = selection.simplified();
        if(shown.size() > 60)
            shown = shown.left(57) + "...";
        copyText(selection, tr("Copied \"%1\"").arg(shown));
    };
    edit->forwardKey = [this](QKeyEvent* event) {
        // (a key sent to the view would be taken as a shortcut override by the main window and dropped)
        m_view->setFocus(Qt::OtherFocusReason);
        emit viewKeyPressed(event);
    };
    text = edit;
    // the texts always have fixed heights, set by relayout()
    text->setFixedHeight(ROW_HEIGHT);
    QVBoxLayout* layout = new QVBoxLayout(box);
    layout->setContentsMargins(SECTION_MARGIN, 0, SECTION_MARGIN, SECTION_MARGIN);
    layout->setSpacing(0);
    layout->addWidget(titleRow);
    layout->addWidget(text);

    box->setProperty("promptCopyButton", QVariant::fromValue<QObject*>(copyButton));
    box->setProperty("promptText", QVariant::fromValue<QObject*>(text));
    box->installEventFilter(this);
    return box;
}

void PromptOverlay::fillPrompt(QTextEdit *edit, const QString &text, const PromptPairs &parts, const QColor &color)
{
    edit->clear();
    QTextCursor cursor(edit->document());
    if(parts.isEmpty()) {
        appendPrompt(cursor, text, color);
    } else {
        // the texts of each text encoder (SDXL) with their names
        QTextCharFormat labelFormat;
        labelFormat.setForeground(LABEL_COLOR);
        for(int i = 0; i < parts.size(); i++) {
            if(i > 0)
                cursor.insertBlock();
            cursor.insertText(parts[i].first, labelFormat);
            cursor.insertBlock();
            appendPrompt(cursor, parts[i].second, color);
        }
    }
    // the text is shown from the beginning
    edit->moveCursor(QTextCursor::Start);
}

void PromptOverlay::rebuildParameters()
{
    // the labels are reused: creating them for every image was slow when the images are turned fast
    while(QLayoutItem* item = m_parametersLayout->takeAt(0))
        delete item;
    int used = 0;
    auto add = [this, &used](int row, int column, const QString& label, const QString& value, int columnSpan, bool size) {
        if(used == m_parameterKeys.size()) {
            QLabel* key = new QLabel(m_parameters);
            key->setObjectName("promptLabel");
            m_parameterKeys << key;
            m_parameterValues << new PromptValueLabel(m_parameters);
        }
        QLabel* key = m_parameterKeys[used];
        PromptValueLabel* text = static_cast<PromptValueLabel*>(m_parameterValues[used]);
        used++;
        key->setText(label);
        key->setContentsMargins(column > 0 ? 8 : 0, 0, 0, 0);
        const QString shown = size ? QString(value).replace('x', QChar(0x00d7)) : value;
        const QString message = tr("Copied %1: %2").arg(label, value.size() > 60 ? value.left(57) + "..." : value);
        text->set(shown, value, [this, value, message] { copyText(value, message); });
        m_parametersLayout->addWidget(key, row, column);
        m_parametersLayout->addWidget(text, row, column + 1, 1, columnSpan);
        // children added to a visible widget are shown later, and the layout would not count them yet
        key->show();
        text->show();
    };
    int row = 0, column = 0;
    const QString model = m_prompt.setting({"Model"});
    if(!model.isEmpty())
        add(row++, 0, tr("Model"), model, 3, false);
    // in the order of A1111 webUI
    const QList<QPair<QString, QStringList>> parameters = {
        {tr("Sampler"), {"Sampler"}},
        {tr("Schedule type"), {"Schedule type", "Scheduler"}},
        {tr("Steps"), {"Steps"}},
        {tr("CFG"), {"CFG scale", "CFG"}},
        {tr("Shift"), {"Shift"}},
        {tr("Seed"), {"Seed", "Noise seed"}},
        {tr("Size"), {"Size"}},
    };
    for(int i = 0; i < parameters.size(); i++) {
        const QString value = m_prompt.setting(parameters[i].second);
        if(value.isEmpty())
            continue;
        add(row, column, parameters[i].first, value, 1, i == parameters.size() - 1);
        column = column == 0 ? 2 : 0;
        if(column == 0)
            row++;
    }
    for(int i = used; i < m_parameterKeys.size(); i++) {
        m_parameterKeys[i]->hide();
        m_parameterValues[i]->hide();
    }
    const bool any = used > 0;
    m_parameters->setVisible(any);
    m_rule->setVisible(any);
}

void PromptOverlay::updateVisibility()
{
    if(m_enabled && m_prompt.isValid()) {
        relayout();
        show();
        raise();
    } else {
        // the keys go back to the image
        QWidget* focus = QApplication::focusWidget();
        if(focus && isAncestorOf(focus))
            m_view->setFocus(Qt::OtherFocusReason);
        hide();
    }
}

void PromptOverlay::setAllSettingsVisible(bool visible)
{
    qApp->setPromptOverlayAllSettings(visible);
    m_settingsButton->setIcon(qApp->flatIcon(visible ? "chevron-down" : "chevron-right", LABEL_COLOR));
    m_settingsText->setVisible(visible);
    relayout();
}

void PromptOverlay::relayout()
{
    if(!m_prompt.isValid())
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
        PromptTextEdit* edit;
        int line;
        int preferred;
        int height;
    };
    QList<Text> texts;
    // the prompts are shown whole (maxLines 0), all the settings in a few lines with a scroll bar
    auto addText = [&](QWidget* box, QTextEdit* edit, int maxLines) {
        if(box->isHidden() || edit->isHidden())
            return;
        PromptTextEdit* text = static_cast<PromptTextEdit*>(edit);
        const PromptTextEdit::Heights heights = text->contentHeights(textWidth, qMax(1, maxLines));
        texts << Text{text, heights.line, qMax(heights.line, maxLines > 0 ? heights.lines : heights.all), 0};
    };
    addText(m_positiveBox, m_positiveText, 0);
    addText(m_negativeBox, m_negativeText, 0);
    addText(m_settingsBox, m_settingsText, SETTINGS_LINES);

    // the height of everything but the texts, whose current fixed heights are in the size hint.
    // The layouts of the sections update their size hints later (LayoutRequest), so they are invalidated here.
    // (The texts are not shrunk to 0 for this: that resized and laid them out at once, which was slow)
    for(int i = 0; i < layout()->count(); i++) {
        QWidget* child = layout()->itemAt(i)->widget();
        if(!child)
            continue;
        if(child->layout())
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

    const bool left = m_corner == Qt::TopLeftCorner || m_corner == Qt::BottomLeftCorner;
    const bool top = m_corner == Qt::TopLeftCorner || m_corner == Qt::TopRightCorner;
    int x = left ? bounds.left() + m_offset.x() : bounds.right() + 1 - m_offset.x() - width;
    int y = top ? bounds.top() + m_offset.y() : bounds.bottom() + 1 - m_offset.y() - height;
    x = qMax(bounds.left(), qMin(x, bounds.right() + 1 - width));
    y = qMax(bounds.top(), qMin(y, bounds.bottom() + 1 - height));
    setGeometry(x, y, width, height);

    // The invalidated layouts would place the children only with the posted LayoutRequest, and the overlay could be
    // painted before it with the new size and the old texts when the images are turned fast. So they are laid out now
    layout()->activate();
    for(QWidget* child : findChildren<QWidget*>()) {
        if(child->layout() && !child->isHidden())
            child->layout()->activate();
    }
}

QRect PromptOverlay::viewArea() const
{
    // the scroll bars are not covered
    return m_view->viewport()->geometry();
}

Qt::Edges PromptOverlay::edgesAt(const QPoint &pos) const
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

void PromptOverlay::updateEdgeCursor(Qt::Edges edges)
{
    setCursor(edges & (Qt::LeftEdge | Qt::RightEdge) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void PromptOverlay::updateAnchor(const QRect &rect)
{
    // the overlay stays at the nearest corner when the window is resized
    const QRect bounds = viewArea();
    const bool left = rect.center().x() < bounds.center().x();
    const bool top = rect.center().y() < bounds.center().y();
    m_corner = top ? (left ? Qt::TopLeftCorner : Qt::TopRightCorner) : (left ? Qt::BottomLeftCorner : Qt::BottomRightCorner);
    m_offset = QPoint(qMax(0, left ? rect.left() - bounds.left() : bounds.right() - rect.right()),
                      qMax(0, top ? rect.top() - bounds.top() : bounds.bottom() - rect.bottom()));
}

void PromptOverlay::storeGeometry()
{
    qApp->setPromptOverlayGeometry(m_customSize, int(m_corner), m_offset);
}

void PromptOverlay::resetGeometry()
{
    m_customSize = QSize();
    m_corner = Qt::BottomRightCorner;
    m_offset = QPoint(OVERLAY_MARGIN, OVERLAY_MARGIN);
    storeGeometry();
    relayout();
}

void PromptOverlay::copyText(const QString &text, const QString &message)
{
    QGuiApplication::clipboard()->setText(text);
    emit notify(message);
}

void PromptOverlay::setHovered(bool hovered)
{
    // darker under the mouse, so that long text stays readable over bright images
    setProperty("hovered", hovered);
    style()->unpolish(this);
    style()->polish(this);
    update();
}
