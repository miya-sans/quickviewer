# QuickViewer fork: notes for the next session

This is a personal fork of QuickViewer (Qt 6, C++, qmake) maintained for one Windows user. The original project is discontinued. Read this file before changing anything; it records what was changed, why, and how to build and test.

## The user

- Windows on a 4K ultrawide monitor (3440 logical pixels wide at about 150% scaling).
- **Priority is minimalism.** The look and feel follows nomacs: a compact dark Flat theme with neutral greys (no color accents), and small 16px monochrome icons.
- **Bars.** Keep the layout of commit a7610f0: system title bar, menu bar and toolbar at the top; the page slider row and the status bar at the bottom. The user tried merged and single-bar layouts and asked to go back.
- Every round ends with a Windows build from GitHub Actions. Give the user the artifact download link (`QuickViewer-windows-x64`) after checking it starts.
- The user installs into `Program Files`, so settings live in `%APPDATA%\QuickViewer\quickviewer.ini` there. A portable unzip keeps the ini next to the exe. User preferences are code defaults, not a bundled ini (a bundled ini did not work for them).
- Never commit the user's own ini files or history.

## Branches

- The default branch is `master` (there is no `main`). Work happens on a `claude/...` branch with a PR into `master`.
- CI: `.github/workflows/windows-build.yml` builds with MSVC 2022, Qt 6.8.3 and jom, packages with windeployqt and uploads `QuickViewer-windows-x64`. It runs on every push and PR.

## Changes in this fork (newest first)

1. **Layout (as of a7610f0, which the user chose to keep).**
   - The standard Windows title bar, the menu bar and the toolbar are at the top. The bottom has two lines: the page frame (`(64/89)` and the slider) and the status bar with the image information.
   - Two layouts were tried and reverted at the user's request: the information and slider in the top bars (73a734f), and the menu bar merged with the title bar through `WM_NCCALCSIZE`/`WM_NCHITTEST` (e459089, b0c052e). Their notes are in the git history if that idea comes back.
2. **Colors.** The Flat theme uses neutral greys: the slider fill, selections, tabs and tags (no blue accent).
3. **Zoom.**
   - **Keys** (`ImageView::zoomBy`): zoom in and zoom out multiply the displayed scale by 1.25, starting from the fitted scale. A step that passes 100%, or ends within 10% of it, stops at 100%. Limits are 2% to 1600%.
   - **Mouse wheel** (`ImageView::zoomByWheel`): the plain wheel, Ctrl+wheel and right button+wheel zoom finely, 10% per notch. Smooth wheels and touchpads zoom by their fraction of a notch, and only passing 100% stops at 100%.
     - Pages are turned with the keys and the mouse back/forward buttons. Settings revision 5 migrates the former default wheel bindings.
     - With "Scroll with cursor when zooming" off, the point under the cursor stays in place. It is on by default, and then the mouse position scrolls the zoomed image.
   - **Status text:** the zoom in the status text updates on every zoom step (`zoomingChanged` also fires when the drawn scale changes).
   - **Remembered:** the manual zoom is `QVApplication::ZoomScale()` (`View/ZoomScale`), relative to the *original* image. It is kept when turning pages, opening files and restarting. Large images may be loaded shrunk; `PageContent::sourceRatio()` is loaded size ÷ original size.
4. **Fitting.**
   - Fitting is remembered (`View/Fitting`): opening a file or starting the app keeps the last fitting and zoom. It is not forced on.
   - F, M, F12 and Num+* toggle fitting. Fit also enlarges small images (`DontEnlargeSmallImagesOnFitting=false`).
5. **Window.** Size, position and maximized state are restored like a web browser, but never fullscreen. A first start opens at 3/4 × 4/5 of the screen, centered.
6. **Keys.** Esc closes the window (or leaves fullscreen).
7. **Application icon.** A lighter greyscale version of the original icon (`QuickViewer/icons/appicon.ico`).
8. **Delete.**
   - Del and Q delete without asking.
   - A toast notification shows the deleted path (`widgets/toastnotification.*`).
9. **Copy to Folder.**
   - E copies the image to a folder and Shift+E chooses that folder. With no folder set, the dialog asks first.
   - The dialog (`widgets/copyfolderdialog.*`) is an editable dropdown: paste a path, pick a recent copy folder or a folder from the viewing history, or use Browse.
   - The folder is saved immediately, and the toast has an "Open folder" button.
10. **Flat theme.**
   - `themes/Flat.qss` is the default theme. Any theme other than "Default" forces the Fusion style; native Windows styles ignore the compact padding.
   - The icons are monochrome SVGs in `icons/flat/` and `themes/flat/`, colored at runtime by `QVApplication::flatIcon()` from the `icon-color:` comment in the qss.
11. **Startup.** The folder panel is off at startup. A saved "Stay on top" is applied at startup.
12. **Stability and performance.**
    - Deleting the last image no longer crashes (`PageManager::reloadVolume`, null guards).
    - The folder is watched with `QFileSystemWatcher`, so added and removed images appear live.
    - Prefetch is limited by memory (about 800MB on 64-bit).
    - Large images are decoded at a reduced size, and a failed decode is retried once.
    - The allocation limit is 4096MB on 64-bit.
13. **Windows build fixes.**
    - `CONFIG += no_batch` in `QuickViewer.pro`. Without it NMake batch rules compiled the wrong `main.cpp`, and `QuickViewer.exe` opened the file-association dialog.
    - The `SHFileOperation` buffer fix is in `mainwindowforwindows.cpp`.

## Default settings and migration

