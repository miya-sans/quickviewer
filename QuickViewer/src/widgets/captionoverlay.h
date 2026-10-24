#ifndef CAPTIONOVERLAY_H
#define CAPTIONOVERLAY_H

#include <QFrame>
#include <functional>

#include "captionfile.h"

class QAbstractScrollArea;
class QLabel;
class QLineEdit;
class QTextEdit;
class QToolButton;
class QVBoxLayout;

/**
 * @brief The CaptionOverlay class
 * Edits the caption file of the displayed image (the ".txt" file with the same name) over the image view.
 * Each caption candidate of a file split by "---===---" lines has its own box, which can be added, removed,
 * moved and named (the comment of its separator line).
 * It looks and moves like PromptOverlay, and is put at the bottom left by default (the prompt is at the bottom right).
 * Ctrl+S saves the caption, and the changes are also saved when the image changes or the editor is hidden
 */
class CaptionOverlay : public QFrame
{
    Q_OBJECT
public:
    explicit CaptionOverlay(QAbstractScrollArea* view);
    /**
     * @brief setImage edits the caption of an image file (an empty path hides the editor, e.g. for archives)
     */
    void setImage(const QString& imagePath);
    /**
     * @brief setOverlayEnabled shows or hides the editor ('Show Caption Editor')
     */
    void setOverlayEnabled(bool enabled);
    /**
     * @brief isModified the caption in the editor differs from the file
     */
    bool isModified() const;
    /**
     * @brief saveChanges saves the caption when it was modified and its image still exists
     */
    void saveChanges();

signals:
    /**
     * @brief notify a message for the notification, e.g. 'Saved image.txt'
     */
    void notify(const QString& message);
    /**
     * @brief hideRequested the close button was pushed
     */
    void hideRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum DragMode { NoDrag, Moving, Resizing };
    /**
     * @brief The Candidate struct the widgets of a caption candidate
     */
    struct Candidate {
        QWidget* box;
        QWidget* titleRow;
        QLineEdit* name;
        QTextEdit* text;
        QToolButton* up;
        QToolButton* down;
        QToolButton* copy;
        QToolButton* remove;
    };

    QToolButton* createButton(const QString& icon, const QString& toolTip, std::function<void()> clicked);
    Candidate createCandidate();
    int indexOf(QWidget* box) const;
    void addCandidate();
    void removeCandidate(QWidget* box);
    void moveCandidate(QWidget* box, int delta);
    QList<CaptionCandidate> editedCandidates() const;
    /**
     * @brief load reads the caption file of the image into the boxes
     */
    void load();
    void save();
    /**
     * @brief updateState shows whether the caption is modified, the titles and the buttons of the boxes
     */
    void updateState();
    void updateButtons(QWidget* box, bool hovered);
    void updateVisibility();
    bool hasEditFocus() const;
    void focusText(int index);
    /**
     * @brief relayout sizes the texts and puts the editor at its place in the view
     */
    void relayout();
    QRect viewArea() const;
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateEdgeCursor(Qt::Edges edges);
    void updateAnchor(const QRect& rect, bool snap);
    void storeGeometry();
    void resetGeometry();
    void setHovered(bool hovered);

    QAbstractScrollArea* m_view;
    bool m_enabled;
    QString m_imagePath;
    CaptionFile m_file;

    QWidget* m_header;
    QLabel* m_fileLabel;
    QToolButton* m_saveButton;
    QWidget* m_boxes;
    QVBoxLayout* m_boxesLayout;
    QList<Candidate> m_candidates;

    // the size set by the user (invalid when it fits the text), the side of the view it stays at and the distance from it
    QSize m_customSize;
    Qt::Alignment m_alignment;
    QPoint m_offset;
    int m_minimumHeight;

    DragMode m_dragMode;
    Qt::Edges m_resizeEdges;
    QPoint m_pressGlobalPos;
    QRect m_pressGeometry;
};

#endif // CAPTIONOVERLAY_H
