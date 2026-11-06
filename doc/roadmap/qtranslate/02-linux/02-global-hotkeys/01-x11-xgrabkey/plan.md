---
kind: task
status: done
detailed: true
---

# X11 XGrabKey backend

## Task

Implement the X11 backend behind the existing `HotkeyManager` API. The manager registers global
hotkeys with `XGrabKey` on the root window, receives the key presses through its own Xlib display
connection and a `QSocketNotifier`, and keeps the public contract unchanged: `registerHotkey`,
`unregisterHotkey`, `unregisterAll`, `setSuspended`, `hotkey`, the `activated` and
`registrationFailed` signals, and the `HotkeyAction` enum. The stub is no longer used on Linux but
stays for other non-Windows platforms.

The implementation is `src/platform/hotkeymanager_x11.cpp`: selected on Linux in `CMakeLists.txt`
and linked with `X11::X11`; `qtranslate_core` stays free of X11. `XGrabKey` is per-client, so the
implementation uses a dedicated `Display*` and does not depend on Qt's xcb native event filter.
Support Control, Shift, Alt (`Mod1`), and Super (`Mod4`) plus the Lock/NumLock variants so Caps Lock
or Num Lock does not break a hotkey; detect `BadAccess` with a scoped X error handler and `XSync`;
ungrab on unregister, suspend, and destruction, and re-grab on resume. Map `QKeySequence` to an X11
keysym and keycode in this platform file; the shared `core/hotkeysequence` helper is Win32-specific
and stays untouched. Reject empty, modifier-only, multi-chord, and unknown sequences, and also keys
that the current keyboard layout cannot resolve to a keycode.

Session rule: on a Wayland session (`WAYLAND_DISPLAY` set, `XDG_SESSION_TYPE=wayland`, or the Qt
platform name starting with `wayland`) the X11 backend must not open a display. `registerHotkey`
remembers the sequence and returns false with a reason that points to the `GlobalShortcuts` portal
and the CLI fallback; the sibling task `02-wayland-globalshortcuts` adds the portal path. This keeps
the stage rule "no X11 display is opened on a Wayland session", including an XWayland session where
Qt was forced to the `xcb` plugin.

Testing decision: the Windows `tests/tst_hotkeymanager.cpp` stays untouched and Windows-only. Linux
gets a sibling `tests/tst_hotkeymanager_x11.cpp` (architecture §4.14 says the Windows platform tests
are replaced by Linux analogues). It uses `QTEST_GUILESS_MAIN` (its only display requirement is
`DISPLAY`, not a Qt platform plugin), skips when `XOpenDisplay` fails, and drives a real activation
through XTest when the extension is available. XTest is used by the test only; the runtime backend
does not synthesize input.

## Steps

