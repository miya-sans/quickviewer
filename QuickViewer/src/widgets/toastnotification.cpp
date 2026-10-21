#include <QtWidgets>

#include "toastnotification.h"

#define TOAST_MARGIN 12
#define TOAST_DURATION 4000
#define TOAST_DURATION_AFTER_HOVER 2000

// The same appearance is used for every theme, like the HUD of an image viewer
static const char* TOAST_STYLESHEET =
    "#ToastNotification { background-color: rgba(0, 0, 0, 190); border: 1px solid rgba(255, 255, 255, 40); }"
    "#ToastNotification QLabel { color: #dddddd; background: transparent; border: none; }"
    "#ToastNotification QLabel#toastFolder { color: #ffffff; font-weight: bold; }"
    "#ToastNotification QPushButton { color: #dddddd; background-color: rgba(255, 255, 255, 30);"
    " border: 1px solid rgba(255, 255, 255, 60); padding: 3px 12px; }"
    "#ToastNotification QPushButton:hover { background-color: rgba(255, 255, 255, 70); }"
    "#ToastNotification QPushButton:pressed { background-color: rgba(255, 255, 255, 110); }";

ToastNotification::ToastNotification(QWidget *parent)
    : QFrame(parent)
    , m_titleLabel(new QLabel(this))
    , m_folderLabel(new QLabel(this))
    , m_openFolderButton(new QPushButton(tr("Open folder", "Button on the notification of 'Copy to Folder' to open the destination folder"), this))
{
    setObjectName("ToastNotification");
    setStyleSheet(TOAST_STYLESHEET);
    m_folderLabel->setObjectName("toastFolder");
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openFolderButton->setCursor(Qt::PointingHandCursor);
    m_openFolderButton->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(m_titleLabel);
    texts->addWidget(m_folderLabel);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(16);
    layout->addLayout(texts);
    layout->addWidget(m_openFolderButton, 0, Qt::AlignVCenter);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, SIGNAL(timeout()), this, SLOT(hide()));
    connect(m_openFolderButton, SIGNAL(clicked()), this, SLOT(onOpenFolderButton_clicked()));
    parent->installEventFilter(this);
    hide();
}

void ToastNotification::showMessage(const QString &title, const QString &folder, const QString &fileToSelect)
{
    m_folder = folder;
    m_fileToSelect = fileToSelect;
    m_titleLabel->setText(title);
    QString nativeFolder = QDir::toNativeSeparators(folder);
    int maxTextWidth = qMax(200, parentWidget()->width() - 2*TOAST_MARGIN - 200);
    m_folderLabel->setText(m_folderLabel->fontMetrics().elidedText(nativeFolder, Qt::ElideMiddle, maxTextWidth));
    m_folderLabel->setToolTip(nativeFolder);
    m_folderLabel->setVisible(!folder.isEmpty());
    m_openFolderButton->setVisible(!folder.isEmpty());
    adjustSize();
    reposition();
    show();
    raise();
    m_hideTimer.start(TOAST_DURATION);
}

bool ToastNotification::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == parentWidget() && event->type() == QEvent::Resize)
        reposition();
    return QFrame::eventFilter(obj, event);
}

void ToastNotification::enterEvent(QEnterEvent *event)
{
    // keep the notification while the mouse is on it
    m_hideTimer.stop();
    QFrame::enterEvent(event);
}

void ToastNotification::leaveEvent(QEvent *event)
{
    if(isVisible())
        m_hideTimer.start(TOAST_DURATION_AFTER_HOVER);
    QFrame::leaveEvent(event);
}

void ToastNotification::onOpenFolderButton_clicked()
{
    hide();
#if defined(Q_OS_WIN)
    if(!m_fileToSelect.isEmpty() && QFileInfo::exists(m_fileToSelect)) {
        // explorer wants the quotes only around the path, so the native arguments are used
        QProcess explorer;
        explorer.setProgram("explorer.exe");
        explorer.setNativeArguments(QString("/select,\"%1\"").arg(QDir::toNativeSeparators(m_fileToSelect)));
        explorer.startDetached();
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
}

void ToastNotification::reposition()
{
    move(TOAST_MARGIN, TOAST_MARGIN);
}