- **Defaults** are in `QuickViewer/src/models/qvapplication.cpp`:
  - **Keys** (`addDefaultKey`):

    | Action | Default keys |
    | --- | --- |
    | NextPage | L, Right, Space, D |
    | PrevPage | H, Left, Backspace, A |
    | ZoomIn | K, Num++, Up, W |
    | ZoomOut | J, Num+-, Down, S |
    | Fitting | F, M, F12, Num+* |
    | RecyclePage | Del, Q |
    | CopyToFolder | E |
    | SetCopyFolder | Shift+E |
    | ExitApplicationOrFullscreen | Esc |

  - **Mouse** (`addDefaultKey` in `registDefaultMouseMap`): the wheel zooms, back/forward buttons turn pages, the middle button toggles fullscreen.
  - **Other defaults**: black background with no checkered pattern, no delete confirmation, theme "Flat", and no folder panel at startup (`ShowOptionViewOnStartup`).
- **Migration.** `QVApplication::migrateSettings()` upgrades existing ini files using `Others/SettingsRevision` (`QV_SETTINGS_REVISION`, currently 5).
  - When you change a default, add a new `revision < N` block and bump the revision.
  - Only change values that still equal the *old* default, so the user's own choices are kept.

## Gotchas

- **Line endings.** Many sources use CRLF. Edit them with a script that keeps `\r\n` (for example Python with `newline=''`), or the diff becomes the whole file.
- **Custom title bar (if it is ever tried again, see e459089 and b0c052e).**
  - Do not use `Qt::FramelessWindowHint`. Qt then maximizes with `MoveWindow` instead of a real maximize.
  - A caption-less maximized window that covers the whole monitor is reported by Qt as fullscreen (`isFullScreen_sys`).
- **Wheel events.** `ImageView::wheelEvent` handles the wheel over the image; `MainWindow::wheelEvent` only gets the wheel over the other widgets.
- **Event filter on Windows.** `MainWindowForWindows::eventFilter` returns early on `QEvent::Leave`, so `MainWindow`'s Leave handling does not run on Windows.
- **`ImageView::zoomingChanged`** refreshes the information text. It fires when the scene rect *or* the drawn scale changes.

## Building and testing on Linux (cloud session)

1. **Install Qt**:
   ```
   pip install aqtinstall
   python3 -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtimageformats -O /tmp/claude-0/Qt
   ```
   Also install these packages for xcb: `libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libxcb-xinerama0`.
2. **Full build** in an empty folder:
   ```
   qmake -recursive QVproject.pro CONFIG+=debug "QMAKE_CXXFLAGS+=-include cstdint -include cstring"
   ```
   unrar has Windows-only files: create stub objects `unrar/isnt.o` and `unrar/motw.o` (compile `static int dummy;`, touch them into the future), then run `make -k -j$(nproc)`.
3. **Rebuild only the app**: in `<build>/QuickViewer` run
   ```
   qmake .../QuickViewer/QuickViewer.pro CONFIG+=debug "QMAKE_CXXFLAGS+=-include cstdint -include cstring" "LIBS+=-L../lib -lQSVGNative -Wl,-rpath,<build>/lib"
   ```
   then `make`. The binary is `<build>/bin/QuickViewer`.
4. **Run headless** with `Xvfb :N -screen 0 1600x1000x24` (start it with `setsid nohup`; displays sometimes die, so use a new number).
   - Drive the app with python-xlib/XTest key presses.
   - Take screenshots with ImageMagick `import -display :N -window root`.
   - Use `QT_SCALE_FACTOR=1.5` to check the user's scaling.
   - Delete `~/.quickviewer/quickviewer.ini` for a first-start test.
   - Run test scripts from a neutral folder: a local `copy.py` next to a script shadows Python's `copy` module.
5. **Syntax-check Windows-only code** before pushing.
   - Install `g++-mingw-w64-x86-64-posix`.
   - Make a shim folder with `Windows.h` and `Shellapi.h` symlinks to the lowercase MinGW headers, plus `QtGui/qwindowdefs_win.h` downloaded from qtbase v6.8.3.
   - Run `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE` from `<build>/QuickViewer`, with the Linux Makefile's `DEFINES` and `INCPATH` plus `-I<shim>`.
6. **Check the Windows artifact** under Wine (`apt-get install wine`, with its own `WINEPREFIX`). Download the CI artifact and start `QuickViewer.exe` with an image to confirm it opens the viewer, not the file-association dialog.
   - **Virtual desktop:** for the title bar, use Wine's virtual desktop, where Wine draws window frames like Windows does: `wine explorer /desktop=qv,1600x1000 'Z:\full\path\QuickViewer.exe' image`. A relative exe path does not start.
   - **Window rects:** a small MinGW program using `EnumWindows`, `GetWindowRect`, `GetClientRect` and `IsZoomed`, started the same way, prints the real window and client rects. `WINEDEBUG=+win` traces `SetWindowPos` calls.
   - **Wine quirk:** Qt calls `SetWindowPlacement` on every maximize. Wine then moves the maximized window from (-4,-4) to (0,0), so under Wine the maximized contents are 4 px too far right and down. This is Wine behavior: Notepad, which makes no such call, maximizes to (-4,-4) in the same desktop, and every Qt 6.8 app on real Windows goes through the same call.
   - **Qt logging:** the release build prints no Qt log output under Wine.

## Ideas not done yet

- Translations (`QuickViewer/translations/*.ts`) do not have the new strings: Copy to Folder, the notifications, and the folder dialog.
- When fitting is turned off with F, the view zooms one 1.25 step from the fitted scale. A toggle to 100% may be preferred; ask the user.
