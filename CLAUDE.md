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
- CI: `.github/workflows/windows-build.yml` builds with MSVC 2022, Qt 6.8.3 and jom, packages with windeployqt (only what the app uses, see "Smaller Windows package") and uploads `QuickViewer-windows-x64`. It runs on every push and PR.

## Changes in this fork (newest first)

1. **Fast page turns while images arrive** (a folder of thousands of images that a generator or a copy keeps filling).
   - Before, every change in the folder reloaded the whole volume on the GUI thread: list and sort the folder, build a new `VolumeManager`, decode the current image, and throw away the images read ahead, which were then decoded again. With 4000 images on Linux one reload blocked the GUI for 40–80 ms. Each notification also restarted the 500 ms timer, so during a fast copy no new image appeared until the copy stopped.
   - Now `PageManager::onWatchTimer_timeout` lists the folder on its own thread (`m_scanPool`, one thread, `scanFolder`), with the size and time of each image, and compares the list there with the volume's list: added, removed and changed files (`FolderScan`). The GUI thread only applies the difference (`applyFolderChanges` → `VolumeManager::updateFileList`). New names are inserted into the sorted list by binary search, removed names are taken out, and the image cache, the current page and the images on the screen stay. The screen is reloaded only when its image was removed or changed, or in dual view when its partner changed. Otherwise only the page number changes and the new neighbors are read ahead.
   - The image cache (`VolumeManager::m_imageCache`) is keyed by file name instead of page index, so an inserted file doesn't invalidate it. Page rotations (`ImageView::m_pageRotations`) are keyed by file name too.
   - New notifications don't restart the timer, so new images appear about 0.5 s after they arrive, also during a long copy. Only one listing runs at a time; a change during a listing starts one more after it.
   - **Files still being written.** Empty files are not shown yet. A file whose size or time changes from one listing to the next has its cached image dropped and is read again (the screen too if it shows the file). Writing into a file doesn't notify the folder, so every listing that found a change is followed by one more. A Windows copy locks the file, so a new image that could not be decoded is dropped from the cache and read again, during the next 10 listings (`FRESH_IMAGE_RETRIES`).
   - A listing is applied only if the loader still has the list it was compared with (same `constData()`); otherwise the folder is listed again. A folder that becomes empty, is removed, or gets images again after being empty still goes through `reloadVolume` and `loadVolume`.
   - `TimeOrderdCacheFuture::trash` no longer waits for a decode that is still running (that blocked the GUI thread when a read-ahead image was evicted or removed). `~VolumeManager` waits for them (`waitForDiscarded`), because they use its loader.
   - **Measured** on Linux (debug build, 4000 PNGs of 1024×1024 and 2 MB, the next or previous image every 300 ms, 60 page turns; "fast" is every 80 ms, 200 turns). The page turn time is from the XTest key press to the paint of the new image:

     | While copying | Page turn, before (median / p90 / max) | After | GUI time per folder change | New image shown after |
     | --- | --- | --- | --- | --- |
     | nothing | 3.7 / 4.6 / 8.3 ms | 3.8 / 4.7 / 7.1 ms | – | – |
     | a file every 0.7 s, at the end | 3.8 / 18.5 / 59 ms | 4.4 / 6.3 / 10 ms | 40–72 ms → 0.5 ms | 520 → 250 ms |
     | a file every 0.7 s, among the viewed pages | 4.1 / 16.7 / 62 ms | 4.4 / 5.9 / 18 ms | 40–79 ms → 0.6 ms (max 1.6) | 513 → 274 ms |
     | a file every 0.15 s | 3.7 / 4.7 / 8.2 ms | 4.3 / 5.7 / 9.0 ms | (no reload until the copy stopped) → 0.5 ms | 10–20 s → 267 ms (max 520) |
     | fast turns, a file every 0.7 s | 3.5 / 15.5 / 64 ms | 4.1 / 5.1 / 11 ms | 40–81 ms → 0.5 ms (max 2) | 516 → 260 ms |

     The listing itself takes about 25 ms on its thread. Images decoded during the fast run: 573 before, 243 after.
