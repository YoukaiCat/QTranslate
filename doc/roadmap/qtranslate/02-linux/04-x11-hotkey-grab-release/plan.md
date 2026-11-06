---
kind: task
status: done
detailed: true
depends: [02-global-hotkeys, 03-x11-selection-and-inplace]
---

# X11 hotkey grab release order

## Task

Fix the X11 selection capture defect found after `03-x11-selection-and-inplace`. When the user
releases the hotkey modifiers before the main key (Ctrl and Shift before Q), or holds the main key
longer than the 300 ms release wait, the passive `XGrabKey` for the hotkey is still active. The
synthetic Ctrl+C is then delivered to QTranslate instead of the focused application; the clipboard
does not change and the grabber reports "nothing is selected" after its 400 ms timeout. Both Ctrl+Q
and Ctrl+Shift+Q are affected.

The user approved changing the `HotkeyManager` behavior/API if needed, for example by passing the
activating keycode to the grabber or by explicitly releasing the active keyboard grab before the
synthetic copy. Prefer the smallest seam that keeps the public `HotkeyManager` API stable; if the API
must change, record the change and its Windows review explicitly. The fix must not regress the X11
hotkeys (16/16), the Wayland portal (20/20), the selection grabber tests, or the Windows
implementation.

## Steps

The fix stays in the Linux selection grabber; `HotkeyManager` and its public API, signals, and
header stay unchanged. A passive `XGrabKey` starts an active keyboard grab on the manager's X
client that ends only when the activating key is released. Releasing Ctrl/Shift first does not end
it, so a Ctrl+C sent while Q is still down is routed to the manager instead of the focused
application. The chosen seam is to make the grabber wait for every non-modifier key that was down
when the grab started (normally the activating hotkey key) before sending Ctrl+C; the active grab
then ends naturally on the main-key release, auto-repeat suppression in `HotkeyManager` stays
intact, and no API change is needed. Rejected: `XUngrabKeyboard` at activation in `HotkeyManager`
(it would end the grab while the activating key is still down, let keyboard auto-repeat reach the
focused application, and can re-activate the passive grab on repeats; the manager would also lose
the later key release that cleans `pressedKeys`), and passing the keycode through the public
`activated` signal/`AppController` (a public API change plus Windows/Wayland churn for the same
result). The user approved an API change if needed; it is not needed here.

1. Extend `SelectionGrabber::LinuxState` in `src/platform/selectiongrabber_linux.cpp`
   (`anyModifierDown`, `releaseModifiers`, `startCopy`, `poll`, `grab`; no header change).
   - Add `QList<KeyCode> triggerKeys` (include `<QList>`) and the helpers `captureTriggerKeys()`
     and `triggerKeysDown()`.
   - `captureTriggerKeys()`: call `XQueryKeymap` on the grabber's display; iterate keycodes 8-255;
     skip the five resolved modifier keycodes (`control`, `shift`, `alt`, `superLeft`,
     `superRight`); store every other keycode that is down. At activation this is the hotkey's main
     key (Q or W); any other non-modifier key held at grab start is included conservatively. Log
     the count at `qtranslate.grab` debug level.
   - `triggerKeysDown()`: one `XQueryKeymap`; return true when any stored keycode is still down,
     false for an empty list or no display.
   - Call `captureTriggerKeys()` in `grab()` after `ensureDisplay()` and before setting
     `State::WaitingForRelease`; clear the list at the start of `grab()`, in `cancel()` and in
     `finish()`.
   - Rework the `WaitingForRelease` branch of `poll()`: read `anyModifierDown()`; when modifiers are
     still down at `kMaxReleaseWaitMs` (300 ms), call `releaseModifiers()` and return so the next
     10 ms tick re-checks (do not call `startCopy()` in the same tick); call `startCopy()` only
     when no modifier and no stored trigger key is down; otherwise keep waiting. `startCopy()`, the
     400 ms clipboard wait, the snapshot/restore and `cancel()` keep their behavior.
   - Comment the X11 fact: the active grab belongs to `HotkeyManager`'s display client, so the
     grabber cannot release it with `XUngrabKeyboard`; waiting for the activating key is the
     correct seam. Note that this preserves the manager's auto-repeat suppression (`pressedKeys`)
     and its normal `KeyRelease` cleanup.
   - Wayland: `captureTriggerKeys()` is not called, `triggerKeysDown()` returns false, and no X11
     display or synthetic input is used.
   - Do not touch `src/platform/selectiongrabber.h`, `src/platform/hotkeymanager.*`,
     `src/app/appcontroller.cpp`, the Windows files, or the application `CMakeLists.txt`.

