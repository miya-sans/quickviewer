#ifndef PROMPTOVERLAY_H
#define PROMPTOVERLAY_H

#include <QFrame>
#include <functional>

#include "promptreader.h"

class QAbstractScrollArea;
class QGridLayout;
class QLabel;
class QTextEdit;
class QToolButton;

/**
 * @brief The PromptOverlay class
 * Shows the prompts and the settings of an image made with Stable Diffusion over the image view.
 * It appears only for images with readable generation data.
 * It is moved with its header and resized with its edges, and both are remembered.
 * Double-clicking the header puts it back in the bottom right corner in the size of the text.
 */
class PromptOverlay : public QFrame
{
    Q_OBJECT
public:
    explicit PromptOverlay(QAbstractScrollArea* view);
    /**
     * @brief setPrompt shows the generation data of the displayed image (hidden when it is not valid)
     */
    void setPrompt(const PromptInfo& prompt);
    /**
     * @brief setOverlayEnabled shows or hides the overlay ('Show Prompt')
     */
    void setOverlayEnabled(bool enabled);

signals:
    /**
     * @brief notify a message for the notification, e.g. 'Copied Seed: 1111'
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

    QToolButton* createButton(const QString& icon, const QString& toolTip, std::function<void()> clicked);
    /**
     * @brief createSection a title row with a copy button shown under the mouse, and a text
     */
    QWidget* createSection(QWidget* title, const QString& copyToolTip, std::function<void()> copy, QTextEdit*& text);
    void fillPrompt(QTextEdit* edit, const QString& text, const PromptPairs& parts, const QColor& color);
    void rebuildParameters();
    void updateVisibility();
    void setAllSettingsVisible(bool visible);
    /**
     * @brief relayout sizes the texts and puts the overlay at its corner of the view
     */
    void relayout();
    QRect viewArea() const;
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateEdgeCursor(Qt::Edges edges);
    void updateAnchor(const QRect& rect);
    void storeGeometry();
    void resetGeometry();
    void copyText(const QString& text, const QString& message);
    void setHovered(bool hovered);

    QAbstractScrollArea* m_view;
    PromptInfo m_prompt;
    bool m_enabled;

    QWidget* m_header;
    QLabel* m_toolLabel;
    QWidget* m_positiveBox;
    QTextEdit* m_positiveText;
    QWidget* m_negativeBox;
    QTextEdit* m_negativeText;
    QWidget* m_rule;
    QWidget* m_parameters;
    QGridLayout* m_parametersLayout;
    // the labels of the settings grid, reused for each image (the shown ones come first)
    QList<QLabel*> m_parameterKeys;
    QList<QWidget*> m_parameterValues;
    QWidget* m_settingsBox;
    QToolButton* m_settingsButton;
    QTextEdit* m_settingsText;

    // the size set by the user (invalid when it fits the text) and the distance from the nearest corner of the view
    QSize m_customSize;
    Qt::Corner m_corner;
    QPoint m_offset;
    int m_minimumHeight;

    DragMode m_dragMode;
    Qt::Edges m_resizeEdges;
    QPoint m_pressGlobalPos;
    QRect m_pressGeometry;
};

#endif // PROMPTOVERLAY_H
