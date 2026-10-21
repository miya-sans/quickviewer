#include <QtWidgets>

#include "copyfolderdialog.h"

CopyFolderDialog::CopyFolderDialog(QWidget *parent, const QString &folder)
    : QDialog(parent)
    , m_pathEdit(new QLineEdit(this))
    , m_messageLabel(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Copy to Folder", "Title of the dialog to select the destination folder of 'Copy to Folder'"));
    setMinimumWidth(520);

    QLabel* label = new QLabel(tr("Folder to copy images to:", "Label of the destination folder of 'Copy to Folder'"), this);
    m_pathEdit->setText(QDir::toNativeSeparators(folder));
    m_pathEdit->setPlaceholderText(tr("Paste a folder path, or select it with Browse...", "Placeholder of the destination folder of 'Copy to Folder'"));
    m_pathEdit->setClearButtonEnabled(true);
    m_pathEdit->selectAll();

    // complete folder names while typing
    QFileSystemModel* model = new QFileSystemModel(this);
    model->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    model->setRootPath(QString());
    QCompleter* completer = new QCompleter(model, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_pathEdit->setCompleter(completer);

    QPushButton* browseButton = new QPushButton(tr("Browse...", "Button to open the folder picker of 'Copy to Folder'"), this);
    browseButton->setAutoDefault(false);

    QHBoxLayout* pathLayout = new QHBoxLayout;
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(browseButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addLayout(pathLayout);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_buttons);

    connect(browseButton, SIGNAL(clicked()), this, SLOT(onBrowseButton_clicked()));
    connect(m_pathEdit, SIGNAL(textChanged(QString)), this, SLOT(onPathEdit_textChanged()));
    connect(m_buttons, SIGNAL(accepted()), this, SLOT(accept()));
    connect(m_buttons, SIGNAL(rejected()), this, SLOT(reject()));
    onPathEdit_textChanged();
}

QString CopyFolderDialog::folder() const
{
    QString path = m_pathEdit->text().trimmed();
    // 'Copy as path' of Explorer encloses the path with quotes
    if(path.length() >= 2 && path.startsWith('"') && path.endsWith('"'))
        path = path.mid(1, path.length()-2).trimmed();
    if(path.isEmpty())
        return path;
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

void CopyFolderDialog::onBrowseButton_clicked()
{
    QString start = folder();
    QString selected = QFileDialog::getExistingDirectory(this,
        tr("Select the folder to copy images to", "Title of the dialog to select the destination folder of 'Copy to Folder'"),
        QDir(start).exists() ? start : QString());
    if(!selected.isEmpty())
        m_pathEdit->setText(QDir::toNativeSeparators(selected));
}

void CopyFolderDialog::onPathEdit_textChanged()
{
    QString path = folder();
    bool valid = !path.isEmpty() && QFileInfo(path).isDir();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    m_messageLabel->setText(path.isEmpty() || valid ? QString()
        : tr("The folder does not exist", "Message of the dialog of 'Copy to Folder'"));
    m_messageLabel->setVisible(!m_messageLabel->text().isEmpty());
}