2. Add `tests/tst_hotkey_selection_x11.cpp`, a new `QTEST_MAIN` test with a real `QApplication`
   (xcb under `xvfb-run`).
   - Helpers: the `EnvironmentGuard` pattern from the existing Linux tests; the focus helper from
     `tests/tst_selectiongrabber_linux.cpp` (`show`, `qWaitForWindowExposed`, `XSetInputFocus`,
     verify `XGetInputFocus`, `setFocus`); and an RAII key guard that releases every pressed
     keycode with `XSync` in its destructor, so a failed assertion cannot leave a key down or a
     grab active.
   - `initTestCase()`: open the display and `QSKIP` when it fails or when
     `QGuiApplication::platformName() != "xcb"`; check `XTestQueryExtension`; save and clear
     `WAYLAND_DISPLAY`, set `XDG_SESSION_TYPE=x11`; probe a spare passive `XGrabKey`
     (`Ctrl+Alt+Shift+F6/F7/F8`) on a separate display and `QSKIP` with the Parsec reason on
     `BadAccess`; restore the environment in `cleanupTestCase()`. This mirrors the proven guard in
     `tests/tst_hotkeymanager_x11.cpp`.
   - Use the exact `AppController::createHotkeys()` seam: create a `HotkeyManager`, register one
     action, create a `SelectionGrabber`, connect `HotkeyManager::activated` to a lambda that calls
     `grabber.grab([...])`. The full `AppController` (settings, translation manager, tray) is not
     needed and would not change the grab interaction.
   - If `registerHotkey` returns false, `QSKIP` only when the `registrationFailed` reason contains
     "another program" (a desktop shortcut owns the combination); any other failure is a test
     failure with the reason, as in `tst_hotkeymanager_x11`.
   - `releasesModifiersBeforeMainKeyReplace()`: a focused `QTextEdit` with a selection; a marker
     clipboard with text plus a custom format; register `Ctrl+Shift+Q` for
     `TranslateAndReplace`; record `activated`; press Ctrl, Shift and Q with XTest and `XSync`;
     `QTRY_COMPARE` the activation; release Ctrl and Shift, keep Q down; wait 600 ms; assert the
     grabber is still busy and the callback has not fired (the old code calls back empty after
     about 400 ms); release Q; `QTRY` the callback; compare the selection text, assert the editor
     text is unchanged (no auto-repeat `q` reached it), and compare the restored marker/custom
     format.
   - `releasesModifiersBeforeMainKeyPopup()`: the same with `Ctrl+Q` and `TranslateToPopup`.
   - `holdsCombinationPastModifierTimeout()`: press Ctrl+Shift+Q and keep all keys down for 800 ms
     (the 300 ms path releases the modifiers but must keep waiting for Q); assert no callback;
     release Q; expect the selection. The old code copies at 300 ms while Q is still grabbed and
     calls back empty at about 700 ms, so this function fails before the fix.
   - `normalReleaseOrderStillWorks()`: press Ctrl+Shift+Q, release Q first, then Ctrl/Shift; expect
     the selection (regression for the working order).
   - `secondActivationAfterCapture()`: after a complete normal-order capture, press the same
     combination again and expect a second callback with the selection; this proves the manager saw
     the Q release and `pressedKeys` is clean, and it catches an early-ungrab implementation that
     would miss the release.
   - Comment in the file that the test must run under `xvfb-run` with `QT_QPA_PLATFORM=xcb`, changes
     the X input focus and the clipboard, and assumes no window manager.
   - The first three functions are the pre-fix reproducers; the other two are regression checks.
     Build the new target against the unmodified grabber first and record the failure (step 4).