1. Wire the build in `CMakeLists.txt`.
   - After the Qt `find_package`, add
     `if(CMAKE_SYSTEM_NAME STREQUAL "Linux") find_package(X11 REQUIRED) endif()`. Use the same
     condition as the header's `Q_OS_LINUX` guards, so no other Unix (for example, macOS or
     FreeBSD) tries to compile the X11 backend against a header without its private members.
     `libx11-dev` is already in `packaging/linux/trixie.Dockerfile` and installed on Astra, so
     `REQUIRED` changes no supported build; a missing package should fail configure with a clear
     CMake error instead of silently building a stub.
   - In the `qtranslate` platform block, keep `if(WIN32)` byte-identical. Replace the current
     `else()` with `elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")`: add
     `src/platform/hotkeymanager_x11.cpp` and the
     four existing stubs (`autostart`, `secretprotector`, `selectiongrabber`, `windowactivation`),
     and `target_link_libraries(qtranslate PRIVATE X11::X11)`. Keep a final `else()` with all five
     stubs for macOS and other non-Windows platforms. Do not link X11 into `qtranslate_core`.
   - Update the stale comment in `src/platform/hotkeymanager_stub.cpp` ("the application is
     Windows-only") to say that it serves the remaining non-Windows platforms.
2. Add the platform-private seam in `src/platform/hotkeymanager.h` without touching the public API.
   - Update the class comment to name `hotkeymanager_win.cpp` and `hotkeymanager_x11.cpp`.
   - Keep the public methods, the signals, the `QAbstractNativeEventFilter` base,
     `nativeEventFilter()`, `Registration::sequence`, `Registration::registered`,
     `m_registrations`, and `m_suspended` unchanged.
   - Put the Windows-only private parts (`Registration::hotkey`, `idForAction`,
     `m_actionsById`) inside `#if defined(Q_OS_WIN)`. In the `#elif defined(Q_OS_LINUX)` branch add
     `quint32 keycode = 0; quint32 modifiers = 0;` to `Registration`, a forward declaration
     `struct X11State;` and `std::unique_ptr<X11State> m_x11;` (include `<memory>`). No Xlib type may
     appear in the header; `X11State` is defined in the `.cpp` and the destructor is already
     out-of-line. Verify with `git diff` that the Windows declarations are unchanged text, and that
     the non-Windows branch compiles on Linux (Windows cannot be compiled here; review only).
3. Create `src/platform/hotkeymanager_x11.cpp` with the session rule and the manager skeleton.
   - `Q_LOGGING_CATEGORY(lcHotkey, "qtranslate.hotkey", QtWarningMsg)`, the same category name as
     the Windows file (the two translation units are never linked together).
   - `isWaylandSession()`: true when `WAYLAND_DISPLAY` is non-empty, or `XDG_SESSION_TYPE` equals
     `wayland`, or `QGuiApplication::platformName()` (when a `QGuiApplication` exists) starts with
     `wayland`. Use `qEnvironmentVariableIsEmpty`/`qEnvironmentVariable`, not `isSet`, so an empty
     `WAYLAND_DISPLAY` from a test environment does not select the Wayland path.
   - Constructor: only `qRegisterMetaType<HotkeyAction>("HotkeyAction")`; do not open a display and
     do not install the native event filter.
   - `registerHotkey`: validate the sequence first (like Windows), replace the action's stored
     registration, then on a Wayland session store the sequence, log, emit `registrationFailed`
     with a non-empty reason and return false *without* calling `XOpenDisplay`. When suspended,
     keep Windows semantics: store and return true. Otherwise call the X11 `registerNative`.
   - `nativeEventFilter()` stays as a false-returning stub for API parity.
   - Reason strings are diagnostics that go to the signal and the log, not to a dialog (the tray
     message is the existing translated `AppController` text). Use `QStringLiteral` here; the
     Wayland task can promote the user-facing warning to `tr()` when it shows one.
   - Keep the Wayland decision in this single `isWaylandSession()`/branch so the sibling task can
     replace the branch with the portal path without touching the X11 code; if it becomes cleaner,
     that task may rename the file to `hotkeymanager_linux.cpp` and add a dispatch, but this task
     does not pre-build that seam.
4. Implement the sequence mapping in the anonymous namespace of the `.cpp`.
   - One chord only (`sequence.count() == 1`), `key != 0` and not `Qt::Key_unknown`.
   - Modifiers: `ControlModifier` → `ControlMask`, `ShiftModifier` → `ShiftMask`, `AltModifier` →
     `Mod1Mask`, `MetaModifier` → `Mod4Mask`, `GroupSwitchModifier` → `ControlMask | Mod1Mask`
     (the same normalisation as `core/hotkeysequence.cpp`); `KeypadModifier` selects the `KP_*`
     keysym. A modifier-only sequence has key 0 and is rejected.
   - Key table with X11 keysyms mirroring the supported set of `core/hotkeysequence.cpp`:
     `XK_a`–`XK_z` (lowercase; try uppercase as a fallback), `XK_0`–`XK_9`, keypad digits,
     `XK_F1`–`XK_F24`, Escape/Tab/BackSpace/Return/`KP_Enter`/Insert/Delete/Home/End/Page_Up/
     Page_Down/arrows/Pause/Print/Clear/Caps_Lock/Num_Lock/Scroll_Lock/Menu/space, the punctuation
     keys and their shifted forms (each shifted form adds `ShiftMask`, like the Windows table), and
     the keypad operators.
   - Resolve with `XKeysymToKeycode(display, keysym)` on the manager's display. A zero keycode means
     "not available in the current keyboard layout" and is reported as a failure, not as a conflict.
     Note in the code that a non-Latin layout can make `XK_q` unresolvable; do not guess another
     keysym.
5. Implement the display, the notifier, and event dispatch.
   - `X11State` (defined in the `.cpp`) holds `Display *display`, `Window root`,
     `QSocketNotifier *notifier`, `QSet<int> pressedKeys`, and `int lastError`.
   - `ensureDisplay()`: `XOpenDisplay(nullptr)`; on failure return false with a reason. Create
     `QSocketNotifier(ConnectionNumber(display), QSocketNotifier::Read)` connected to
     `readEvents()`. All Xlib calls stay on the manager's thread.
   - `readEvents()`: `while (XPending(display) > 0) XNextEvent(...)`. On `KeyPress`, clean the state
     with `state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)`, find the registration with the
     same keycode and mask, ignore the event when the keycode is already in `pressedKeys` (server
     auto-repeat), otherwise insert it and `emit activated(action)`. On `KeyRelease`, remove the
     keycode. Clear `pressedKeys` on suspend and on every ungrab path; document that X11 auto-repeat
     arrives as repeated `KeyPress` because detectable auto-repeat is not enabled.
6. Implement grabs and lifecycle.
   - Grab the four variants `mask`, `mask | LockMask`, `mask | Mod2Mask`,
     `mask | LockMask | Mod2Mask` with `XGrabKey(display, keycode, modifiers, root, False,
     GrabModeAsync, GrabModeAsync)`. `owner_events = False` matches the Windows "the combination is
     taken from all programs" behaviour.
   - Detect failures with a scoped `XSetErrorHandler` around the batch plus `XSync(display, False)`;
     restore the previous handler immediately. On `BadAccess` the reason is "already used by another
     program", on other codes include the X error code. On any failure ungrab the four variants
     (partial grabs must not stay) and set `registered = false`.
   - Re-registration of the same action ungrabs the old keycode/mask first, like Windows: the old
     combination is gone even if the new one fails.
   - `unregisterHotkey` ungrabs and erases; `setSuspended(true)` ungrabs everything, clears
     `pressedKeys` and keeps the sequences; `setSuspended(false)` grabs again and reports failures
     through the same signal; `unregisterAll` ungrabs, deletes the notifier, `XCloseDisplay`s and
     resets `m_x11`. `~HotkeyManager()` calls `unregisterAll()` first.
7. Add `tests/tst_hotkeymanager_x11.cpp` and its `tests/CMakeLists.txt` block.
   - `QTEST_GUILESS_MAIN`; include `platform/hotkeymanager.h`; add the test under the same
     `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` condition in `tests/CMakeLists.txt`; the test target
     compiles `src/platform/hotkeymanager_x11.cpp` and links `X11::X11` (pattern of the existing
     Windows test). When `X11_XTest_FOUND`, also link `X11::Xtst` and define
     `QTRANSLATE_HAVE_XTEST`.
   - `initTestCase()`: `QSKIP` when `XOpenDisplay(nullptr)` fails; clear `WAYLAND_DISPLAY` and set
     `XDG_SESSION_TYPE=x11` so `xvfb-run` on a Wayland desktop still tests the X11 path; probe a
     raw `XGrabKey` on a spare display and `QSKIP` with a clear message when the server denies
     passive grabs (Astra Linux Parsec denies them for untrusted binaries — see Risks). Restore the
     environment in `cleanupTestCase()`.
   - Test functions: the four actions (`Ctrl+Q` for the popup, `Ctrl+Shift+Q` for replace,
     `Ctrl+W` for the main window, and a distinctive combination for `ShowMainWindow`) register and
     `hotkey()` returns them (QSKIP that function if a desktop shortcut owns one of them); unregister releases the grab (a second manager can take it); two managers
     conflict and the second `registrationFailed` carries the action, the sequence and a non-empty
     reason; suspend releases the grab and resume re-grabs it (another manager grabs it in between
     and is destroyed); registration while suspended is deferred and applied on resume; empty,
     modifier-only, multi-chord (`Ctrl+K, Ctrl+C`), unknown (`Qt::Key_MediaPlay`) and
     layout-unresolvable sequences fail with a reason; `unregisterAll` releases everything (a second
     manager can take both combinations); on a Wayland session (`WAYLAND_DISPLAY=wayland-0` and an
     unused `DISPLAY=:77`) `registerHotkey` returns false with the Wayland reason instead of a
     display error; with XTest, pressing `Ctrl+Alt+Shift+F9` emits `activated(TranslateToPopup)`
     exactly once and a second press emits again.
   - Keep the key choices inside what Xvfb's keymap has: Xvfb maps `F1`–`F12` only (confirmed:
     `F24` has no keycode) and the X server reserves `Ctrl+Alt+F1`–`F12` for VT switching, so the
     tests use `Ctrl+Alt+Shift+F9`/`F10`/`F11` and the defaults, not `F13`–`F24` and not
     `Ctrl+Alt+F*`.
   - Keep the existing `if(WIN32)` test block untouched; the Windows test count stays 23, the Linux
     count becomes 21 (`20 + tst_hotkeymanager_x11`).
