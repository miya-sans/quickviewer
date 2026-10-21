#include <Windows.h>
#include <dwmapi.h>
#include <mapi.h>
#include <Shellapi.h>
#include <QtCore>

#include "qvapplication.h"
#include "mainwindowforwindows.h"
#include "ui_mainwindow.h"

#define IDM_SHOWMAINMENU 2
// width of the edges that resize the window, which has no visible frame
#define RESIZE_BORDER 5

MainWindowForWindows* MainWindowForWindows::MainWindowForWindows_self;
WNDPROC MainWindowForWindows::DefStaticProc;

MainWindowForWindows::MainWindowForWindows(QWidget *parent)
    : MainWindow(parent), bFirstView(true), m_customTitleBar(false)
{
    QString menutxt = tr("Sh&ow/Hide MainMenuBar", "An additional menu displayed when right-clicking the title bar, which is used when undoing the main menu when it is hidden");
    menutxt = ui->actionShowMainMenuBar->text();

    MainWindowForWindows_self = this;

    // The menu bar is also the title bar. The window was created with the system title bar
    // around the restored geometry, so the geometry is restored again without it
    updateTitleBar();
    if(m_customTitleBar && !isFullScreen()) {
        setWindowState(Qt::WindowNoState);
        restoreWindowGeometry();
    }
}

void MainWindowForWindows::updateTitleBar()
{
    // without the menu bar, the system title bar is used
    bool custom = qApp->ShowMenuBar();
    setWindowButtonsVisible(custom && !isFullScreen());
    if(custom != m_customTitleBar) {
        m_customTitleBar = custom;
        applyTitleBar();
    }
}

void MainWindowForWindows::applyTitleBar()
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    // a frame of 1 pixel under the contents keeps the shadow of the window
    const MARGINS margins = m_customTitleBar ? MARGINS{1, 1, 1, 1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // calculate the client area again (WM_NCCALCSIZE)
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

bool MainWindowForWindows::event(QEvent *e)
{
    // a new native window has the system title bar again
    if(e->type() == QEvent::WinIdChange && m_customTitleBar)
        QTimer::singleShot(0, this, [this]{ applyTitleBar(); });
    return MainWindow::event(e);
}

// A maximized window without a title bar would cover the whole monitor when the taskbar hides automatically
// (or the monitor has none): Qt takes that for fullscreen, and the taskbar could not be shown.
// One pixel is left at the edge of the taskbar
static void leaveTaskbarEdge(RECT* rect, const RECT& monitor)
{
    for(UINT edge : {ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT}) {
        APPBARDATA bar = {};
        bar.cbSize = sizeof(APPBARDATA);
        bar.uEdge = edge;
        bar.rc = monitor;
        if(!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar))
            continue;
        switch(edge) {
        case ABE_TOP: rect->top++; return;
        case ABE_LEFT: rect->left++; return;
        case ABE_RIGHT: rect->right--; return;
        default: rect->bottom--; return;
        }
    }
    rect->bottom--;
}

bool MainWindowForWindows::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG* msg = static_cast<MSG*>(message);
    // in fullscreen the window has no caption style
    if(m_customTitleBar && (GetWindowLongPtr(msg->hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        switch(msg->message) {
        case WM_NCCALCSIZE: {
            // the whole window is the client area: there is no title bar and no visible frame
            RECT* rect = msg->wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0]
                                     : reinterpret_cast<RECT*>(msg->lParam);
            // a maximized window is larger than the screen by its invisible frame
            if(IsZoomed(msg->hwnd)) {
                MONITORINFO info = {};
                info.cbSize = sizeof(MONITORINFO);
                if(GetMonitorInfo(MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST), &info)) {
                    IntersectRect(rect, rect, &info.rcWork);
                    if(EqualRect(rect, &info.rcMonitor))
                        leaveTaskbarEdge(rect, info.rcMonitor);
                }
            }
            *result = 0;
            return true;
        }
        case WM_NCHITTEST:
            *result = hitTest(msg->hwnd, msg->lParam);
            return true;
        default:
            break;
        }
    }
    return MainWindow::nativeEvent(eventType, message, result);
}