3. Wire the new test in `tests/CMakeLists.txt` (Linux block only).
   - Add `tst_hotkey_selection_x11` with `src/platform/hotkeymanager.h`,
     `src/platform/portalshortcut.h`, `src/platform/hotkeymanager_linux.cpp`,
     `src/platform/selectiongrabber.h`, `src/platform/sessiontype.h`, and
     `src/platform/selectiongrabber_linux.cpp`; link `Qt::Widgets X11::X11 X11::Xtst Qt6::DBus`;
     set `ENVIRONMENT "QT_QPA_PLATFORM=xcb"`.
   - Keep the `if(WIN32)` block, the two hotkey targets, `tst_selectiongrabber_linux` and
     `tst_windowactivation_linux` unchanged. The application `CMakeLists.txt` needs no change:
     `X11::X11`, `X11::Xtst` and `Qt6::DBus` are already Linux dependencies.
   - A new target is chosen over adding the function to `tst_selectiongrabber_linux` or
     `tst_hotkeymanager_x11`: it keeps the existing 12/12 and guiless 16/16 targets untouched and
     does not leak the new Widgets/DBus hotkey-manager dependencies into them.
   - Linux becomes 25 CTest tests (the 24 existing plus the new target); the existing targets keep
     16, 20, 12 and 13 QTest passes; Windows keeps 23. The new target reports 7 passes (5 test
     functions plus init/cleanup) when the display and XTEST are available.

4. Build and verify in the `debian:trixie` container (Qt 6.8.2), before and after the fix.
   - Build the new target against the unmodified grabber from the read-only source mount; run only
     `tst_hotkey_selection_x11` under `xvfb-run` and capture the failure of the three reproducer
     functions (empty selection after the 400 ms wait while Q is held; the target exits non-zero).
     If the test passes before the fix, stop and investigate the test/grab interaction; do not fake
     a pass.
   - Apply step 1; rebuild Debug and Release with `-Wall -Wextra`; grep the build log for
     `warning:` and expect none.
   - `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure` must be 25/25 in Debug
     and Release; run the new test alone and record its 7 passes; run
     `-R 'tst_hotkeymanager_x11|tst_hotkeymanager_wayland|tst_selectiongrabber_linux|tst_windowactivation_linux'`
     and record 16/16, 20/20, 12/12, 13/13.
   - Keep the runs isolated: concurrent Debug/Release CTest on one host has produced
     `XOpenDisplay` flakes before, and `timeout` is mandatory.

5. Build and verify on Astra Linux 1.8.7 (Qt 6.10.2), then check the real X11 session.
   - Native Debug and Release in `build/linux-debug` and `build/linux-release`; 0 `-Wall -Wextra`
     warnings; run CTest. After every rebuild run `sudo /usr/bin/fly-set-execid.sh`; the existing
     `tst_hotkeymanager_x11` must stay 16/16 and `tst_hotkeymanager_wayland` 20/20; the grabber and
     window tests 12/12 and 13/13.
   - The new test target is a new binary, and Parsec denies passive grabs to untrusted binaries. To
     get an Astra pass, add `build/linux-{debug,release}/tests/tst_hotkey_selection_x11` to
     `/etc/X11/trusted.d/qtranslate` and rerun `fly-set-execid.sh`. If that host change is not made,
     record the new target as `QSKIP` with the Parsec reason: the trixie run is the automated gate
     and the manual check below is the Astra evidence. Do not treat the skip as a code failure and
     do not fake a pass.
   - Manual check on the real `:0` session with the release binary
     (`build/linux-release/qtranslate --verbose`) and a real editor (the previous check used
     `kate`): select text and, for Ctrl+Shift+Q and Ctrl+Q, do (a) release Ctrl/Shift first, hold Q
     about 1 s, then release Q; (b) release Q first; (c) hold the whole combination longer than
     300 ms. In every case the translation must arrive, the clipboard must be restored, and the
     editor must not receive repeated `q` characters while Q is held (the active grab stays in
     place, so auto-repeat is suppressed). Record the editor, desktop, key order, and observed
     result. If the session or the trusted attribute is unavailable, record the check as not
     performed with the reason.
   - Clang 16 Debug if time allows; the existing baseline is the requirement.