8. Build and verify.
   - Build Debug and Release in the `debian:trixie` container with the source mounted read-only
     (`cmake -S /src -B /tmp/build-<Config> -G Ninja -DCMAKE_BUILD_TYPE=<Config>`,
     `cmake --build ... --parallel`), grep the log for `warning:`.
   - Run `timeout 300 xvfb-run -a ctest --test-dir /tmp/build-<Config> --output-on-failure`: expect
     21/21; also run the new test alone with `-R tst_hotkeymanager_x11`. Repeat for Release.
   - On the Astra host, build and run the same suite: the 20 shared tests must pass, and the X11
     hotkey test is expected to `QSKIP` because of Parsec (capture the `XPARSEC: keylogger-grabber`
     line from the Xvfb log as evidence). Do not fake a pass and do not treat the skip as a code
     failure.
   - On the Astra host, run the new test under
     `xvfb-run -a valgrind --leak-check=full --error-exitcode=99 <test>` if time allows and record
     definite leaks; if Valgrind is skipped, the `XCloseDisplay`/notifier cleanup in the destructor
     and `unregisterAll` is the evidence.
   - Wayland guard: the unit test above covers the env-based rule. If a Wayland compositor is
     available (`weston --backend=headless-backend.so` with a private `XDG_RUNTIME_DIR` and the Qt
     Wayland plugin), start `qtranslate --verbose` there and confirm no X11 display is opened and the
     registration reason mentions the portal; otherwise record the Weston run as not performed and
     leave the full Wayland check to the sibling task.
   - Manual end-to-end: with the application running on a real X11 session, `xdotool key ctrl+q`
     must reach `AppController::translateSelectionInPopup` (the popup or its "nothing is selected"
     state is observable) and the four configured actions must be logged. On this Astra host the
     Parsec policy blocks the grab, so this check is recorded as blocked by the environment unless
     the policy trusts the binary; the XTest test covers the same path up to `activated`.
