#ifndef MAINWINDOWFORWINDOWS_H
#define MAINWINDOWFORWINDOWS_H

#include "mainwindow.h"
#include <Windows.h>

class MainWindowForWindows : public MainWindow
{
    Q_OBJECT
public:
    MainWindowForWindows(QWidget *parent = 0);
    virtual bool moveToTrush(QString path) override;
    virtual bool setStayOnTop(bool top) override;
    virtual void setWindowTop(bool signalOnly) override;
    virtual void setMailAttachment(QString path) override;
    virtual bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool event(QEvent *e) override;
    void updateTitleBar() override;

public slots:

private:
    /**
     * @brief applyTitleBar removes the system title bar (the menu bar is the title bar), or brings it back
     */
    void applyTitleBar();
    /**
     * @brief hitTest answers WM_NCHITTEST: the edges resize the window, and the empty part of the menu bar moves it
     */
    LRESULT hitTest(HWND hwnd, LPARAM lParam) const;

    bool bFirstView;
    HMENU m_showMainMenu; // HMENU
    /**
     * @brief m_customTitleBar the menu bar is also the title bar, the system title bar is removed
     */
    bool m_customTitleBar;

    static MainWindowForWindows* MainWindowForWindows_self;
    static WNDPROC DefStaticProc;
    static LRESULT CALLBACK StaticProc(HWND hwnd , UINT msg , WPARAM wp , LPARAM lp);
};

#endif // MAINWINDOWFORWINDOWS_H