6. Review Windows and report; do not edit documentation or commit.
   - Confirm with `git diff` that the only changed files are
     `src/platform/selectiongrabber_linux.cpp`, the new test, and the Linux block of
     `tests/CMakeLists.txt`; no public `HotkeyManager`/`SelectionGrabber` header change, no
     `*_win.cpp`, no `if(WIN32)` block, no application `CMakeLists.txt`, and no `doc/`, README,
     `AGENTS.md` or `handoff.md` changes. Windows Debug/Release and the 23 tests are not performed
     here (no MSVC); the review is by diff.
   - Report for the lead: the chosen mechanism and why the `XUngrabKeyboard` and public-keycode
     alternatives were rejected; the pre-fix failure log and the post-fix pass; the new test name
     and Linux 25 / Windows 23; the existing 16/16, 20/20, 12/12, 13/13; the trixie/Astra results;
     the manual `:0` result or not performed; the Astra `trusted.d` caveat for the new binary; the
     fact that `qtranslate_core` stays X11-free and Wayland has no synthetic input. Do not commit;
     the lead commits.

## Done when

- `src/platform/selectiongrabber_linux.cpp` waits for the non-modifier keys that were down when
  `grab()` started (the activating hotkey key) in addition to the modifiers; Ctrl+C is sent only
  after the server reports those keys released, so the manager's passive grab is no longer active
  and the copy reaches the focused application. The 300 ms modifier timeout still releases held
  modifiers first; `startCopy()`, the 400 ms clipboard wait, the snapshot/restore and `cancel()`
  keep their behavior. Wayland reads the manually copied clipboard without an X11 display or
  synthetic input.
- `HotkeyManager`, `HotkeyManager::activated`/`registrationFailed`, the public headers,
  `AppController`, `hotkeymanager_win.cpp`, `selectiongrabber_win.cpp` and the application
  `CMakeLists.txt` are unchanged; the manager's `pressedKeys` still clears on the normal key
  release and repeat suppression is preserved.
- The new `tst_hotkey_selection_x11` test uses a real `HotkeyManager`, a real `SelectionGrabber`
  and a focused `QTextEdit` under `xvfb-run` with `QT_QPA_PLATFORM=xcb`; before the fix its three
  reproducer functions fail with the empty-selection/timeout evidence; after the fix all its
  functions pass; it covers Ctrl+Q and Ctrl+Shift+Q with Ctrl/Shift released before Q, a
  whole-combination hold past 300 ms, the normal release order, and a second activation after a
  capture.