LRESULT MainWindowForWindows::hitTest(HWND hwnd, LPARAM lParam) const
{
    POINT pt = { short(LOWORD(lParam)), short(HIWORD(lParam)) };
    ScreenToClient(hwnd, &pt);
    RECT client;
    GetClientRect(hwnd, &client);
    const qreal dpr = devicePixelRatioF();
    if(!IsZoomed(hwnd)) {
        const int border = qRound(RESIZE_BORDER * dpr);
        const bool left = pt.x < border;
        const bool right = pt.x >= client.right - border;
        const bool top = pt.y < border;
        const bool bottom = pt.y >= client.bottom - border;
        if(top && left) return HTTOPLEFT;
        if(top && right) return HTTOPRIGHT;
        if(bottom && left) return HTBOTTOMLEFT;
        if(bottom && right) return HTBOTTOMRIGHT;
        if(left) return HTLEFT;
        if(right) return HTRIGHT;
        if(top) return HTTOP;
        if(bottom) return HTBOTTOM;
    }
    // the empty part of the menu bar moves the window and maximizes it with a double click, like a title bar
    const QPoint pos = ui->menuBar->mapFrom(this, QPoint(qFloor(pt.x / dpr), qFloor(pt.y / dpr)));
    if(ui->menuBar->isVisible() && ui->menuBar->rect().contains(pos)
       && !ui->menuBar->actionAt(pos) && !ui->menuBar->childAt(pos))
        return HTCAPTION;
    return HTCLIENT;
}

bool MainWindowForWindows::moveToTrush(QString path)
{
    // pFrom must be terminated with two null characters, and the path may be longer than MAX_PATH
    std::wstring from = QDir::toNativeSeparators(path).toStdWString();
    from.push_back(L'\0');
    SHFILEOPSTRUCT fileop = {0};
    fileop.wFunc = FO_DELETE;
    fileop.pFrom = from.c_str();
    fileop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    int rv = SHFileOperation( &fileop );
    if( 0 != rv ){
        qDebug() << rv << QString::number( rv ).toInt( 0, 8 );
        return false;
    }

    qDebug() << rv << path;
    return true;
}

bool MainWindowForWindows::setStayOnTop(bool top)
{
    auto hwnd = reinterpret_cast<HWND>(winId());
    if(!hwnd) return false;
    // Place window A on top
    if(top)
        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE);
    else
        ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE);
    return true;
}

void MainWindowForWindows::setWindowTop(bool signalOnly)
{
    auto hwnd = reinterpret_cast<HWND>(winId());
    if(!hwnd) return;
    if(!isMaximized())
        ::ShowWindow(hwnd,SW_RESTORE);
//    ::SwitchToThisWindow(hwnd, false);
    int foregroundID;
    if(!signalOnly) {
        foregroundID = ::GetWindowThreadProcessId( ::GetForegroundWindow(), NULL);
        ::AttachThreadInput( ::GetCurrentThreadId(), foregroundID, TRUE);
    }

    ::SetForegroundWindow(hwnd);
    ::SetActiveWindow(hwnd);
    ::SetFocus(hwnd);
    ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE);

    if(!signalOnly) {
        ::AttachThreadInput( ::GetCurrentThreadId(), foregroundID, FALSE);
    }
}

static LPSTR qStringToLPSTR(const QString& qString)
{
    // return qString.toLocal8Bit().data();
    return qString.toLatin1().data();
}

static QString mapi32path()
{
    // // try to determine default Simple MAPI handler
    // // first, look for current user's default client
    // registry::tstring default_client =  registry::const_key(HKEY_CURRENT_USER, "Software\\Clients\\Mail")[""].reg_sz("");
    //
    // if (default_client.empty()) {
    // 	// then look for machine-wide settings
    // 	default_client =  registry::const_key(HKEY_LOCAL_MACHINE, "Software\\Clients\\Mail")[""].reg_sz("");
    // }
    //
    // if (!default_client.empty()) {
    // 	registry::const_key regClient(HKEY_LOCAL_MACHINE, registry::tstring("Software\\Clients\\Mail\\" + default_client).c_str());
    // 	registry::tstring s = regClient["DLLPath"].reg_sz("");
    //
    // 	if (s.empty())
    // 		s = regClient["DLLPathEx"].reg_sz("");
    //
    // 	if (!s.empty())
    // 		dllpath = s;
    // }

    return "mapi32";
}

