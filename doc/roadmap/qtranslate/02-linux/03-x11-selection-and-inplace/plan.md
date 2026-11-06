---
kind: task
status: in-progress
detailed: true
depends: [01-linux-build-baseline]
---

# X11 selection capture and in-place insertion

## Task

Replace the Linux stubs for `SelectionGrabber` and the `Platform` window helpers with X11
implementations: XTest Ctrl+C to copy the selection, clipboard snapshot and restore, the
`_NET_ACTIVE_WINDOW`/`XGetInputFocus` target window, `_NET_WM_STATE_FULLSCREEN` for the fullscreen
hotkey pause, and XTest Ctrl+V for the in-place scenario. This is the core of Ctrl+Q, Ctrl+Shift+Q,
and Ctrl+W on X11.

On Wayland, do not synthesize input. The user copies the selection manually; the grabber reads the
current clipboard, and the in-place path puts the translation back on the clipboard and tells the
user instead of aborting or pasting. Do not add `ydotool`, `wtype`, the RemoteDesktop portal, or any
other synthetic input.

Start after `02-global-hotkeys/01-x11-xgrabkey` so the manual in-place check can be triggered with
Ctrl+Shift+Q; the X11 code itself only needs the baseline. Keep the shared header changes minimal and
do not break the Windows implementation.