9. Report the facts for the lead; do not edit `doc/`, the READMEs, or `AGENTS.md`. Facts: the new
   file and test names, the Linux test count 21 (Windows 23), the `find_package(X11)` build
   requirement and the `libx11-6` runtime dependency (already pulled by Qt's xcb plugin), the
   Wayland detection rule, the Astra Parsec limitation, and the Xvfb keymap/VT-grab constraints.

## Done when

- `CMakeLists.txt` selects `src/platform/hotkeymanager_x11.cpp` on Linux, links `X11::X11`, keeps
  the stub for other non-Windows platforms, and leaves the Windows block unchanged; the tree
  configures and builds in Debug and Release in `debian:trixie` (Qt 6.8.2) with no `-Wall -Wextra`
  warnings.
- `xvfb-run -a ctest` passes 21/21 in the trixie container for Debug and Release, including
  `tst_hotkeymanager_x11`; `tst_hotkeymanager` remains Windows-only and the 20 shared tests still
  pass. The new test `QSKIP`s with a clear message when `XOpenDisplay` fails.
- Ctrl+Q, Ctrl+Shift+Q, Ctrl+W, F-keys, letters, digits, arrows, and common navigation keys register
  and map to valid X11 hotkeys; empty, modifier-only, multi-chord, unknown, and layout-unresolvable
  sequences return false and emit `registrationFailed` with a non-empty reason.
- Two managers cannot own the same combination: the second `registerHotkey` returns false and emits
  `registrationFailed` with the action, the sequence and a non-empty reason.
- Suspension releases the grab so another manager can take the combination; resuming re-grabs it;
  registration while suspended is deferred and applied on resume.
- The destructor and `unregisterAll` release every grab, delete the notifier and close the display;
  the test run logs no X protocol errors and, when Valgrind is run, no definite leak from the
  backend.
- On a Wayland session the X11 backend is not selected: `registerHotkey` returns false with a
  portal/CLI reason and no X display is opened; the unit test proves it with `WAYLAND_DISPLAY` set.
  The portal backend itself is the sibling task `02-wayland-globalshortcuts`.
- The public `HotkeyManager` API, its signals, `core/hotkeysequence.*`, `hotkeymanager_win.cpp`,
  `AppController`, and the UI are unchanged; the header diff only moves private members behind
  `Q_OS_*` guards. The Astra build and the 20 shared tests pass, and the X11 hotkey test result on
  Astra (skip/blocked by the Parsec policy, with the log evidence) is recorded honestly; the manual
  `xdotool` check is either performed on a real X11 session or recorded as blocked with the reason.
- Documentation is untouched; the facts listed in step 9 are in the task outcome.

## Risks

- Astra Linux Parsec denies passive key grabs for untrusted binaries: on this host every `XGrabKey`
  returns `BadAccess` on both `:0` and a fresh Xvfb, and the Xvfb log says
  `XPARSEC: keylogger-grabber ... detected`; the same probe succeeds in the `debian:trixie`
  container and `XTestFakeKeyEvent` is allowed on Astra. So the X11 gate must run in trixie, and
  making hotkeys work on Astra needs the security policy to trust the binary (or the desktop-bound
  CLI shortcut fallback). This is a user/security-policy decision and is raised in the report; do
  not work around it in code.
- Xvfb maps `F1`–`F12` only, and the X server reserves `Ctrl+Alt+F1`–`F12` for VT switching; tests
  must stay in the F1-F12/letter space, and registering `Ctrl+Alt+F12` will fail even on a healthy
  server.
- On a non-Latin layout `XKeysymToKeycode(XK_q)` can return 0, so a Latin-letter hotkey is reported
  as unsupported where Windows works by virtual key. Document the limitation; do not guess a
  Cyrillic/second-level keysym.
- Auto-repeat differs from Windows `MOD_NOREPEAT`: suppress repeats with the pressed-key set. XTest
  cannot produce a hold-repeat (it coalesces repeated presses), so hold-the-key behaviour is a
  manual check, not an automated one.
- Xlib defines macros (`None`, `Bool`, `Status`, `KeyPress`) and has process-global error handler
  state. Include Xlib after the Qt headers in the `.cpp`, keep Xlib out of the header, install the
  error handler only around the grab batches, and keep every Xlib call on the manager's thread.
- The header is shared with MSVC and cannot be compiled here. Keep the Windows declarations byte-
  identical inside `#if defined(Q_OS_WIN)`; review the diff, because a mistake breaks the Windows
  build silently.
- The Wayland rule is env-based, so `QT_QPA_PLATFORM=xcb` on a Wayland session still takes the
  Wayland path, as the stage requires ("no X11 display on a Wayland session"). Record this in the
  outcome so the rule is not "fixed" later by accident.
- Two managers deliberately open two display connections; `XGrabKey` is per-client and the conflict
  test depends on it. Do not share one display between managers.
- The Linux test name (`tst_hotkeymanager_x11`) differs from the stage wording "`tst_hotkeymanager`
  on Linux"; it follows architecture §4.14's "Windows platform tests are replaced by Linux
  analogues". Report it so the lead can align the stage text.
- The `registrationFailed` reason is still only logged, and `AppController`'s tray text stays "is
  already used by another program". On Wayland that text is misleading; the user-facing Wayland
  warning belongs to `02-wayland-globalshortcuts` (and task 05 for the CLI fallback), not here.

## Outcome

Done. The Linux tree now has an X11 hotkey backend behind the unchanged `HotkeyManager` API, and the
X11 gate passes in the `debian:trixie` container.

- Files: new `src/platform/hotkeymanager_x11.cpp` and `tests/tst_hotkeymanager_x11.cpp`;
  `CMakeLists.txt` selects the X11 file and links `X11::X11` into `qtranslate` only;
  `src/platform/hotkeymanager.h` moves the Windows-only members behind `Q_OS_WIN` and adds the X11
  state behind `Q_OS_LINUX`; `src/platform/hotkeymanager_stub.cpp` now documents the remaining
  non-Windows platforms; `tests/CMakeLists.txt` adds the Linux test. `qtranslate_core` stays
  X11-free, the Windows block and the Windows test are byte-identical, and the public API, signals,
  `core/hotkeysequence.*`, `hotkeymanager_win.cpp`, `AppController`, and the UI are unchanged.
- Behaviour: the manager owns a dedicated `Display*` and a `QSocketNotifier`; it grabs each
  combination on the root window for the four Lock/Mod2 variants, detects `BadAccess` with a scoped
  Xlib error handler plus `XSync`, ungrabs on unregister, suspend, and destruction, re-grabs on
  resume, and suppresses auto-repeat with a pressed-key set. Sequences are mapped to keysyms and
  keycodes in the platform file (letters, digits, keypad, F1-F24, navigation, punctuation and their
  shifted forms, keypad operators); empty, modifier-only, multi-chord, unknown, and
  layout-unresolvable sequences fail with a non-empty `registrationFailed` reason.
- Wayland: the backend never opens an X display when `WAYLAND_DISPLAY` is non-empty,
  `XDG_SESSION_TYPE` is `wayland`, or the Qt platform name starts with `wayland`; `registerHotkey`
  stores the sequence and returns false with a portal/CLI reason. This includes an XWayland session
  with `QT_QPA_PLATFORM=xcb`, as the stage requires. The env-based rule and the reason are covered by
  the unit test.
- Deviations from the plan text, both deliberate:
  - `XkbSetDetectableAutoRepeat(display, True, nullptr)` is enabled on the manager's connection. The
    plan assumed auto-repeat arrives as repeated `KeyPress`; without XKB it arrives as
    `KeyRelease`/`KeyPress` pairs, which the pressed-key set cannot suppress. Detectable auto-repeat
    is client-local; servers without XKB keep the documented repeat limitation.
  - The Wayland check runs before the suspended check, so on a Wayland session `registerHotkey`
    always returns false with the portal/CLI reason, even while suspended. This follows the stage
    rule "on Wayland `registerHotkey` returns false with a reason" and does not change the X11
    suspend/resume semantics. `registerNative` repeats the Wayland guard so a resume cannot open a
    display.
- Verification:
  - `debian:trixie` (Qt 6.8.2, GCC 14.2), clean Debug and Release builds from the read-only source
    mount: 0 `warning:` lines with `-Wall -Wextra`; `timeout 300 xvfb-run -a ctest` 21/21 in both;
    `-R tst_hotkeymanager_x11` 1/1; the new test reports 16 passed, 0 failed, 0 skipped (14 test
    functions plus init/cleanup), so XTest drives a real activation.
  - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2), native Debug and Release in `build/linux-debug` and
    `build/linux-release`: 0 warnings; CTest 21/21. In the original run `tst_hotkeymanager_x11`
    `QSKIP`ped in `initTestCase` with the Parsec message (`XPARSEC: keylogger-grabber` is in the
    Xvfb log); after the `KBD_R` trusted-client follow-up below it passes 16/16 on both configs. A
    Clang 16 Debug build before the follow-up was clean and showed the same 20 + skip.
  - Valgrind in a trixie container: 0 definitely/indirectly/possibly lost and `ERROR SUMMARY: 0` for
    the new test. On Astra the pre-follow-up run was skipped before the backend ran, so Valgrind only
    confirmed the skip; the trusted-client run was not repeated under Valgrind.
  - Weston headless with a private `XDG_RUNTIME_DIR` and `QT_QPA_PLATFORM=wayland`: the app starts,
    logs the portal/CLI reason, `strace` shows no connect to `/tmp/.X11-unix`, and the Parsec line is
    absent. The full portal path remains task `02-wayland-globalshortcuts`.
  - The manual `xdotool` check on the real Astra X11 session was blocked by Parsec in the original
    run; the `KBD_R` follow-up below performed it successfully on `:0` (Ctrl+W and Ctrl+Q reached
    `AppController`). The XTest test covers the same path up to `activated`.