- In `debian:trixie` Debug and Release: no `-Wall -Wextra` warnings; `timeout 300 xvfb-run -a ctest`
  is 25/25; the new target passes without skips; `tst_hotkeymanager_x11` remains 16/16,
  `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12 and
  `tst_windowactivation_linux` 13/13. Linux has 25 CTest tests; Windows keeps 23.
- On Astra Linux Debug and Release build without warnings and the existing targets keep their
  counts; the new target passes if its binary is trusted, or is recorded as `QSKIP` with the
  Parsec reason if `trusted.d` is not extended. The manual `:0` check covers Ctrl+Q and
  Ctrl+Shift+Q in both release orders and the >300 ms hold, the clipboard is restored, and no
  repeated `q` characters appear; the result is recorded, or the check is recorded as not
  performed with the reason.
- The Windows build and the 23 Windows tests are recorded as not performed (no MSVC); the diff
  review confirms no Windows or public API changes; `qtranslate_core` stays X11-free and no
  synthetic input is added on Wayland. `doc/handoff.md`, README, `AGENTS.md`,
  `doc/architecture.md`, other roadmap nodes and the journal are untouched; the lead commits.

## Risks

- The chosen fix changes the capture timing: while the activating key is held, the grabber waits;
  a physically stuck key delays the capture until it is released (there is no timeout for the main
  key). That is the intended behavior for the >300 ms hold, but it is a new edge: do not add a
  short main-key timeout that recreates the defect. If a stuck-key guard is wanted later, it needs
  a separate decision and a test.
- The trigger-key set is inferred from `XQueryKeymap`, not passed by `HotkeyManager`, so it includes
  any non-modifier key held at grab start, not only the hotkey key. An unrelated held key can delay
  the capture until it is released. This is conservative (it avoids sending Ctrl+C while another
  key is down) and is the price of keeping the public API unchanged; passing the keycode would
  change `activated`, `AppController`, Windows and Wayland and is rejected for this task.
- Do not add `XUngrabKeyboard` to the grabber: the active grab belongs to `HotkeyManager`'s X
  client, not the grabber's, so the grabber cannot release it. Doing it in `HotkeyManager` at
  activation would end the grab while the activating key is still down, let keyboard auto-repeat
  reach the focused application, and can re-activate the passive grab on repeats; the manager would
  also lose the later key release needed to clean `pressedKeys`. The integration test's
  second-activation function guards that state.
- Keep the existing `anyModifierDown()`/`releaseModifiers()` behavior: they resolve only the left
  Control/Shift/Alt and both Super keys, while the trigger-key snapshot conservatively includes
  right-side modifiers and other held keys. A right modifier pressed after grab start is an
  existing left-only limitation, not part of this defect; do not expand the modifier table without
  a separate test.
- The 300 ms path releases the modifiers with XTest while the active grab is still held; those fake
  releases go to the grabbing client and update the server state, as today. The copy must still
  wait for the main key; do not call `startCopy()` in the same poll after `releaseModifiers()`.
- The test's 600 ms and 800 ms waits are deliberate: the old code calls back empty after about
  400 ms, and shorter waits would hide the pre-fix failure. Do not weaken the assertions or replace
  them with a sleep-only check.
- Xvfb has no window manager; the focused `QTextEdit` needs `XSetInputFocus` and `setFocus()`, as in
  the existing grabber test. A keyboard grab can produce FocusOut/FocusIn around the activation; if
  the `QTextEdit` loses the Qt focus, debug the focus helper, do not skip the test. The test must
  run under `xvfb-run` with the xcb plugin; never against the user's real session.
- The new test target on Astra is a new binary and Parsec denies it passive grabs unless it is
  added to `/etc/X11/trusted.d/qtranslate` and `fly-set-execid.sh` is rerun. Without that host step
  it `QSKIP`s; record the reason honestly and use trixie plus the manual `:0` check. Do not claim
  an Astra pass that did not happen.
- `XOpenDisplay` and XTest can flake under load in this environment; run the configurations
  isolated with `timeout 300` and rerun once before treating a failure as a regression. Never fake
  a pass.
- The clipboard uses `QClipboard::changed`; a same-owner re-copy of identical text can miss the
  change (existing limitation). The test uses a distinct marker and selection text to avoid it.
- Windows cannot be compiled here. The fix is in a Linux-only `.cpp`, and the public headers and
  Windows files must stay untouched; verify by `git diff`. If the implementation is tempted to add
  a header/API seam, stop and report instead of silently changing the Windows contract.
- `doc/handoff.md`, README, architecture and the journal are the lead's; do not update them in this
  task. The final report is the only place for the new counts, the pre-fix evidence and the Astra
  caveat.

## Outcome

Done. The X11 grab-release defect is fixed in `SelectionGrabber::LinuxState` without changing the
public API; Linux has 25 tests, Windows keeps 23.

- Files: `src/platform/selectiongrabber_linux.cpp` (the fix), new `tests/tst_hotkey_selection_x11.cpp`,
  and the Linux block of `tests/CMakeLists.txt` (the new target). `selectiongrabber.h`,
  `hotkeymanager.*`, `appcontroller.*`, the `*_win.cpp` files, the application `CMakeLists.txt`, the
  `if(WIN32)` block, docs and the handoff are untouched; `qtranslate_core` stays X11-free.
- Mechanism: `LinuxState` snapshots the non-modifier keycodes that are down at `grab()` start with
  `XQueryKeymap` (normally the activating Q/W) and `poll()` waits for both the modifiers and those
  keys before sending Ctrl+C. The passive `XGrabKey` therefore ends naturally on the main-key release,
  the synthetic copy reaches the focused application, and the manager keeps its auto-repeat
  suppression and the `KeyRelease` that clears `pressedKeys`. The 300 ms modifier path still calls
  `releaseModifiers()`, returns, and re-checks on the next 10 ms tick; it never calls `startCopy()` in
  the same tick. `startCopy()`, the 400 ms clipboard wait, the snapshot/restore and `cancel()` are
  unchanged. On Wayland the snapshot is not taken and no display or synthetic input is used.
- Test: `tst_hotkey_selection_x11` uses a real `HotkeyManager` connected to a real `SelectionGrabber`
  and a focused `QTextEdit` under `xvfb-run` with `QT_QPA_PLATFORM=xcb`. It covers Ctrl+Shift+Q and
  Ctrl+Q with the modifiers released first, the whole combination held past 300 ms, the normal
  release order, and a second activation after a capture. It probes a spare passive grab and `QSKIP`s
  with the Parsec reason when the server denies grabs; `registerHotkey` failures are `QSKIP` only for
  an "another program" conflict and fail otherwise.
- Pre-fix gate: the new target built against `HEAD`'s grabber in `qtranslate-trixie` failed with
  `4 passed, 3 failed`; `releasesModifiersBeforeMainKeyReplace`, `releasesModifiersBeforeMainKeyPopup`
  and `holdsCombinationPastModifierTimeout` failed on `grabber.isBusy()` being false after 600/800 ms
  (the old code sent Ctrl+C into the still-active grab and reported an empty selection). The reviewer
  independently rebuilt the same combination and reproduced 4/3. The two regression functions passed
  before the fix as designed. Evidence: `/tmp/opencode/trixie-prefix-gate.log`.
- Verification:
  - `debian:trixie` (Qt 6.8.2, GCC 14.2) Debug and Release from the read-only mount with
    `-Wall -Wextra`: 0 warnings; `timeout 300 xvfb-run -a ctest` 25/25 in both configurations;
    `tst_hotkey_selection_x11` 7/7 with 0 skips, `tst_hotkeymanager_x11` 16/16,
    `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12 and
    `tst_windowactivation_linux` 13/13.
  - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) native `build/linux-debug` and `build/linux-release`:
    0 warnings; CTest 25/25 in both configurations; the same 7/7, 16/16, 20/20, 12/12 and 13/13
    counts. The new test binary is trusted: two lines were appended to
    `/etc/X11/trusted.d/qtranslate` (the host change approved by the user) and
    `sudo /usr/bin/fly-set-execid.sh` was rerun after every rebuild. Without that step the new target
    would `QSKIP` on Parsec; this is recorded, not hidden.
  - Clang 16 Debug builds clean with 0 warnings; its CTest was not run because that binary is not in
    `trusted.d`. It is not the gate.
  - Manual check on the real Astra X11 `:0` session (Fly desktop, `kate`, the release binary, and
    `xdotool` to send the key sequences): Ctrl+Shift+Q and Ctrl+Q with (a) Ctrl/Shift released first
    and Q held about 1 s, (b) Q released first, (c) the whole combination held 0.8 s. All six cases
    grabbed the 44-character selection, translated it (replace: the kate text was replaced; popup:
    the popup was shown), restored the clipboard marker and left no repeated `q` in kate. The verbose
    log shows `Non-modifier keys held at grab start: 1` and, for the (c) cases, `Modifiers are still
    down after 300 ms` followed by a successful grab after Q was released. Evidence:
    `/tmp/opencode/manual-qtranslate-full.log` and the popup screenshots under `/tmp/opencode`. One
    first attempt failed because the X focus had moved to Firefox; the rerun checked
    `xdotool getactivewindow` before each case. The original clipboard text was restored afterwards
    (only the text format; the pre-check clipboard had other formats that a text-only copy cannot
    restore). qtranslate and kate were stopped.
