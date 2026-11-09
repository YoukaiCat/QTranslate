---
kind: task
status: todo
depends: [02-selection-check-and-replace]
---

# X11 replacement integration

## Task

Prove the external replacement on X11 with the real platform code: a focused editor with selected
text, the real `SelectionGrabber` (XTest Ctrl+C), the real `ProofreadingPopupWindow` in external mode
and the real `Platform::sendPasteShortcut()`, under `xvfb-run` with `QT_QPA_PLATFORM=xcb`. This is the
automated gate for the stage's "replaces a focused editor's selection" criterion. The window-change
and Wayland copy fallbacks live in `AppController`, which this test cannot link without pulling in the
whole application; record them as a manual check in the outcome instead of reimplementing the
decision in the test.

## Steps

1. Add `tests/tst_proofreading_selection_x11.cpp` (Linux only, `QT_QPA_PLATFORM=xcb`, links
   `Qt::Widgets`, `X11::X11` and `X11::Xtst`, and lists
   `src/ui/proofreadingpopupwindow.cpp`, `src/ui/proofreadingpanel.cpp`,
   `src/ui/proofreadingerrortext.cpp`, `src/platform/selectiongrabber_linux.cpp`,
   `src/platform/windowactivation_linux.cpp` and their headers; `qtranslate_core` supplies the rest).
   Follow `tests/tst_hotkey_selection_x11.cpp` for the Xvfb assumptions (no window manager, the test
   changes the input focus and the clipboard), the `EnvironmentGuard` and the guard that releases
   every key on exit.
2. Test cases:
   - `replacesFocusedEditorSelection`: create a `QTextEdit` with "teh cat", select "teh", activate
     it, run `SelectionGrabber::grab()` and wait for the callback, feed a synthetic
     `ProofreadingResult` for "teh" with the replacement "the" into
     `ProofreadingPopupWindow::showChecking()/showResult()`, click `Replace selection`, put the
     popup's `correctedText()` on the clipboard and call `Platform::sendPasteShortcut()`; assert the
     editor contains "the cat" and that the popup emitted `replaceRequested()` once.
   - `popupDoesNotTakeFocus`: after `ProofreadingPopupWindow::show()`, the editor is still
     `Platform::foregroundWindow()`/`QApplication::focusWindow()` and the popup is not; this is the
     premise of the AppController guard.
   - `grabberRestoresClipboard`: put a marker with a non-text MIME type on the clipboard before the
     grab, assert the marker is back after the grabber finishes (the grabber's own restore), and note
     that the post-paste restore of `AppController` is manual.
   - `noSelectionIsEmpty`: an editor without a selection produces an empty grab result; do not assert
     the AppController message.
   - Skip cleanly with `QSKIP` when the display, XTEST or the X11 grab is unavailable, using the same
     reasons as the existing X11 tests.
3. If the test exposes a defect in `SelectionGrabber`, `WindowActivation`, the popup placement or the
   non-activation flags, fix it here and keep the change minimal; do not change the frozen
   `HotkeyManager` API.
4. Register the target in `tests/CMakeLists.txt` inside the
   `CMAKE_SYSTEM_NAME STREQUAL "Linux"` block with the existing
   `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=xcb")`; Linux becomes 45 tests, Windows
   stays 39. The binary needs the same trusted-client treatment as the other X11 tests on Astra
   (`/etc/X11/trusted.d` or `fly-set-execid.sh`); record the step.
5. Run the new test repeatedly under `xvfb-run` (the X11 clipboard and focus timing is the flake
   risk: use `QTRY_*` with generous timeouts, wait for the clipboard to settle and restore the focus
   in `cleanup()`), then the full suite in Debug and Release (45/45, zero warnings). Record a real
   `:0`/Astra check of the window-change fallback as performed or not performed, never as passed
   without evidence.

## Done when

- The new Linux test runs under Xvfb, focuses a real editor, grabs its selection through
  `SelectionGrabber`, shows the confirmation window without taking focus, pastes the corrected text
  after the confirmation and asserts the editor content changed.
- The test asserts the clipboard path it covers and skips cleanly when X11/XTEST is unavailable; the
  window-change and Wayland fallbacks are named as manual gaps, not as automated.
- Linux Debug and Release build with `-Wall -Wextra` and the suite is 45/45 with zero warnings;
  Windows 39 remains not performed; the Astra trusted-client step is recorded.
- No text from the editor or from an issue reaches the log.
