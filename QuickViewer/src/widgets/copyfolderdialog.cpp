#include <QtWidgets>

#include "copyfolderdialog.h"

CopyFolderDialog::CopyFolderDialog(QWidget *parent, const QString &folder, const QStringList &recentFolders, const QStringList &otherFolders)
    : QDialog(parent)
    , m_pathCombo(new QComboBox(this))
    , m_messageLabel(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Copy to Folder", "Title of the dialog to select the destination folder of 'Copy to Folder'"));
    setMinimumWidth(560);

    QLabel* label = new QLabel(tr("Folder to copy images to:", "Label of the destination folder of 'Copy to Folder'"), this);

    m_pathCombo->setEditable(true);
    // the entered path must not be added to the list, and must not be completed with a longer folder name
    m_pathCombo->setInsertPolicy(QComboBox::NoInsert);
    m_pathCombo->setCompleter(nullptr);
    m_pathCombo->setMaxVisibleItems(20);
    m_pathCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pathCombo->setMinimumContentsLength(40);
    foreach(const QString& path, recentFolders)
        m_pathCombo->addItem(QDir::toNativeSeparators(path));
    if(!recentFolders.isEmpty() && !otherFolders.isEmpty())
        m_pathCombo->insertSeparator(m_pathCombo->count());
    foreach(const QString& path, otherFolders)
        m_pathCombo->addItem(QDir::toNativeSeparators(path));
    m_pathCombo->setEditText(QDir::toNativeSeparators(folder));
    m_pathCombo->lineEdit()->setPlaceholderText(tr("Paste a folder path, choose one from the list, or select it with Browse...", "Placeholder of the destination folder of 'Copy to Folder'"));
    m_pathCombo->lineEdit()->setClearButtonEnabled(true);
    m_pathCombo->lineEdit()->selectAll();

    QPushButton* browseButton = new QPushButton(tr("Browse...", "Button to open the folder picker of 'Copy to Folder'"), this);
    browseButton->setAutoDefault(false);

    QHBoxLayout* pathLayout = new QHBoxLayout;
    pathLayout->addWidget(m_pathCombo, 1);
    pathLayout->addWidget(browseButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addLayout(pathLayout);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_buttons);

    connect(browseButton, SIGNAL(clicked()), this, SLOT(onBrowseButton_clicked()));
    connect(m_pathCombo, SIGNAL(editTextChanged(QString)), this, SLOT(onPath_textChanged()));
    connect(m_buttons, SIGNAL(accepted()), this, SLOT(accept()));
    connect(m_buttons, SIGNAL(rejected()), this, SLOT(reject()));
    onPath_textChanged();
}

QString CopyFolderDialog::folder() const
{
    QString path = m_pathCombo->currentText().trimmed();
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
        m_pathCombo->setEditText(QDir::toNativeSeparators(selected));
}

void CopyFolderDialog::onPath_textChanged()
{
    QString path = folder();
    bool valid = !path.isEmpty() && QFileInfo(path).isDir();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    m_messageLabel->setText(path.isEmpty() || valid ? QString()
        : tr("The folder does not exist", "Message of the dialog of 'Copy to Folder'"));
    m_messageLabel->setVisible(!m_messageLabel->text().isEmpty());
}
