# QuickViewer fork: notes for the next session

This is a personal fork of QuickViewer (Qt 6, C++, qmake) maintained for one Windows user. The original project is discontinued. Read this file before changing anything; it records what was changed, why, and how to build and test.

## The user

- Windows on a 4K ultrawide monitor (3440 logical pixels wide at about 150% scaling).
- **Priority is minimalism.** The look and feel follows nomacs: a compact dark Flat theme, small 16px monochrome icons, and only two bars at the top (the menu bar and the toolbar), with no bar at the bottom.
- Every round ends with a Windows build from GitHub Actions. Give the user the artifact download link (`QuickViewer-windows-x64`) after checking it starts.
- The user installs into `Program Files`, so settings live in `%APPDATA%\QuickViewer\quickviewer.ini` there. A portable unzip keeps the ini next to the exe. User preferences are code defaults, not a bundled ini (a bundled ini did not work for them).
- Never commit the user's own ini files or history.

## Branches

- The default branch is `master` (there is no `main`). Work happens on a `claude/...` branch with a PR into `master`.
- CI: `.github/workflows/windows-build.yml` builds with MSVC 2022, Qt 6.8.3 and jom, packages with windeployqt and uploads `QuickViewer-windows-x64`. It runs on every push and PR.

## Changes in this fork (newest first)

1. **Top bars only.**
   - The image information (file name, page, resolution, zoom, file size) is right-aligned in the menu bar corner, next to the fullscreen button. It is shortened in the middle when there is no room, and menus keep priority over it.
   - The page slider is the last item of the toolbar (`m_pageBarAction`). The status bar and the bottom page bar are hidden.
   - Messages go through `MainWindow::showStatusText()`. "Bookmark Saved." is a toast notification.
2. **Zoom.**
   - Zoom in and zoom out multiply the displayed scale by 1.25, starting from the fitted scale. A step that passes 100%, or ends within 10% of it, stops at 100%. Limits are 2% to 1600%.
   - `ImageView::m_zoomScale` is relative to the *original* image, so the percentage stays the same when turning pages. Large images may be loaded shrunk; `PageContent::sourceRatio()` is loaded size ÷ original size.
3. **Fitting.**
   - Fitting is turned on when a file is opened (`MainWindow::fitOnOpen()` in `loadVolume`) and when the app starts. It is not reset when turning pages.
   - F, M, F12 and Num+* toggle fitting. Fit also enlarges small images (`DontEnlargeSmallImagesOnFitting=false`).
4. **Window.** Size, position and maximized state are restored like a web browser, but never fullscreen. A first start opens at 3/4 × 4/5 of the screen, centered.
5. **Keys.** Esc closes the window (or leaves fullscreen).
6. **Application icon.** A lighter greyscale version of the original icon (`QuickViewer/icons/appicon.ico`).
7. **Delete.**
   - Del and Q delete without asking.
   - A toast notification shows the deleted path (`widgets/toastnotification.*`).
8. **Copy to Folder.**
   - E copies the image to a folder and Shift+E chooses that folder. With no folder set, the dialog asks first.
   - The dialog (`widgets/copyfolderdialog.*`) is an editable dropdown: paste a path, pick a recent copy folder or a folder from the viewing history, or use Browse.
   - The folder is saved immediately, and the toast has an "Open folder" button.
9. **Flat theme.**
   - `themes/Flat.qss` is the default theme. Any theme other than "Default" forces the Fusion style; native Windows styles ignore the compact padding.
   - The icons are monochrome SVGs in `icons/flat/` and `themes/flat/`, colored at runtime by `QVApplication::flatIcon()` from the `icon-color:` comment in the qss.
10. **Startup.** The folder panel is off at startup.
11. **Stability and performance.**
    - Deleting the last image no longer crashes (`PageManager::reloadVolume`, null guards).
    - The folder is watched with `QFileSystemWatcher`, so added and removed images appear live.
    - Prefetch is limited by memory (about 800MB on 64-bit).
    - Large images are decoded at a reduced size, and a failed decode is retried once.
    - The allocation limit is 4096MB on 64-bit.
12. **Windows build fixes.**
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

  - **Other defaults**: black background with no checkered pattern, no delete confirmation, theme "Flat", and no folder panel at startup (`ShowOptionViewOnStartup`).
- **Migration.** `QVApplication::migrateSettings()` upgrades existing ini files using `Others/SettingsRevision` (`QV_SETTINGS_REVISION`, currently 3).
  - When you change a default, add a new `revision < N` block and bump the revision.
  - Only change values that still equal the *old* default, so the user's own choices are kept.

## Gotchas

- **Line endings.** Many sources use CRLF. Edit them with a script that keeps `\r\n` (for example Python with `newline=''`), or the diff becomes the whole file.
- **Menu bar corner widget.**
  - `setCornerWidget` reparents the widget and hides it, so call `show()` afterwards.
  - QMenuBar lays the corner out again only on show/hide, which is why `resetStatusLabel()` hides and re-shows it after changing the text.
- **`ImageView::zoomingChanged`** refreshes the information text. It fires when the scene rect *or* the drawn scale changes.
- **Hidden bars.** `onGraphicsView_anchorHovered` no longer has a bottom page bar in fullscreen; the slider comes with the toolbar at the top.

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
5. **Check the Windows artifact** under Wine (`apt-get install wine`, with its own `WINEPREFIX`). Download the CI artifact and start `QuickViewer.exe` with an image to confirm it opens the viewer, not the file-association dialog.

## Ideas not done yet

- Translations (`QuickViewer/translations/*.ts`) do not have the new strings: Copy to Folder, the notifications, and the folder dialog.
- When fitting is turned off with F, the view zooms one 1.25 step from the fitted scale. A toggle to 100% may be preferred; ask the user.