void MainWindowForWindows::setMailAttachment(QString path)
{
    QLibrary lib("mapi32");
#ifdef _MSC_VER
    if( LPMAPISENDMAILW mapi = LPMAPISENDMAILW(lib.resolve("MAPISendMailW")) )
    {
        QString filePath = QDir::toNativeSeparators( path );
        QString fileName = QFileInfo( path ).fileName();
//        QString subject = q.queryItemValue( "subject", QUrl::FullyDecoded );
        MapiFileDescW doc = { 0, 0, 0, 0, 0, 0 };
        doc.nPosition = -1;
        doc.lpszPathName = PWSTR(filePath.utf16());
        doc.lpszFileName = PWSTR(fileName.utf16());
        MapiMessageW message = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
//        message.lpszSubject = PWSTR(subject.utf16());
//        message.lpszNoteText = L"";
        message.nFileCount = 1;
        message.lpFiles = lpMapiFileDescW(&doc);
        switch( mapi( NULL, 0, &message, MAPI_LOGON_UI|MAPI_DIALOG, 0 ) )
        {
        case SUCCESS_SUCCESS:
        case MAPI_E_USER_ABORT:
        case MAPI_E_LOGIN_FAILURE:
            return;
        default: break;
        }
    }
    else
#endif
        if( LPMAPISENDMAIL mapi = LPMAPISENDMAIL(lib.resolve("MAPISendMail")) )
    {
        QByteArray filePath = QDir::toNativeSeparators( path ).toLocal8Bit();
        QByteArray fileName = QFileInfo( path ).fileName().toLocal8Bit();
//        QByteArray subject = q.queryItemValue( "subject", QUrl::FullyDecoded ).toLocal8Bit();
        MapiFileDesc doc = { 0, 0, 0, 0, 0, 0 };
        doc.nPosition = -1;
        std::string flpath = filePath.toStdString();
        std::string flname = fileName.toStdString();
        doc.lpszPathName = LPSTR(flpath.c_str());
        doc.lpszFileName = LPSTR(flname.c_str());
        MapiMessage message = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
//        message.lpszSubject = LPSTR(subject.constData());
//        message.lpszNoteText = "";
        message.nFileCount = 1;
        message.lpFiles = lpMapiFileDesc(&doc);
        switch( mapi( NULL, 0, &message, MAPI_LOGON_UI|MAPI_DIALOG, 0 ) )
        {
        case SUCCESS_SUCCESS:
        case MAPI_E_USER_ABORT:
        case MAPI_E_LOGIN_FAILURE:
            return;
        default: break;
        }
        }
}

bool MainWindowForWindows::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Leave:
//        if(obj == ui->pageFrame && isFullScreen()) {
//            ui->pageFrame->hide();
//            return true;
//        }
        return QObject::eventFilter(obj, event);
    default:
        break;
    }
    return MainWindow::eventFilter(obj, event);
}


// // This method is used because QShowEvent may not be called
// bool MainWindowForWindows::nativeEvent(const QByteArray &, void *, long *)
// {
//     if(!bFirstView)
//         return false;

// //    int mes = reinterpret_cast<int>(message);
//     auto hwnd = reinterpret_cast<HWND>(winId());
//     m_showMainMenu = ::GetSystemMenu(hwnd, false);

//     QString menutxt = ui->actionShowMainMenuBar->text();

//     ::AppendMenu(m_showMainMenu, MF_SEPARATOR, IDM_SHOWMAINMENU-1, 0);
//     ::AppendMenu(m_showMainMenu, MF_STRING, IDM_SHOWMAINMENU,
//                  reinterpret_cast<LPCWSTR>(menutxt.utf16()));

// #ifdef _WIN64
//     DefStaticProc = (WNDPROC)GetWindowLongPtr(hwnd , GWLP_WNDPROC);
//     SetWindowLongPtr(hwnd , GWLP_WNDPROC , reinterpret_cast<LONG_PTR>(StaticProc));
// #else
//     DefStaticProc = (WNDPROC)GetWindowLongPtr(hwnd , GWL_WNDPROC);
//     SetWindowLongPtr(hwnd , GWL_WNDPROC , reinterpret_cast<LONG_PTR>(StaticProc));
// #endif
//     bFirstView = false;
//     return false;
// }


LRESULT CALLBACK MainWindowForWindows::StaticProc(HWND hwnd , UINT msg , WPARAM wp , LPARAM lp) {
    switch(msg) {
    case WM_SYSCOMMAND:
        if (LOWORD(wp) == IDM_SHOWMAINMENU) {
                if(qApp->ShowMenuBar()) {
                    MainWindowForWindows_self->ui->actionShowMenuBar->setChecked(false);
                    MainWindowForWindows_self->ui->actionShowMenuBar->triggered(false);
                }
                else {
                    MainWindowForWindows_self->ui->actionShowMenuBar->setChecked(true);
                    MainWindowForWindows_self->ui->actionShowMenuBar->triggered(true);
                }
            return 0;
        }
    }
    return CallWindowProc(DefStaticProc , hwnd , msg , wp , lp);
}
