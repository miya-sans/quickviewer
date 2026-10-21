#ifndef TOASTNOTIFICATION_H
#define TOASTNOTIFICATION_H

#include <QFrame>
#include <QTimer>

class QLabel;
class QPushButton;

/**
 * @brief The ToastNotification class
 * A small message shown over the image view, which hides itself after a while.
 * It can have a button to open a folder (e.g. the destination of 'Copy to Folder').
 */
class ToastNotification : public QFrame
{
    Q_OBJECT
public:
    explicit ToastNotification(QWidget* parent);
    /**
     * @brief showMessage shows the message on the upper left of the parent
     * @param title first line, e.g. 'Copied "page01.jpg" to'
     * @param folder shown on the second line and opened with the button. When it is empty, they are hidden
     * @param fileToSelect selected in the file manager when the folder is opened
     * @param openFolderButton shows the button to open the folder
     */
    void showMessage(const QString& title, const QString& folder = QString(), const QString& fileToSelect = QString(), bool openFolderButton = true);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onOpenFolderButton_clicked();

private:
    void reposition();

    QLabel* m_titleLabel;
    QLabel* m_folderLabel;
    QPushButton* m_openFolderButton;
    QTimer m_hideTimer;
    QString m_folder;
    QString m_fileToSelect;
};

#endif // TOASTNOTIFICATION_H