2. **Free panning** (drag the image with the left button, like PureRef).
   - A left drag (past `QApplication::startDragDistance()`) moves the images anywhere, also beyond the edges of the view, fitted or zoomed. 48 px (`FREE_PAN_MARGIN`) stay in the view so they cannot be lost. The cursor is a closed hand while dragging.
   - While the images are free, the mouse no longer pans them (`scrollOnZoomMode` is skipped) and zooming keeps them where they are: the wheel keeps the point under the cursor, the keys keep the point at the center of the view.
   - Double-clicking (the zoom is kept), another image (or the other half of a wide image), F, M and Fit to width put them back (`leaveFreePan`): the next layout places them like before the drag. The left and right edges (`HOVER_BORDER`) still turn pages on a click and never start a drag. A press with the loupe tool also leaves free panning.
   - State: `m_freeOffset` is the center of the images minus the center of the viewport, in viewport pixels. `applyFreePan()` turns the scroll bars off, makes the scene rect the images plus one view on each side, and sets the scroll values, which are absolute view coordinates (so they survive scene rect changes). It runs on every drag move and from `setSceneRectMode` on every layout.
   - The press is not accepted, so `MainWindow::eventFilter` still gets it for the mouse actions. QGraphicsView's `ScrollHandDrag` is no longer used: with "Scroll with cursor when zooming" off, the left drag is this free drag too, and the arrow keys that scroll in that mode go through `ImageView::scrollBy`.
3. **Fullscreen text** (View › Show texts on Fullscreen, on by default): in F11 fullscreen the path and page number, `C:\folder\image.png (64/89)` (`PageManager::pageSignage`), are shown at the top right; in dual view each page has its own at its outer top corner. Light grey `#dddddd` on half-transparent black, chosen by the user for the black bars beside vertical images (the original was green).
   - The original feature was intact, but 2fb25c1 had put its default back to off. Settings revision 7 turns it on again.
   - The original drew it as scene items (`PageContent::GText`). In this fork that moved with zoom and panning, and at 150% scaling the view's `1/devicePixelRatio` transform put it far from the corner and the image covered its background. It is now two `QLabel`s over the viewport (`ImageView::updateSignages`), children of the view, not of the viewport, because `QGraphicsView` scrolls the viewport's children with it. They are created right after the viewport, so the toast and the overlays, made later by `MainWindow`, are above them.
4. **Smaller Windows package** (the user installs it into Program Files). The original portable zip was about 40 MB; the package had grown to 101 MB unpacked (53 MB zip). windeployqt now skips what the app never loads: the VC++ runtime installer `vc_redist.x64.exe` (the runtime DLLs are copied next to the exe instead, as the original installer did), `opengl32sw.dll`, `d3dcompiler_47.dll`, `dxcompiler.dll` and `dxil.dll` (the build defines `QV_WITHOUT_OPENGL`, and Windows 10/11 have the D3D compiler anyway), the `generic`, `networkinformation` and `tls` plugins with `Qt6Network.dll` (`QuickViewer.exe` does not import it), the SQL drivers other than SQLite, and Qt's own `qt_*.qm` translations (a missing one is skipped; the app's own translations are kept).
   - The Package step fails if any DLL that a packaged binary imports is neither in the package nor in `System32` (`dumpbin /dependents`), and prints the file sizes.