- Known limitations and deferred items:
  - The `activated` signal is emitted from inside `X11State::readEvents()`. A slot that calls
    `unregisterAll()` or deletes the manager synchronously would free the state under the event
    loop; no caller in the project does that today (`AppController` only starts asynchronous work),
    so the edge is left as is. Closing it safely needs the notifier/X11State lifetime to be
    deferred.
  - In `xtestActivation()` any failed registration is still reported as "combination unavailable"
    and skipped without checking the reason. The F-key mapping is covered by
    `mapsRepresentativeKeys()`, so the risk is a masked regression only for that one combination.
  - One concurrent trixie run (Debug CTest started while Release was building) hit `XOpenDisplay`
    failures in `suspendedRegistrationIsDeferred`; 30 isolated runs of the new test and 3 isolated
    full CTest runs were clean, and an isolated `XOpenDisplay` loop failed in the same environment.
    This is an Xvfb/Xlib environment flake under load, not a backend defect; keep the test run
    isolated in CI.
  - `SettingsDialog::validate` still uses the Win32 `HotkeySequence` helper on Linux. The supported
    key set matches; layout-unresolvable combinations pass dialog validation and fail at
    registration, which the plan allows.
- Facts for the documentation (not edited here): the backend is
  `src/platform/hotkeymanager_x11.cpp` and the test `tests/tst_hotkeymanager_x11.cpp`; Linux has 21
  tests, Windows keeps 23; the build needs `find_package(X11)` (`libx11-dev`, and `libxtst-dev` for
  the test), the runtime needs `libx11-6` (already pulled by Qt's xcb plugin); the Wayland rule is
  env-based; Astra's Parsec policy blocks passive grabs unless the binary carries the `KBD_R`
  `security.exec_id` (see the follow-up); Xvfb maps F1-F12 only and the server
  reserves `Ctrl+Alt+F1-F12`, so the tests use `Ctrl+Alt+Shift+F9/F10/F11`; the stage text says
  `tst_hotkeymanager` on Linux while the actual name is `tst_hotkeymanager_x11` (architecture §4.14
  "Windows platform tests are replaced by Linux analogues").
- Windows Debug/Release and the 23 Windows tests were not performed: this machine has no MSVC or
  Windows toolchain; the header diff and the `if(WIN32)` blocks were reviewed by inspection only.
- Committed locally after review; nothing was pushed.

### Follow-up: `KBD_R` trusted-client exception on Astra (2026-11-06)

The user superseded the earlier "accept the Astra limitation" decision and asked to apply the
trusted-client exception, so the X11 path is now verified on the real Astra host, not only in the
trixie container.

- Host setup: `/etc/X11/trusted.d/qtranslate` lists
  `build/linux-{debug,release}/qtranslate` and `build/linux-{debug,release}/tests/tst_hotkeymanager_x11`
  with the `KBD_R` role; `sudo /usr/bin/fly-set-execid.sh` sets `security.exec_id` to `0x10` (`KBD_R`)
  on each binary.
- Astra Linux 1.8.7, Debug and Release: CTest 21/21, and `tst_hotkeymanager_x11` reports
  16 passed, 0 failed, 0 skipped. The Xvfb log shows `client '<pid>' role=10 by execid` instead of
  `XPARSEC: keylogger-grabber`; the test's `QSKIP` guard stays for untrusted hosts.
- Manual end-to-end on the real X11 session `:0`: `build/linux-release/qtranslate --verbose`
  registers Ctrl+Q (popup), Ctrl+Shift+Q (replace) and Ctrl+W (main window; `showMainWindow` has no
  default); `xdotool key ctrl+w` logs `Hotkey activated: 2`, `xdotool key ctrl+q` logs
  `Hotkey activated: 0` and `Popup shown (nothing selected) in 10 ms`. No `BadAccess`.
- Environment caveat: `security.exec_id` belongs to the build artifact and is lost when the binary is
  rebuilt; re-run `sudo /usr/bin/fly-set-execid.sh` after a rebuild. The `/etc/X11/trusted.d/qtranslate`
  entry is persistent. This is host setup, not repository content: no code, test, or CMake change.
- The verification was made from the task commit `5ef937e`; documentation only.
