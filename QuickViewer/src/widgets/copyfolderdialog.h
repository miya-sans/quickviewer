#ifndef COPYFOLDERDIALOG_H
#define COPYFOLDERDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QDialogButtonBox;

/**
 * @brief The CopyFolderDialog class
 * Selects the destination folder of 'Copy to Folder'.
 * The path can be pasted into the text box, or selected with the folder picker of the OS.
 */
class CopyFolderDialog : public QDialog
{
    Q_OBJECT
public:
    CopyFolderDialog(QWidget* parent, const QString& folder);
    /**
     * @brief folder the selected folder (the path is cleaned, e.g. quotes of 'Copy as path' are removed)
     */
    QString folder() const;

private slots:
    void onBrowseButton_clicked();
    void onPathEdit_textChanged();

private:
    QLineEdit* m_pathEdit;
    QLabel* m_messageLabel;
    QDialogButtonBox* m_buttons;
};

#endif // COPYFOLDERDIALOG_H
