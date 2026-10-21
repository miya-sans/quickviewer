#ifndef COPYFOLDERDIALOG_H
#define COPYFOLDERDIALOG_H

#include <QDialog>

class QComboBox;
class QLabel;
class QDialogButtonBox;

/**
 * @brief The CopyFolderDialog class
 * Selects the destination folder of 'Copy to Folder'.
 * The path can be pasted into the box, chosen from the drop down list
 * (recent destinations and folders in the history), or selected with the folder picker of the OS.
 */
class CopyFolderDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param folder the initial folder
     * @param recentFolders listed first, e.g. recent destinations
     * @param otherFolders listed after a separator, e.g. folders in the history
     */
    CopyFolderDialog(QWidget* parent, const QString& folder, const QStringList& recentFolders, const QStringList& otherFolders);
    /**
     * @brief folder the selected folder (the path is cleaned, e.g. quotes of 'Copy as path' are removed)
     */
    QString folder() const;

private slots:
    void onBrowseButton_clicked();
    void onPath_textChanged();

private:
    QComboBox* m_pathCombo;
    QLabel* m_messageLabel;
    QDialogButtonBox* m_buttons;
};

#endif // COPYFOLDERDIALOG_H
