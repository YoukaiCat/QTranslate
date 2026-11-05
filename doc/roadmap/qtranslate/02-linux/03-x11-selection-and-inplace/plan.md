---
kind: task
status: todo
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

## Done when

- `src/platform/selectiongrabber_linux.cpp` and `src/platform/windowactivation_linux.cpp` are
  selected on Linux in `CMakeLists.txt`; the stubs remain for other platforms; the target links
  `X11::X11` and `X11::Xtst`.
- X11 grabber: it waits for the hotkey modifiers to be released with `XQueryKeymap`, sends Ctrl+C
  with `XTestFakeKeyEvent`, snapshots the clipboard, detects the change, reads the text, trims and
  limits it to `maxLength`, restores the snapshot, and handles the 300 ms release / 400 ms clipboard
  timeouts and `cancel()` as the Windows implementation does. `restoreClipboard=false` leaves the
  copied text in place.
- X11 window helpers: `foregroundWindow()` reads `_NET_ACTIVE_WINDOW` with an `XGetInputFocus`
  fallback; `isForegroundWindow()` compares the active window; `isFullscreenForegroundWindow()` reads
  `_NET_WM_STATE` for `_NET_WM_STATE_FULLSCREEN`; `sendPasteShortcut()` sends XTest Ctrl+V;
  `allowAnyProcessToActivate()` is a no-op on X11.
- Wayland: `foregroundWindow()` returns 0, `sendPasteShortcut()` returns false, and no X11 display or
  XTest call is made. The grabber reads the clipboard the user copied, and the controller copies the
  translation to the clipboard with a clear message instead of pasting. No synthetic input is added.
- New headless tests under `xvfb-run` in Debian 13 (trixie) cover the grabber state machine and
  clipboard
  restore (a focused `QTextEdit` is a sufficient target) and the window helpers (a dummy window with
  `_NET_ACTIVE_WINDOW`/`_NET_WM_STATE` set by the test); they `QSKIP` when no display is available.
  The 20 shared tests still pass and the build has no warnings.
- Manual X11 check on the real session: select text in an editor, Ctrl+Q shows the popup with the
  translation, Ctrl+Shift+Q replaces the selection, Ctrl+W fills the main window, and the clipboard
  is restored. Record the result, including any desktop-specific behaviour.
- The Windows implementation is untouched; any shared-header change is reviewed for Windows (not
  compiled here).

## Risks

- X11 clipboard ownership is per-process and the change is asynchronous; do not assume the clipboard
  changes immediately after XTest Ctrl+C. Use a generation counter or `QClipboard::changed` with the
  existing timeouts.
- XTest key events go to the focused window; under a bare Xvfb there is no window manager, so the
  test must set the input focus explicitly with `XSetInputFocus`.
- `_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` are EWMH properties; under a bare Xvfb no window manager
  sets them. The tests must set them, and the code must fall back to `XGetInputFocus` and return
  false rather than crash.
- The Wayland clipboard fallback can read a stale clipboard; the UI warning must make the manual
  copy step explicit. Do not try to detect the selection without a portal or synthetic input.