5. **Caption editor** (O, View › Show Caption Editor) for training captions: the `.txt` file with the same name as the image, in the same folder (`image.png` → `image.txt`, as sd-scripts reads it). It has the look and the move/resize/remember behavior of the prompt overlay, at the bottom left by default (the prompt overlay is at the bottom right).
   - `models/captionfile.*` reads and writes the file. Lines starting with `---===---` (`multiline_caption_separator` of sd-scripts' `enable_multiline_caption`) split it into caption candidates, and the text after `#` on a separator line is the candidate's name. A file starting with a separator names the first candidate (sd-scripts drops the empty one before it). Blank lines around candidates are trimmed like sd-scripts does; CRLF, a final newline and a UTF-8 BOM are kept.
   - `widgets/captionoverlay.*`: a box per candidate. The title rows (an editable name, and move up, move down, copy and remove under the mouse) appear only with several candidates or a named first one. The header has the file name (• when modified), add candidate, save and close.
   - Like the prompt overlay, it is shown only for images that have a caption file (the user asked for this; a new caption file can't be created in the app).
   - Its height always fits the whole text, and it has no scroll bars at all (the user asked to remove them; their captions are mostly short and the bar hid text). Only text taller than the view is cut, and scrolls with the wheel and the cursor. Only the left and right edges resize it; just the width is saved.
   - Keys: the edits accept every ShortcutOverride except F1–F35, so typed keys never turn pages. Esc gives the focus back to the image, Ctrl+S saves, Tab/Shift+Tab go through the boxes, Enter in a name goes to its text.
   - Saving: Ctrl+S or the save button, and automatically (with a toast) when the image changes, the editor is hidden or the window closes. A deleted image's caption is not written again. Only images in folders, not archives; in dual view the first image.
   - It is off until O is pressed. Position is an alignment (left/center/right, top/bottom) and an offset (`[CaptionOverlay]` in the ini); double-clicking the header puts it back at the bottom left, and dropping it within 24 px of the center snaps it to the center.
6. **Prompt overlay** (P, View › Show Prompt), like [SD Prompt Reader](https://github.com/receyuki/stable-diffusion-prompt-reader). The user chose style A of the mockups (a panel with everything visible).
   - `models/promptreader.*` reads A1111/Forge/Forge Neo, ComfyUI, NovelAI (text chunks and the gzip data in the alpha channel), InvokeAI, Easy Diffusion, Fooocus, StableSwarmUI and Draw Things from PNG (tEXt/zTXt/iTXt/eXIf), JPEG and WebP (EXIF UserComment). It has its own small inflate because `qUncompress()` can't read gzip.
   - It runs in `futureLoadImageFromFileVolumeImpl` (worker threads) on the bytes already loaded, and fills `ImageContent::Prompt`. Its regular expressions are `thread_local`.
   - ComfyUI is read by following the graph back from the sampler nearest to SaveImage. It handles KSampler and SamplerCustomAdvanced (noise, guider, sampler, sigmas), SDXL Clip G/L, Flux guidance, ConditioningZeroOut, and the shift of ModelSampling* nodes.
   - `widgets/promptoverlay.*`: header (tool, close; the user asked to remove its copy-all button, the sections keep theirs), Prompt, Negative prompt, a grid (Model, Sampler, Schedule type, Steps, CFG, Shift, Seed, Size; a click copies a value) and "All settings". Weights and LoRA tags are tinted blue (the user asked to keep that one color), brackets are dimmed, and escaped `\(` `\)` are plain text.
   - It is shown only for images with readable data. Dragging the header moves it, the edges resize it, and double-clicking the header resets it to the bottom right in the size of the text. Size, nearest corner and offset are saved immediately (`[PromptOverlay]` in the ini).
   - It accepts all mouse and wheel events so that nothing reaches the image view under it (the wheel zooms there), and its widgets never take the keyboard focus.
7. **Layout (as of a7610f0, which the user chose to keep).**
   - The standard Windows title bar, the menu bar and the toolbar are at the top. The bottom has two lines: the page frame (`(64/89)` and the slider) and the status bar with the image information.
   - Two layouts were tried and reverted at the user's request: the information and slider in the top bars (73a734f), and the menu bar merged with the title bar through `WM_NCCALCSIZE`/`WM_NCHITTEST` (e459089, b0c052e). Their notes are in the git history if that idea comes back.
8. **Colors.** The Flat theme uses neutral greys: the slider fill, selections, tabs and tags (no blue accent).
9. **Zoom.**
   - **Keys** (`ImageView::zoomBy`): zoom in and zoom out multiply the displayed scale by 1.25, starting from the fitted scale. A step that passes 100%, or ends within 10% of it, stops at 100%. Limits are 2% to 1600%.
   - **Mouse wheel** (`ImageView::zoomByWheel`): the plain wheel, Ctrl+wheel and right button+wheel zoom finely, 5% per notch (`WHEEL_ZOOM_STEP`; the user's wheel is very sensitive). Smooth wheels and touchpads zoom by their fraction of a notch, and only passing 100% stops at 100%.
     - Pages are turned with the keys and the mouse back/forward buttons. Settings revision 5 migrates the former default wheel bindings.
     - **"Scroll with cursor when zooming"** (on by default, and on for the user): the mouse position pans the zoomed image (`scrollOnZoomMode`).
       - A zoom (wheel or keys) lands where that pan puts it, so it goes where the cursor points and does not jump at the next mouse move.
       - `setSceneRectMode` skips its keep-the-center correction while the cursor is over the view. Without the cursor over the view, the zoom keeps the center.
     - **With that option off,** the point under the cursor stays in place (`zoomBy` with an anchor).
   - **Status text:** the zoom in the status text updates on every zoom step (`zoomingChanged` also fires when the drawn scale changes).
   - **Remembered:** the manual zoom is `QVApplication::ZoomScale()` (`View/ZoomScale`), relative to the *original* image. It is kept when turning pages, opening files and restarting. Large images may be loaded shrunk; `PageContent::sourceRatio()` is loaded size ÷ original size.
10. **Fitting.**
   - Fitting is remembered (`View/Fitting`): opening a file or starting the app keeps the last fitting and zoom. It is not forced on.
   - **F zooms to fit** (`actionFitToWindow`), every time. Fit to window and Fit to width are an exclusive `QActionGroup`, so triggering the checked one fits again instead of unchecking it (before, every second press did nothing).
   - **M, F12 and Num+* toggle fitting** (`actionFitting`). Settings revision 6 moved F from the toggle back to Fit to window.
   - Fit also enlarges small images (`DontEnlargeSmallImagesOnFitting=false`).
11. **Window.** Size, position and maximized state are restored like a web browser, but never fullscreen. A first start opens at 3/4 × 4/5 of the screen, centered.
12. **Keys.** Esc closes the window (or leaves fullscreen).
13. **Application icon.** A lighter greyscale version of the original icon (`QuickViewer/icons/appicon.ico`).
14. **Delete.**
   - Del and Q delete without asking.
   - A toast notification shows the deleted path (`widgets/toastnotification.*`).
15. **Copy to Folder.**
   - E copies the image to a folder and Shift+E chooses that folder. With no folder set, the dialog asks first.
   - The dialog (`widgets/copyfolderdialog.*`) is an editable dropdown: paste a path, pick a recent copy folder or a folder from the viewing history, or use Browse.
   - The folder is saved immediately, and the toast has an "Open folder" button.
16. **Flat theme.**
   - `themes/Flat.qss` is the default theme. Any theme other than "Default" forces the Fusion style; native Windows styles ignore the compact padding.
   - The icons are monochrome SVGs in `icons/flat/` and `themes/flat/`, colored at runtime by `QVApplication::flatIcon()` from the `icon-color:` comment in the qss.
17. **Startup.** The folder panel is off at startup. A saved "Stay on top" is applied at startup.
18. **Stability and performance.**
    - Deleting the last image no longer crashes (`PageManager::reloadVolume`, null guards).
    - The folder is watched with `QFileSystemWatcher`, so added and removed images appear live (see "Fast page turns while images arrive").
    - Prefetch is limited by memory (about 800MB on 64-bit).
    - Large images are decoded at a reduced size, and a failed decode is retried once.
    - The allocation limit is 4096MB on 64-bit.
19. **Windows build fixes.**
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
    | Fitting (toggle) | M, F12, Num+* |
    | FitToWindow (zoom to fit) | F |
    | RecyclePage | Del, Q |
    | CopyToFolder | E |
    | SetCopyFolder | Shift+E |
    | ExitApplicationOrFullscreen | Esc |
    | ShowPrompt | P |
    | ShowCaption | O |

  - **Mouse** (`addDefaultKey` in `registDefaultMouseMap`): the wheel zooms, back/forward buttons turn pages, the middle button toggles fullscreen.
  - **Other defaults**: black background with no checkered pattern, no delete confirmation, theme "Flat", no folder panel at startup (`ShowOptionViewOnStartup`), the prompt overlay on (`PromptOverlay/Visible`), the caption editor off (`CaptionOverlay/Visible`), and the fullscreen text on (`View/ShowFullscreenSignage`).
  - New actions with a default key need no migration: keys missing from `[KeyConfig]` use the default.
- **Migration.** `QVApplication::migrateSettings()` upgrades existing ini files using `Others/SettingsRevision` (`QV_SETTINGS_REVISION`, currently 7).
  - When you change a default, add a new `revision < N` block and bump the revision.
  - Only change values that still equal the *old* default, so the user's own choices are kept.

## Gotchas

- **Line endings.** Many sources use CRLF. Edit them with a script that keeps `\r\n` (for example Python with `newline=''`), or the diff becomes the whole file.
- **Custom title bar (if it is ever tried again, see e459089 and b0c052e).**
  - Do not use `Qt::FramelessWindowHint`. Qt then maximizes with `MoveWindow` instead of a real maximize.
  - A caption-less maximized window that covers the whole monitor is reported by Qt as fullscreen (`isFullScreen_sys`).
- **Wheel events.** `ImageView::wheelEvent` handles the wheel over the image; `MainWindow::wheelEvent` only gets the wheel over the other widgets.
- **Event filter on Windows.** `MainWindowForWindows::eventFilter` returns early on `QEvent::Leave`, so `MainWindow`'s Leave handling does not run on Windows.
- **Overlay layouts.**
  - Widgets added to a *visible* widget stay hidden until Qt shows them later, so call `show()` before measuring (see `rebuildParameters`).
  - Nested layouts update their size hints with a posted LayoutRequest. `PromptOverlay::relayout()` invalidates them before reading `sizeHint()`.
  - The Flat theme's `QTextEdit { padding: 2px }` also applies inside the overlay unless its own stylesheet sets the padding.
  - Text heights come from the document layout (`PromptTextEdit::contentHeights`), not `QFontMetrics::lineSpacing()`: fallback fonts (Japanese, Wine's fonts) make lines taller and the last line was cut. One layout of a copy of the text gives the first line, the first N lines and the whole height.
  - An invalidated layout does not lay out on its widget's resize; it waits for the posted LayoutRequest. When images are turned fast, the caption editor was painted before that, with its new size and the old text geometry (one squeezed line, overlapping boxes). Both overlays' `relayout()` therefore call `activate()` on their layouts (parents first) after `setGeometry()`, and measure the texts at their current fixed heights instead of shrinking them to 0 (`setFixedHeight` resizes and lays out the text at once, which was also slow).
  - **Speed when turning pages fast** (the user flips as fast as possible with both overlays on). Measured in a debug build, per image: the prompt overlay went from 10 ms (max 24) to 2 ms (max 6), the caption editor from 6 ms (max 17) to 2 ms (max 6). What was slow: creating widgets under the overlay style sheet, drawing the SVG icons of new buttons, laying out a copy of each text three times, and shrinking the texts to 0. So the settings grid reuses its labels, the caption editor keeps unused candidate boxes hidden for reuse and draws each icon once, and it asks `QCursor::pos()` only when the mouse is over it. To measure again, put `QElapsedTimer` + `fprintf(stderr, ...)` around `setPrompt`/`setImage` and turn pages with XTest key presses 15 ms apart.
- **`Windows.h` in every source.** `src/stdafx.h` is the precompiled header and includes `Windows.h` without `NOMINMAX`, so write `(std::numeric_limits<T>::min)()`.
- **Scroll bar policy and `resizeEvent`.** A policy change lays out the scroll area at once: scroll bars that appear resize the viewport, `ImageView::resizeEvent` then sets the scene rect to the viewport size (clamping the scroll values), and range changes show or hide the scroll bars later through a queued connection. `leaveFreePan` therefore restores `ScrollBarAsNeeded` while the large free-pan scene rect is still set; otherwise the image was stuck at the top left without scroll bars.
- **`ImageView::zoomingChanged`** refreshes the information text. It fires when the scene rect *or* the drawn scale changes.
- **Volume lists.** In `VolumeManager`, `m_filelist` is sorted by name only for the name sorts; for size and time sorts the order is `m_imageMetadataList`, and `getIndexedFileName`/`indexOfFileName` pick the right one (or the random slide show list). Change the lists only through `updateFileList`, which keeps all of them, the loader's `contents()` and the counter `m_cnt` together.

## Building and testing on Linux (cloud session)

1. **Install Qt**:
   ```
   pip install aqtinstall
   python3 -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtimageformats -O /tmp/claude-0/Qt
   ```
   Also install these packages for xcb: `libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libxcb-xinerama0`.
2. **Full build** in an empty folder. A fresh clone has no submodules: run `git submodule update --init --recursive` first, and run qmake again if it ran without them (its Makefiles then miss files).
   ```
   qmake -recursive QVproject.pro CONFIG+=debug "QMAKE_CXXFLAGS+=-include cstdint -include cstring"
   ```
   unrar has Windows-only files: create stub objects `unrar/isnt.o` and `unrar/motw.o` (compile `static int dummy;`, touch them into the future), then run `make -k -j$(nproc)`.
   - Without the submodules qmake keeps the paths of the missing files relative, and the app then fails with "No rule to make target". `--depth 50` makes the checkout quicker.
   - `fileloader` includes `../Qt7z/Qt7z/lib7zip/src` relative to the build folder: `ln -s <repo>/Qt7z/Qt7z/lib7zip <build>/Qt7z/Qt7z/lib7zip`.
   - `qsvgrenderer` lists headers as sources: stub `Constants.o` and `XMLParser.o` like the unrar ones, and remove the duplicate objects from `OBJECTS` in `<build>/qsvgrenderer/Makefile` (every qmake run regenerates it). The app then links only after step 3.
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
   - A fresh window has no keyboard focus under Xvfb: click the image before sending keys.
   - Without a window manager, F11 does not resize the window, so fullscreen stays at the window's size. Start `openbox` on the display (`apt-get install openbox`) to test real fullscreen. Under X11 the window is resized only after `readyForPaint()`, and `MainWindow` skips the view's resize events while fullscreen, so the image is not fitted to the fullscreen size there; on Windows the resize happens first.
   - Stop the app with `pkill -x QuickViewer`. `pkill -f build/bin/QuickViewer` also matches the shell running it.
   - Don't edit headers while a build runs: objects compiled during the edit look up to date but use the old header (it crashed in `VolumeManager`). Run `make clean` in `<build>/QuickViewer` if in doubt.
   - The OpenGL headers are needed for the full build: `apt-get install libgl-dev libegl-dev libxkbcommon-dev`.
   - **Measuring positions:** use test images in saturated colors (the UI is grey, the view black) with a small green marker. The bounding box of the saturated pixels (numpy) is the visible image, and the marker's centroid checks that a zoom keeps the point under the cursor.
   - **Page turns while files arrive:** the timing code is not committed. Print `QDateTime::currentMSecsSinceEpoch()` with `fprintf(stderr, ...)` from an `ImageView::viewportEvent` override when the shown path changes, and time `onFolderScanned`/`applyFolderChanges` with `QElapsedTimer`. A Python script sends XTest keys and copies files into a folder of 4000 hard links (`os.link`) to a few dozen generated PNGs, and matches each key press with the next paint. Keep the binary named `QuickViewer`, or `pkill -x QuickViewer` misses the old instances, which then keep reloading the same folder and spoil the next measurement.
   - **Archives** don't open in this Linux build (no 7-Zip library at run time, the base build neither); check them under Wine.
   - **Prompt reader without the GUI:** compile a small program with `src/models/promptreader.cpp` and QtGui, and make sample images with Pillow and piexif (A1111 `parameters`, ComfyUI `prompt` JSON, EXIF UserComment, NovelAI alpha bits). The original Python reader can be imported for comparison by stubbing its GUI modules (`customtkinter`, `tkinterdnd2`, `pyperclip`, `CTkToolTip`).
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

- Translations (`QuickViewer/translations/*.ts`) do not have the new strings: Copy to Folder, the notifications, the folder dialog, the prompt overlay and the caption editor.
- Free panning keeps 48 px of the image in the view; PureRef has no such limit. Ask the user if they want it smaller or gone.
- When fitting is turned off with M, the view zooms one 1.25 step from the fitted scale. A toggle to 100% may be preferred; ask the user.
- Images arriving by a Windows copy were tested only on Linux. The retry of new images that can't be decoded follows how Windows copies (the file is locked during the copy and gets its time at the end). If a half image stays on screen after a copy on Windows, look at `FRESH_IMAGE_RETRIES` and the stamps in `scanFolder`.