- Review: no real defects or regressions; the reviewer reproduced the pre-fix 4/3 failure, the
  trixie Debug/Release 25/25 and all counts. One accepted remark fixed the `tests/CMakeLists.txt`
  comment that called the hotkey tests "Both" (three targets now compile that translation unit). The
  unused `EnvironmentGuard` helper in the new test was rejected: the plan asked for that pattern and
  it has no behavioral impact.
- Known limitations (as planned): the trigger set is inferred from `XQueryKeymap`, so any other
  non-modifier key held at grab start delays the capture until it is released; there is no timeout
  for the main key by design (a stuck key blocks the capture until release; a stuck-key guard would
  need a separate decision). Right-side modifiers and other held keys are conservatively included;
  the left-only modifier table is unchanged. The same-owner identical-text clipboard limitation is
  unchanged.
- Windows: Debug/Release and the 23 tests were not performed (no MSVC); the diff review confirms no
  public header, `hotkeymanager_win.cpp`, `selectiongrabber_win.cpp`, `if(WIN32)`, `AppController` or
  application CMake change.
- Facts for the documentation (not edited here): Linux has 25 tests and Windows 23; the new
  `tst_hotkey_selection_x11` binary must be added to `/etc/X11/trusted.d/qtranslate` and followed by
  `sudo /usr/bin/fly-set-execid.sh` for an Astra pass, otherwise it `QSKIP`s on Parsec; the trixie
  `xvfb-run` gate is the automated evidence.