The two platform files are one Linux translation unit each and dispatch on the session type, like
`hotkeymanager_linux.cpp`. The session rule (`WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, the Qt platform
name) is currently private to the hotkey backend and is needed by all three files, so it moves to a
small platform-private header instead of being copied. The Wayland in-place fallback is a small
`AppController` change and reuses existing translated messages; the one-time manual-copy warning
belongs to `05-cli-and-desktop-integration` and is not added here.

## Steps

1. Add the shared session rule in `src/platform/sessiontype.h` and use it in the hotkey backend.
   - Header-only `Platform::isWaylandSession()` with exactly the current rule from
     `hotkeymanager_linux.cpp:48-61`: non-empty `WAYLAND_DISPLAY` (use
     `qEnvironmentVariableIsEmpty`, not `isSet`, so an empty value stays on X11),
     `XDG_SESSION_TYPE` equal to `wayland` case-insensitively, or
     `qGuiApp && qGuiApp->platformName().startsWith("wayland")`. The guiless hotkey tests have no
     `QGuiApplication`, so keep the `qGuiApp` null check. Document that the rule is checked before
     any X11 call and that it is deliberately env-based (an XWayland session with
     `QT_QPA_PLATFORM=xcb` still counts as Wayland, as the stage requires).
   - In `src/platform/hotkeymanager_linux.cpp` remove the anonymous-namespace `isWaylandSession()`,
     include `platform/sessiontype.h`, and call `Platform::isWaylandSession()` in the constructor
     (`m_waylandSession`). This is behavior-preserving and is the gate for the refactor: the X11
     test must stay 16/16 and the Wayland mock test 20/20.
   - List `src/platform/sessiontype.h` in the Linux source lists in `CMakeLists.txt` and in the two
     new test targets in `tests/CMakeLists.txt` (header-only, no new `.cpp`). Do not add it to
     `qtranslate_core` and do not change the public `HotkeyManager` API.

2. Extend the shared headers only with Linux-private state and comments.
   - `src/platform/selectiongrabber.h`: update the class comment to name `selectiongrabber_win.cpp`
     and `selectiongrabber_linux.cpp` (X11 plus the Wayland manual-copy path); keep the public API,
     the callback contract and the `State` enum unchanged. Add inside `#if defined(Q_OS_LINUX)` a
     forward declaration `struct LinuxState;` and `std::unique_ptr<LinuxState> m_linux;`. No Xlib
     type may appear in the header; the destructor is already out-of-line, so the incomplete type
     is fine. The Windows class layout must stay byte-identical (verify with `git diff`).
   - `src/platform/selectiongrabber_stub.cpp`: update the stale "the application is Windows-only"
     comment to say that it serves the remaining non-Linux platforms; keep its behavior and the
     `<QMimeData>` include. It must still compile on macOS with the new `Q_OS_LINUX` member.
   - `src/platform/windowactivation.h`: update the comments to mention the X11 implementation
     (EWMH plus XTest) and the Wayland behavior (0/false/no-op); the signatures do not change.
   - Do not touch `src/platform/selectiongrabber_win.cpp` or
     `src/platform/windowactivation_win.cpp`.

3. Create `src/platform/selectiongrabber_linux.cpp` with the X11 grabber.
   - File-private logging category `qtranslate.grab` (same name as the Windows file), constants
     `kPollIntervalMs = 10`, `kMaxReleaseWaitMs = 300`, `kMaxClipboardWaitMs = 400`,
     `kDefaultMaxLength = 20000`, and a `SelectionGrabber::LinuxState` holding the cached
     `wayland` flag, the lazy `Display *`, root, modifier/keycodes, the XTest availability and the
     clipboard-changed flag. Include Xlib after the Qt headers and keep every Xlib call on the GUI
     thread.
   - Constructor: configure the 10 ms `m_timer`/`poll()` connection; cache
     `m_linux->wayland = Platform::isWaylandSession()` once (the session cannot change at runtime);
     on X11 connect `QClipboard::changed` to a lambda that only reacts to
     `QClipboard::Clipboard` while `m_state == State::WaitingForClipboard` and calls `poll()`;
     on Wayland no X11 connection is opened. Destructor: `cancel()` and close the display.
   - `grab()`: ignore an empty callback and a second call while busy; on Wayland set
     `State::WaitingForClipboard` and start the timer, so the first 10 ms tick reads the clipboard
     (keep the callback asynchronous and do not snapshot or restore anything); on X11 set
     `State::WaitingForRelease`, start `m_elapsed` and the timer, and call `poll()` like Windows.
   - `ensureDisplay()`: `XOpenDisplay(nullptr)`, the root window, `XKeysymToKeycode` for
     `XK_Control_L`, `XK_Shift_L`, `XK_Alt_L`, `XK_Super_L`/`XK_Super_R` and `XK_c` (fall back to
     `XK_C`); query the XTEST extension with `XTestQueryExtension` once. Any failure logs a warning
     and finishes with an empty text; do not crash and do not retry on every poll.
   - `poll()` in `WaitingForRelease`: `XQueryKeymap` every 10 ms and check the Control, Shift,
     Alt/Mod1 and Super/Mod4 keycodes (L and R). When all are up, `startCopy()`. At 300 ms call
     `releaseModifiers()` first: for every pressed modifier send `XTestFakeKeyEvent(..., False,
     CurrentTime)` and `XSync`, so the source application does not receive Ctrl+Alt+C; then
     `startCopy()`. This mirrors `selectiongrabber_win.cpp:167-207`.
   - `startCopy()`: snapshot all `QMimeData` formats (with the helper below) only when
     `m_restoreClipboard` is true; send Ctrl+C with `XTestFakeKeyEvent` (Ctrl down, C down, C up,
     Ctrl up, `XSync`) and fail with an empty text if the keycodes or XTest are unavailable; set
     `State::WaitingForClipboard`, clear the changed flag and restart `m_elapsed`.
   - `poll()` in `WaitingForClipboard`: if the `QClipboard::changed` flag is set, read the clipboard
     text through `QGuiApplication::clipboard()->mimeData()` (`hasText()`), restore the snapshot,
     apply `limitLength()` and finish. If 400 ms pass without a change, log and finish with an empty
     text (no selection). The initial clipboard text must never be returned just because it exists:
     a no-selection Ctrl+C does not change the ownership and must stay "no selection".
   - Helpers: `snapshotClipboard()` copies `formats()`/`data()` into a new `QMimeData` (the same
     approach as `AppController::snapshotClipboard()` and the Windows file; the Windows-only
     exclusion formats are not added on X11); `restoreClipboardData()` puts the snapshot back only
     when `m_restoreClipboard` is true and otherwise drops it, so `restoreClipboard=false` leaves
     the copied text in place; `limitLength()` trims, truncates to `m_maxLength` and does not split
     a surrogate pair, with the Windows truncation warning; `setMaxLength()` keeps the
     `length > 0 ? length : 20000` rule; `cancel()` stops the timer, restores a captured snapshot
     when in `WaitingForClipboard`, resets the state/callback and is idempotent; `finish()` stops
     the timer, clears the state and calls the callback exactly once.
   - Wayland branch: `poll()` reads `QClipboard::text()`, applies `limitLength()` and finishes;
     `cancel()` just stops and clears; no snapshot, no restore, no `XOpenDisplay`, no XTest. The
     clipboard is whatever the user copied manually; the stale-clipboard risk is documented in the
     test and in the task outcome.

4. Create `src/platform/windowactivation_linux.cpp`.
   - A lazy, process-lifetime `Display *` opened through `XOpenDisplay(nullptr)` only when
     `Platform::isWaylandSession()` is false; cache the failure and close it with a `Q_GLOBAL_STATIC`
     or a `std::unique_ptr` deleter so a Valgrind run does not report it as a definite leak. Intern
     `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE`, `_NET_WM_STATE_FULLSCREEN` and `_NET_WM_PID` once.
     Install a scoped `XSetErrorHandler` around the property reads and restore the previous handler
     immediately; never leave a handler installed.
   - `foregroundWindow()`: return 0 on Wayland or when no display is available; read
     `_NET_ACTIVE_WINDOW` from the root with `XGetWindowProperty(..., XA_WINDOW, ...)`, require one
     32-bit item, and validate it with `XGetWindowAttributes` (a stale id must not crash). Fall back
     to `XGetInputFocus` when the property is absent, zero or invalid; return 0 for `None`,
     `PointerRoot` and the root window itself, otherwise the focus window.
   - `isForegroundWindow(window)`: `window != 0 && foregroundWindow() == window`.
   - `isFullscreenForegroundWindow()`: false on Wayland, on a missing display and on a failed
     property read. Take the active window from `foregroundWindow()`; return false when
     `_NET_WM_PID` equals `getpid()` (mirror the Windows own-process exclusion; if the property is
     absent, do not exclude). Read `_NET_WM_STATE` as `XA_ATOM` and return true only when it
     contains `_NET_WM_STATE_FULLSCREEN` and `XGetWindowAttributes` reports
     `IsViewable`, as the Windows implementation ignores hidden/iconic windows. A missing property,
     a missing WM, an unmapped window and a destroyed window all mean false, never a crash.
   - `sendPasteShortcut()`: false on Wayland or when XTest or the keycodes are unavailable;
     otherwise Ctrl+V through `XTestFakeKeyEvent` (Ctrl down, V down, V up, Ctrl up, `XSync`) and
     true on success. It sends to the focused window; the caller is responsible for the
     `isForegroundWindow()` check.
   - `allowAnyProcessToActivate()`: empty no-op on X11 and Wayland, as the header promises.
   - Bare Xvfb has no window manager: every function must tolerate absent EWMH properties and
     return 0/false instead of relying on a WM or printing X protocol errors.

5. Add the Wayland in-place fallback in `src/app/appcontroller.cpp`
   (`translateSelectionInPlace`, lines 744-804).
   - Remove the early `target == 0` return and keep the captured `target` (0 on Wayland). The
     grabber now reads the manually copied clipboard, so the scenario must not abort before it.
   - After a successful translation, branch in this order:
     - `m_inPlaceTarget == 0`: copy `result.translatedText` with
       `QGuiApplication::clipboard()->setText()` and show the existing
       `tr("Could not paste automatically; the translation is on the clipboard.")`. This is the
       Wayland path: no window handle, no synthetic input, no snapshot restore, and the translation
       stays on the clipboard. On X11 this branch also protects against pasting into a window that
       cannot be identified (focus is `PointerRoot`/`None`).
     - `m_inPlaceTarget != 0 && !Platform::isForegroundWindow(m_inPlaceTarget)`: keep the existing
       "The active window changed; the translation was copied to the clipboard." branch.
     - otherwise: `pasteTranslation()` as today.
   - No new `tr()` strings: both messages already exist in the `AppController` context and in all
     14 `i18n/qtranslate_*.ts` files, so `tst_translations` and the `.ts` files are untouched. The
     one-time Wayland warning that explains the manual copy step is
     `05-cli-and-desktop-integration`, not this task.
   - Windows/X11 behavior for a real target is unchanged. The only shared change is the null-target
     corner case: a silent abort becomes a clipboard copy plus the existing notification; review it
     for Windows by diff (no MSVC here). Do not change `pasteTranslation()` or the 500 ms restore.

6. Wire the build and the tests in `CMakeLists.txt` and `tests/CMakeLists.txt`.
   - `CMakeLists.txt`, Linux `qtranslate` block: replace
     `src/platform/selectiongrabber_stub.cpp` and `src/platform/windowactivation_stub.cpp` with
     `src/platform/selectiongrabber_linux.cpp` and `src/platform/windowactivation_linux.cpp`, add
     `src/platform/sessiontype.h`, keep `autostart_stub.cpp`, `secretprotector_stub.cpp` and
     `hotkeymanager_linux.cpp`, and link `X11::X11 X11::Xtst Qt6::DBus`. Require the XTest imported
     target explicitly (`if(NOT TARGET X11::Xtst) message(FATAL_ERROR ...)`) so a missing
     `libxtst-dev` fails configure with a clear message instead of silently building a grabber that
     cannot copy. Keep the final `else()` with all five stubs for macOS and other non-Linux
     platforms; keep the Windows block, `qtranslate_core` and the public APIs unchanged.
     `libxtst-dev` is already in `packaging/linux/trixie.Dockerfile`; no Dockerfile change.
   - `tests/CMakeLists.txt`, Linux block: add `tst_selectiongrabber_linux` and
     `tst_windowactivation_linux`, compile `selectiongrabber_linux.cpp` /
     `windowactivation_linux.cpp` with their headers, link `Qt::Widgets X11::X11 X11::Xtst`, and set
     `QT_QPA_PLATFORM=xcb` through `set_tests_properties(... ENVIRONMENT ...)` so the GUI tests use
     the xcb plugin under `xvfb-run` even when the CI exports `QT_QPA_PLATFORM=offscreen`. Keep the
     two hotkey tests and the Windows block untouched.

7. Add `tests/tst_selectiongrabber_linux.cpp`.
   - `QTEST_MAIN` (a real `QApplication` is needed for `QClipboard` and `QTextEdit`);
     `initTestCase()` opens `XOpenDisplay(nullptr)` and `QSKIP`s with a clear message when it
     fails, `QSKIP`s when `QGuiApplication::platformName()` is not `xcb` (the CTest environment sets
     it; a direct run must set it too), saves/clears `WAYLAND_DISPLAY`, sets
     `XDG_SESSION_TYPE=x11` and restores both in `cleanupTestCase()`. Check
     `XTestQueryExtension` and `QSKIP` when the server has no XTEST.
   - A focus helper shows the widget, waits for exposure, calls
     `XSetInputFocus(display, WId(widget->winId()), RevertToParent, CurrentTime)`, verifies with
     `XGetInputFocus` and calls `setFocus()`. Without a WM the X focus does not follow Qt by itself,
     so this is required.
   - Test functions:
     - `copiesSelectionAndRestoresClipboard`: a `QTextEdit` with a selection, the clipboard holding
       a text plus an extra custom format; the callback is the selection, the clipboard is restored
       including the extra format, and the grabber is idle.
     - `waitsForModifierRelease`: press Ctrl with XTest, `grab()`, verify it is busy and the
       callback did not fire, release Ctrl, then get the selection. This proves the
       `XQueryKeymap` state.
     - `releasesHeldModifiersOnTimeout`: keep Ctrl down and verify the callback still arrives after
       the 300 ms path (the synthetic release plus Ctrl+C); release the physical key afterwards.
     - `cancelStopsGrab`: hold Ctrl, `grab()`, `cancel()`, verify no callback, not busy and the
       clipboard unchanged; release Ctrl.
     - `cancelDuringClipboardWait`: focus a plain `QWidget` that ignores Ctrl+C, `grab()` (the copy
       does not change the clipboard), `cancel()` and verify no callback and the original clipboard;
       this covers the restore branch of `cancel()`.
     - `noSelectionTimesOut`: focus a plain `QWidget`, set a clipboard marker, `grab()`, verify the
       callback is empty after about 400 ms and the marker is still on the clipboard.
     - `restoreDisabledLeavesCopiedText`: `setRestoreClipboard(false)`, a marker on the clipboard,
       a selection; the callback is the selection and the clipboard now holds the copied text.
     - `trimsAndLimits`: a selection with leading/trailing spaces and a long string;
       `setMaxLength()`; the callback is trimmed and truncated (and a surrogate pair is not split if
       the test includes one).
     - `grabWhileBusyIsIgnored`: hold Ctrl, call `grab()` twice, release Ctrl; only the first
       callback fires.
     - `waylandReadsClipboardWithoutXTest`: an `EnvironmentGuard` sets `WAYLAND_DISPLAY=wayland-test`
       and `XDG_SESSION_TYPE=wayland`; the clipboard holds "clipboard text" while the focused
       `QTextEdit` selects "selection text"; a grabber constructed in that environment returns
       "clipboard text" (not the selection), leaves the clipboard untouched and does not restore.
       This is the automated Wayland gate; no display is opened by the implementation.
   - Use `QTRY_COMPARE_WITH_TIMEOUT`/`QTRY_VERIFY_WITH_TIMEOUT` with generous limits; the fixed
     waits are only for XTest key/focus propagation. No assertion may depend on a window manager.

8. Add `tests/tst_windowactivation_linux.cpp`.
   - `QTEST_MAIN`; the same display/platform/XTest guards and environment handling as step 7.
     Helpers create and destroy raw `XCreateSimpleWindow` windows, set/delete
     `_NET_ACTIVE_WINDOW` on the root and set `_NET_WM_STATE`/`_NET_WM_PID` on a dummy window; intern
     the atoms with `XInternAtom`, and clean the properties between test functions.
   - Test functions:
     - `netActiveWindowWinsOverFocus`: the root property names window A while the X input focus is
       window B; `foregroundWindow()` is A.
     - `focusFallbackWithoutProperty`: delete the property, focus A; `foregroundWindow()` is A.
     - `staleActiveWindowFallsBack`: the property names a destroyed window; focus A; the fallback
       returns A and no X error escapes.
     - `noFocusReturnsZero`: no property and `XSetInputFocus(PointerRoot)`; the result is 0 and
       `isForegroundWindow(0)` is false.
     - `isForegroundWindowCompares`: true for the active window, false for another and for 0.
     - `fullscreenStateIsDetected`: `_NET_WM_STATE` with `_NET_WM_STATE_FULLSCREEN` and a
       `_NET_WM_PID` of another process; true; without the state false.
     - `ownFullscreenWindowIsIgnored`: `_NET_WM_PID` equal to `getpid()`; false.
     - `unmappedFullscreenWindowIsIgnored`: the same state on a window that is unmapped; false.
     - `missingPropertiesAreSafe`: no WM properties at all; `isFullscreenForegroundWindow()` is
       false and nothing crashes.
     - `sendPasteShortcutPastes`: a focused `QTextEdit`, the clipboard holding "pasted";
       `sendPasteShortcut()` returns true and the edit receives the text (QSKIP when XTEST or the
       keycodes are absent).
     - `waylandReturnsZeroAndFalse`: with `WAYLAND_DISPLAY=wayland-test` and
       `XDG_SESSION_TYPE=wayland`, `foregroundWindow()` is 0, `isForegroundWindow(A)` and
       `isFullscreenForegroundWindow()` are false, `sendPasteShortcut()` is false and a focused edit
       is not pasted into; `allowAnyProcessToActivate()` is a harmless no-op. The real X display is
       available in this test, so returning 0/false proves the Wayland guard did not use it.
   - Record in the test comment that it must run under `xvfb-run`; it changes the clipboard and the
     X input focus.

9. Build and verify.
   - `debian:trixie` (Qt 6.8.2): Debug and Release from the read-only source mount with
     `-Wall -Wextra`; grep the log for `warning:`; run
     `timeout 300 xvfb-run -a ctest --test-dir ... --output-on-failure` and expect 24/24 in both
     configurations; run `-R 'tst_selectiongrabber_linux|tst_windowactivation_linux'` and the two
     hotkey tests alone. The new tests must report their pass counts without skips when the display
     and XTEST are present.
   - Astra Linux 1.8.7 (Qt 6.10.2): native Debug and Release; 0 warnings; CTest 24/24. After every
     rebuild run `sudo /usr/bin/fly-set-execid.sh` (host setup, not repository content) so
     `tst_hotkeymanager_x11` keeps the `KBD_R` `security.exec_id` and stays 16/16; the new tests use
     XTest but no passive grabs. Clang 16 if time allows. If a test fails only because of the
     environment, record the exact log and do not fake a pass.
   - Manual X11 check on the real `:0` session (Astra): start the release binary with `--verbose`,
     select text in an editor, Ctrl+Q shows the popup with the translation, Ctrl+Shift+Q replaces
     the selection, Ctrl+W fills the main window, and the original clipboard is restored after each
     scenario. Repeat once with `restoreClipboard=false` if practical and record the observed
     difference. Record the editor, desktop and any Parsec/desktop-specific behavior; if the session
     or the trusted attribute is unavailable, record the check as not performed and ask the user,
     never claim it passed.
   - Wayland: the unit tests are the automated gate for `foregroundWindow() == 0`,
     `sendPasteShortcut() == false`, no X11 display and the manual clipboard read. Optionally start
     the application under Weston headless with a private `XDG_RUNTIME_DIR` and
     `strace -f -e connect` to confirm no `/tmp/.X11-unix` connection when the grabber and window
     helpers are used; the real Ctrl+Q check needs a clipboard owner, so record it as not performed
     unless a real Wayland desktop is available.
   - Windows: review the `Q_OS_LINUX` header additions, the `AppController` null-target branch, the
     `if(WIN32)` CMake/test blocks and the untouched `*_win.cpp` files by diff; do not run MSVC and
     do not claim a Windows build. The Windows 23 tests are not performed here.
   - Report the facts for the lead: files, Linux 24 / Windows 23, the X11/XTest build requirement,
     the Wayland behavior, the unchanged `tst_translations` and `.ts` files, the same-owner
     clipboard limitation, the null-target AppController behavior, the `QT_QPA_PLATFORM=xcb`
     requirement of the GUI tests, and the `fly-set-execid.sh` caveat.

## Done when

- `src/platform/selectiongrabber_linux.cpp` and `src/platform/windowactivation_linux.cpp` are
  selected on Linux in `CMakeLists.txt`; the stubs remain for other platforms; the `qtranslate`
  target links `X11::X11` and `X11::Xtst` and configure fails with a clear message when XTest is
  missing. `qtranslate_core` stays X11-free and the Windows platform files are untouched.
- X11 grabber: it waits for the hotkey modifiers to be released with `XQueryKeymap` (300 ms cap and
  a synthetic release at the cap), sends Ctrl+C with `XTestFakeKeyEvent`, snapshots all clipboard
  formats, detects the change with `QClipboard::changed` and the 400 ms timeout, reads the text,
  trims it and limits it to `maxLength` without splitting a surrogate pair, restores the snapshot,
  handles `cancel()` as the Windows implementation does, and leaves the copied text in place when
  `restoreClipboard=false`. A no-selection Ctrl+C returns an empty text and never the pre-existing
  clipboard text.
- X11 window helpers: `foregroundWindow()` reads `_NET_ACTIVE_WINDOW` and validates it, falls back
  to `XGetInputFocus` (0 for `None`, `PointerRoot` and the root), `isForegroundWindow()` compares
  the active window, `isFullscreenForegroundWindow()` reads `_NET_WM_STATE` for
  `_NET_WM_STATE_FULLSCREEN`, requires a viewable window and ignores our own process through
  `_NET_WM_PID`, `sendPasteShortcut()` sends XTest Ctrl+V, `allowAnyProcessToActivate()` is a no-op,
  and missing properties or a missing WM produce 0/false without a crash or an X protocol error.
- Wayland: `foregroundWindow()` returns 0, `sendPasteShortcut()` returns false, and no X11 display
  or XTest call is made by the grabber, the window helpers or the controller. The grabber reads the
  clipboard the user copied manually, and the controller copies the translation to the clipboard
  with the existing clear message instead of pasting or aborting. No synthetic input and no new
  `tr()` strings are added; the 14 `.ts` files and `tst_translations` are unchanged, and the
  one-time manual-copy warning stays in `05-cli-and-desktop-integration`.
- New headless tests under `xvfb-run` in Debian 13 (trixie) cover the grabber state machine,
  clipboard snapshot/restore and `cancel()` (a focused `QTextEdit` is the target), the Wayland
  clipboard read without XTest, and the window helpers (dummy windows with
  `_NET_ACTIVE_WINDOW`/`_NET_WM_STATE` set by the test) plus the Wayland 0/false guard. The tests
  are `tst_selectiongrabber_linux` and `tst_windowactivation_linux`; they use
  `QT_QPA_PLATFORM=xcb` under `xvfb-run` and `QSKIP` with a clear message when `XOpenDisplay`
  fails, the platform is not `xcb`, or the server has no XTEST. Linux has 24 tests (the 22 existing
  plus the two new ones); the 22 existing tests still pass, including `tst_hotkeymanager_x11`
  (16/16) and `tst_hotkeymanager_wayland` (20/20), and Windows keeps 23.
- Debug and Release build in `debian:trixie` (Qt 6.8.2) and on Astra Linux 1.8.7 (Qt 6.10.2) with
  no `-Wall -Wextra` warnings; `xvfb-run -a ctest` is 24/24 in both configurations; on Astra the
  rebuild is followed by `sudo /usr/bin/fly-set-execid.sh` before the X11 hotkey test.
- Manual X11 check on the real session: select text in an editor, Ctrl+Q shows the popup with the
  translation, Ctrl+Shift+Q replaces the selection, Ctrl+W fills the main window, and the clipboard
  is restored. The result, the editor/desktop and any desktop-specific behavior are recorded; if
  the check cannot be performed, that is recorded as not performed with the reason.
- The Windows implementation is untouched; the `Q_OS_LINUX` header additions and the
  `AppController` null-target branch are reviewed for Windows by diff (not compiled here), and the
  outcome states that the Windows build and the 23 Windows tests were not performed.

## Risks

- X11 clipboard ownership is per-process and the change is asynchronous; do not assume the clipboard
  changes immediately after XTest Ctrl+C. Use `QClipboard::changed` with the existing timeouts and
  never fall back to returning the snapshot text at the 400 ms timeout: that would translate the
  stale clipboard when the user has no selection. A same-owner re-copy of the same text may not
  emit `changed`; that rare false negative is accepted and documented (the ICCCM `TIMESTAMP`
  target would be the only reliable fix and is out of scope).
- XTest key events go to the focused window; under a bare Xvfb there is no window manager, so the
  tests must `XSetInputFocus` the target explicitly and wait for exposure. A focus failure is a test
  bug, not a reason to skip.
- `_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` are EWMH properties; under a bare Xvfb no window manager
  sets them. The tests set them, and the code must validate the property, fall back to
  `XGetInputFocus` and return 0/false rather than crash. `_NET_WM_PID` may be absent (some WMs or
  raw test windows); in that case do not try to exclude our own process.
- The Wayland clipboard fallback can read a stale clipboard and the same-owner change is not
  observable; the UI warning must make the manual copy step explicit. That warning is
  `05-cli-and-desktop-integration`, so this task must not expand the UI or the `.ts` files. Do not
  try to detect the selection without a portal or synthetic input.
- `XKeysymToKeycode(display, XK_c)` can return 0 on a non-Latin layout, so Ctrl+C capture would fail
  where Windows uses a virtual key. Log the reason, return an empty selection and document the
  limitation; do not guess a Cyrillic or second-level keysym.
- XTest is now a hard build dependency of the Linux application. A missing `libxtst-dev` must fail
  configure with a clear CMake error, and a server without the XTEST extension must degrade to an
  empty selection/failed paste with a warning, not a crash. The `trixie` image and Astra already
  have `libxtst-dev`; `X11::Xtst` is linked into `qtranslate` and the two new tests only.
- The `sessiontype.h` refactor touches the accepted hotkey backend. Keep the rule byte-equivalent
  (including the `qEnvironmentVariableIsEmpty` empty-value behavior) and use the 16/16 X11 and
  20/20 Wayland hotkey tests as the regression gate. Do not change the public `HotkeyManager` API.
- The new GUI tests change the clipboard and the X input focus, so they must run under `xvfb-run`
  with `QT_QPA_PLATFORM=xcb`; do not run them against the user's real session. The xcb platform
  plugin is provided by `libqt6gui6` in trixie.
- The `AppController` null-target branch changes a silent abort into a clipboard copy plus the
  existing notification on Windows in the rare case `GetForegroundWindow()` returns NULL; that is
  the same safe fallback as the "active window changed" path and must be reviewed by diff. Do not
  add `tr()` strings for it.
- The window helpers keep a process-lifetime X display; close it through a deleter/global static and
  keep Valgrind free of definite leaks. The Windows build cannot be compiled here; keep the
  `Q_OS_WIN` header layout and the `if(WIN32)` CMake/test blocks byte-identical and review only.
- A rebuild on Astra loses `security.exec_id`; re-run `sudo /usr/bin/fly-set-execid.sh` before the
  X11 hotkey test, otherwise it `QSKIP`s. This is host setup, not repository content.
