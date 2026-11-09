---
kind: task
status: done
detailed: true
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

## Outcome

- Added `tests/tst_proofreading_selection_x11.cpp` and registered the target in the
  `CMAKE_SYSTEM_NAME STREQUAL "Linux"` block of `tests/CMakeLists.txt` with
  `QT_QPA_PLATFORM=xcb`, `Qt::Widgets`, `X11::X11`, `X11::Xtst` and the real popup, panel, error
  text, grabber and window-activation sources. No application source changed: the test found no
  defect in `SelectionGrabber`, `WindowActivation` or the popup placement and non-activation flags.
- Cases, all on the real platform code under Xvfb: `replacesFocusedEditorSelection` grabs "teh"
  from "teh cat" through XTest Ctrl+C, feeds the synthetic issue (0/3 → "the"), clicks
  `Replace selection` (`replaceRequested()` exactly once), snapshots `correctedText()` before
  `hide()` as `AppController` does, puts it on the clipboard and calls the real
  `Platform::sendPasteShortcut()`, then waits for "the cat"; `popupDoesNotTakeFocus` shows the
  popup and asserts that `QApplication::focusWindow()` and `Platform::foregroundWindow()` still
  belong to the editor (the premise of the controller guard, not the guard itself);
  `grabberRestoresClipboard` checks the grabber's own restore of a marker with a non-text MIME
  type; `noSelectionIsEmpty` checks the empty result without asserting the controller message.
  The test repeats the controller's order (hide, foreground check, clipboard, paste) in the test
  body instead of linking `AppController`.
- Flake risk: the X11 focus and clipboard are set manually because there is no window manager;
  `EnvironmentGuard` removes `WAYLAND_DISPLAY` and sets `XDG_SESSION_TYPE=x11`, `PressedKeysGuard`
  releases every key, `cleanup()` restores `PointerRoot`, and all waits are `QTRY_*` with 2–5 s.
  A missing display, a non-xcb platform or a missing XTEST extension skips through the same
  `QSKIP` reasons as the existing X11 tests. The XTEST-only grab needs no passive `XGrabKey`.
- Verification: Linux Debug and Release build with `-Wall -Wextra` and zero warnings; the full
  `xvfb-run -a ctest` is 45/45 in both configurations (the new test is #42). The new test passed
  60+ consecutive runs during development and `--repeat until-fail:10` in Debug after the final
  edit (5 runs in Release), with no flake. `noSelectionIsEmpty` intentionally waits for the
  grabber's 400 ms clipboard timeout. Nothing logs the editor text or an issue message.
- Astra trusted-client step: the test ran on Astra 1.8 with Qt 6.10.2 under Xvfb with 6 passed,
  0 skipped **without** a new `/etc/X11/trusted.d` entry. The XTEST injection used here is
  permitted for an untrusted client, unlike the passive `XGrabKey` path that required the
  `KBD_R` entries for `tst_hotkey_selection_x11`; the existing entries were not changed.
  `fly-set-execid.sh` was therefore not needed for the new binary.
- Translations: no new strings; the measured total stays **382** `<message>` in each of the 14
  files and `tst_translations` passes. Windows 39 tests are **not performed** (no MSVC here); the
  target exists only in the Linux block.
- Manual gaps, deliberately not automated: the `AppController` window-change fallback, the
  Wayland copy branch and the post-paste clipboard restore in `pasteText()` (500 ms) are not
  covered; a real `:0` session was **not** run, so those paths are recorded as not performed,
  not as passed.
- Known limitation, inherited from the other X11 tests: the `QSKIP` for a missing `DISPLAY` is
  unreachable because `QTEST_MAIN` creates the `QApplication` before `initTestCase`; without a
  display the process aborts instead of skipping. CI runs every X11 test under `xvfb-run`.
- Review: one minor remark accepted and fixed — the corrected-text snapshot is taken before
  `hide()`, exactly like `AppController` reads `m_proofreadCorrected`. The unreachable `DISPLAY`
  skip and a fixed `qWait(200)` in `popupDoesNotTakeFocus` were rejected as inherited behaviour
  and a non-defect.
- Found while working, not edited here: `AGENTS.md` still says 34 Windows and 39 Linux tests (now
  39 and 45), and `doc/proofreading.md` and `doc/handoff.md` still describe the external-selection
  stage as not implemented. Both were already stale before this task.
