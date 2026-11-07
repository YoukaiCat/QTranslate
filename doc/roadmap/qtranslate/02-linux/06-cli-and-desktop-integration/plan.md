---
kind: task
status: done
detailed: true
depends: [02-global-hotkeys, 03-x11-selection-and-inplace, 04-x11-hotkey-grab-release, 05-xdg-autostart-and-secrets]
---

# CLI commands and Linux desktop integration

## Task

Add `--translate-popup`, `--translate-main`, and `--translate-inplace` as the portable way to
trigger the three scenarios, forward them to the running instance, and make the Linux desktop
behaviour explicit: tray availability, Wayland limitations, and the in-place clipboard fallback.
The CLI commands are platform-neutral and also work on Windows; the Linux-specific warnings are
only shown there.

The commands cover Wayland and desktops where a global hotkey cannot be bound: the user selects or
copies text, binds the command in the desktop's shortcut settings, and the running instance executes
it against the current active window. If no instance is running, the command starts the application,
executes the scenario once, and leaves it in the tray. A secondary instance must forward the command
over the existing single-instance socket and exit 0.

Wayland scope is unchanged: `GlobalShortcuts` portal where available, Ctrl+Q with manual copying,
CLI commands, and explicit warnings. Do not add `ydotool`, `wtype`, RemoteDesktop portal, or any
other synthetic input, and do not expand the stage.

The `.desktop` file, hicolor icons, and `share/applications` installation stay in
`07-packaging-and-ci`; this task defines the CLI contract and the runtime desktop behaviour that the
packaging reuses. The CLI options and the single-instance protocol are shared code, so they must
also compile on Windows; the Linux-only warnings are guarded in `src/app/appcontroller.cpp`.

## Steps

1. Extend `SingleInstance` with the command set and keep the wire format backward compatible.
   - `src/app/singleinstance.h:21-24`: extend the enum to
     `enum class Command { None, Activate, TranslatePopup, TranslateMain, TranslateInplace };`
     and replace the `activateRequested()` signal with
     `void commandRequested(Command command);`. `tryBecomePrimary` and `sendToPrimary` keep their
     signatures and their 200/500/1000 ms timeouts.
   - `src/app/singleinstance.cpp:21-22,80-120`: keep `activate\n` and `noop\n` exactly as they are;
     add `translate-popup\n`, `translate-main\n`, and `translate-inplace\n`. Put the enum-to-line and
     line-to-enum mapping in two small helpers (`messageFor`/`commandForMessage`) so
     `sendToPrimary` and `onNewConnection` share one table. `noop` and unknown lines stay silently
     ignored; `activate` now emits `commandRequested(Command::Activate)`.
   - Keep `Platform::allowAnyProcessToActivate()` only for `Command::Activate` in `sendToPrimary`
     (`singleinstance.cpp:84-85`); a translate command must never take the foreground from the
     application the user is translating in.
   - Log the received command once at info level (`qCInfo(lcApp) << "Received command" << ...`) for
     the scripted check. Do not log selection or translated text.
   - Register the enum as a metatype: add `Q_DECLARE_METATYPE(SingleInstance::Command)` after the
     class in `singleinstance.h` and call
     `qRegisterMetaType<SingleInstance::Command>("SingleInstance::Command");` in the constructor,
     like `HotkeyManager` does for `HotkeyAction` (`hotkeymanager.h:22`,
     `hotkeymanager_linux.cpp:1426`). `QSignalSpy` needs the registered type.
   - Do not add a test-only environment hook to `defaultKey()`; the new test constructs
     `SingleInstance` with its own temporary key.

2. Extract the command-line definition into a testable helper.
   - Create `src/app/commandline.h`/`.cpp` with a `CommandLineOptions` struct holding the
     `QCommandLineOption` objects for `--minimized`, `--verbose`, `--translate-popup`,
     `--translate-main`, and `--translate-inplace`; provide `addTo(QCommandLineParser &)`,
     `scenario(const QCommandLineParser &, QString *error)`, and readers for the two existing
     booleans.
   - Build the option descriptions with `QCoreApplication::translate("main", ...)`, not
     `QApplication::translate`, so a guiless test can construct the struct after
     `QCoreApplication`. Do not construct the options at static-initialization time.
   - `scenario()` counts the set scenario options: more than one sets `*error` to the translated
     mutual-exclusion message and returns `None`; otherwise the set option maps to its command.
   - Positional arguments stay accepted and ignored, so a desktop entry with a stray `%U`/`%F` does
     not break the command. Unknown options keep the standard `QCommandLineParser` error and exit
     code 1.
   - Add `src/app/commandline.cpp` and `.h` to the `qtranslate` target in `CMakeLists.txt:71-89`.

3. Parse the options in `src/main.cpp` and forward or execute the command.
   - Replace the inline option block at `src/main.cpp:39-54` with `CommandLineOptions`; keep
     `--help`, `--version`, and the application description. `parser.process(app)` keeps Qt's clear
     error for unknown options and its exit code 1; after it, call `options.scenario(parser, &error)`
     and on an error call `parser.showError(error)` plus `return 1`.
   - Read `startHidden = parser.isSet(options.minimized)` and
     `Logging::initialize(parser.isSet(options.verbose))` as today.
   - Choose the forwarded command before `tryBecomePrimary`: the scenario option if set, otherwise
     `startHidden ? Command::None : Command::Activate`. A secondary exits 0 after forwarding, before
     `SettingsStore` and `AppController` are created (`src/main.cpp:56-60`).
   - Pass both values to `AppController::Options{startHidden, command}`; only the process that
     became primary executes the command.

4. Execute forwarded and startup commands in `AppController`.
   - `src/app/appcontroller.h`: include `app/singleinstance.h`; add
     `SingleInstance::Command command = SingleInstance::Command::None;` to `Options` and a private
     `void executeCommand(SingleInstance::Command command);`.
   - `src/app/appcontroller.cpp:131`: connect `SingleInstance::commandRequested` to
     `executeCommand` instead of connecting `activateRequested` to `showMainWindow`.
   - `executeCommand` maps `Activate` to `showMainWindow()` and the three translate commands to the
     existing `translateSelectionInPopup()`, `translateSelectionInMainWindow()`, and
     `translateSelectionInPlace()`; `None` does nothing. Reusing the hotkey paths keeps the
     `m_grabber->isBusy()` guards (`appcontroller.cpp:663-668, 708-713, 759-764`), so a duplicate
     command arriving during a grab is dropped instead of starting a second operation.
   - Only `Activate` may call `showMainWindow()`/`activateWindow()`/`allowAnyProcessToActivate()`.
     The translate commands must not activate a QTranslate window before the grab; in-place already
     captures `Platform::foregroundWindow()` before the grab and refuses to paste when the window
     changed (`appcontroller.cpp:775-826`).

5. Implement the startup behaviour with and without a tray.
   - In `start()` (`src/app/appcontroller.cpp:74-141`), compute `hasStartupCommand` and do not call
     `showMainWindow()` at startup when it is true. Call `executeCommand(m_options.command)` once at
     the end of `start()`, after the tray, hotkeys, autostart, and signal wiring, so the source
     application is still the foreground window when the grab starts.
   - With a tray: the process stays in the tray after the command (`setQuitOnLastWindowClosed(false)`
     is already set in `main.cpp:37`); `--translate-main` shows the main window from its grab
     callback, while the popup and in-place commands do not show it at startup.
   - Without a tray (`m_trayAvailable == false`), never show the main window before the grab:
     `--translate-main` keeps the existing callback; `--translate-popup` calls `showMainWindow()` in
     both branches of its grab callback (empty and non-empty text) after the text was copied and
     before `popup->showLoading()`/`showNoSelection()`, so the popup still receives the focus and a
     window remains when the popup closes; `--translate-inplace` shows the main window only after
     the in-place operation ends (the no-selection message or the reply handling after the paste or
     the clipboard fallback), so `Platform::isForegroundWindow(m_inPlaceTarget)` is not invalidated.
   - A normal start without a tray keeps `showMainWindow()` (`appcontroller.cpp:138-140`) and adds
     the one-time tray warning from step 7. A command start without a tray uses the scenario's own
     window or message box as its feedback; do not show the tray warning before the grab.

6. Show one Wayland warning and stop repeating it per hotkey.
   - At the top of `start()`, set `m_waylandSession` (add the member to `appcontroller.h`): include
     `platform/sessiontype.h` under `#ifdef Q_OS_LINUX` and call `Platform::isWaylandSession()`;
     `false` on other platforms.
   - After the tray exists, show exactly one notification when `m_waylandSession` is true:
     title `Limited Wayland support`, body
     `Global hotkeys require the GlobalShortcuts portal. If this desktop does not provide it, copy the selected text with Ctrl+C and use the CLI commands or a desktop shortcut; in-place replacement copies the translation to the clipboard instead of pasting it.`
     Use `m_tray->showMessage(...)` when a tray exists, otherwise a non-modal `QMessageBox`
     (`open()`, `WA_DeleteOnClose`) parented to the main window.
   - In the `registrationFailed` handler (`appcontroller.cpp:212-223`), when `m_waylandSession` is
     true keep the `qCWarning` and return without a tray message: the desktop may reject all four
     shortcuts because the portal is missing, and the single startup warning is the user-facing
     message. On X11 keep the existing `Hotkey unavailable` tray message.
   - Do not add synthetic input or a second portal; the existing
     `hotkeymanager_linux.cpp:1420-1423, 1494-1499` behavior is unchanged.

7. Make the missing tray and the in-place messages visible.
   - Add a private `void showTrayUnavailableWarning();` that displays one non-modal message box with
     title `System tray unavailable` and body
     `The system tray is not available, so the application cannot stay in the background. Closing the window quits the application.`
     Call it once from the normal start path after `showMainWindow()` when `m_trayAvailable` is
     false. Keep the GNOME/AppIndicator advice out of this UI string; the lead documents it.
   - Change `showInPlaceMessage()` (`appcontroller.cpp:875-880`): keep the tray balloon when
     `m_tray` exists; otherwise show the same message in a non-modal `QMessageBox` with the existing
     `Translate and replace` title. This makes "nothing is selected", paste failures, and the
     clipboard fallback visible without a tray, and adds no new title string.
   - Use `open()` with `WA_DeleteOnClose`; never a nested `exec()` loop, because a modal dialog
     before the grab or the paste would steal the foreground and break the scenario.

8. Add the Linux test for parsing and two-process forwarding.
   - Create `tests/tst_cli.cpp`, a guiless Linux-only test registered in the Linux block of
     `tests/CMakeLists.txt:78-151`. Compile `src/app/commandline.cpp`, `src/app/singleinstance.cpp`,
     `src/app/logging.cpp`, and `src/platform/windowactivation_linux.cpp` into the target; link
     `qtranslate_core`, `Qt::Test`, `X11::X11`, and `X11::Xtst`. No display is needed.
   - Parser tests: defaults; each scenario option; each scenario combined with `--minimized` and
     `--verbose`; two scenario options produce the mutual-exclusion error; `parser.parse({"--bogus"})`
     returns false and `parser.errorText()` contains `Unknown option` (do not call `process()`, it
     exits); a positional argument is accepted and ignored.
   - Forwarding test (two processes, temporary key): the parent creates
     `SingleInstance` with a unique key such as `qtranslate-test-<pid>-<uuid>` and becomes primary;
     a custom `main` creates the `QCoreApplication` first, then checks for a `--child` mode before
     `QTest::qExec`, parses the remaining arguments with the same `CommandLineOptions` code, calls
     `tryBecomePrimary`, and exits 0 when the command was forwarded. Spawn the child with `QProcess` and the key in
     `QProcessEnvironment`; assert exit code 0 and that a `QSignalSpy` on
     `commandRequested` receives exactly the expected command. Cover `--translate-popup`,
     `--translate-main`, `--translate-inplace`, and `--minimized` (noop, no signal).
   - Legacy wire test: connect a raw `QLocalSocket` to the primary, write `activate\n` and `noop\n`,
     and assert that only `Activate` is emitted. This keeps the old secondary instances compatible.
   - Keep the `QSignalSpy` and the child process alive across the wait and use `QSignalSpy::wait()`
     or `QTRY_*` so the parent's event loop runs `QLocalServer::newConnection`; do not block in
     `QProcess::waitForFinished()` before the signal was delivered. The child must be started after
     the parent's spy is armed, and the parent must kill it on a timeout.
   - The child must never become primary: return a distinct non-zero code if
     `tryBecomePrimary` returns true, and make the parent kill and report the child on failure.
     Linux test count becomes 28; Windows stays 23 (the test is not added to the Windows block).

9. Update all translations for the new user-visible strings.
   - Source strings and contexts:
     - `main`: `Translate the selected text in a popup window.`,
       `Translate the selected text in the main window.`,
       `Translate the selected text and replace it.`, and the mutual-exclusion error
       `The --translate-popup, --translate-main and --translate-inplace options are mutually exclusive.`
     - `AppController`: `Limited Wayland support`, the Wayland body from step 6,
       `System tray unavailable`, and the tray body from step 7.
   - Regenerate all 14 files in `QTRANSLATE_TS_FILES` (`CMakeLists.txt:146-161`) with `lupdate` over
     `src` and `--no-obsolete`, or with the `update_translations` target. Translate every new message,
     remove `type="unfinished"`, and do not add or remove leading/trailing spaces. `tst_translations`
     is the gate: it fails on any unfinished or empty message and also checks placeholders and
     symbols.
   - Do not edit `README.md`, `README.ru.md`, `doc/architecture.md`, `AGENTS.md`, `doc/handoff.md`, or
     `i18n/README.md`; report the source strings and contexts to the lead.

10. Build and verify on Debian 13 (trixie) and Astra Linux; review Windows by diff.
    - `debian:trixie` (Qt 6.8.2, GCC 14.2): Debug and Release with `-Wall -Wextra`, no `warning:`
      lines; `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure` is 28/28.
      Run `tst_cli`, `tst_translations`, and the existing platform tests individually and record
      the counts; the total is 28.
    - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2): native Debug and Release, 0 warnings, 28/28; run
      `sudo /usr/bin/fly-set-execid.sh` after every rebuild so the X11 tests keep their trusted
      client attribute, and record the unchanged 16/16, 20/20, 12/12, 13/13, and 7/7 counts.
    - Manual X11 check on `:0` (and a scripted Xvfb check where possible): with no instance,
      `--translate-popup` shows the popup with the selection, `--translate-main` fills the main
      window, and `--translate-inplace` replaces or copies; with the app running, each command is
      forwarded and the second process exits 0; `--minimized --translate-popup` stays in the tray;
      `--verbose` logs the received command; `--translate-popup --translate-main` and an unknown
      option fail with a clear message and a non-zero exit; Ctrl+Q/Ctrl+W/Ctrl+Shift+Q behave as
      before. Use a temporary `USERNAME`/`USER` and a temporary `XDG_CONFIG_HOME` so the check does not
      interfere with a real running instance.
    - Wayland: Weston headless or `QT_QPA_PLATFORM=offscreen` with `WAYLAND_DISPLAY` set shows one
      warning, no per-hotkey message storm, no X11 display, and the in-place clipboard fallback.
      A real `GlobalShortcuts` portal remains not performed unless a portal-capable desktop is
      available; record the mock or the skip with the reason.
    - Windows: do not build (no MSVC). Review by `git diff` that the Windows mutex,
      `windowactivation_win.cpp`, the `if(WIN32)` CMake/test blocks, and the shared `SingleInstance`
      protocol are unchanged except for the new enum values and messages; record the Windows 23
      tests as not performed.
    - `git diff --check`; confirm only this task's source, tests, CMake, and `.ts` files changed;
      fill `## Outcome` and commit the task per the coordinator flow; the lead keeps the other map
      files and the documentation.

## Done when

- `main.cpp` parses `--translate-popup`, `--translate-main`, and `--translate-inplace`; they are
  mutually exclusive and compatible with `--minimized` and `--verbose`; `--help` lists them;
  unknown options exit non-zero with the standard clear message; positional arguments are ignored.
- `SingleInstance::Command` carries the three commands over the local socket as
  `translate-popup\n`, `translate-main\n`, and `translate-inplace\n`; the old `activate\n` and
  `noop\n` lines still work; the secondary exits 0 after forwarding; the primary emits
  `commandRequested` once and executes the scenario once against the current active window; a busy
  grabber drops a duplicate command.
- Starting a scenario option with no running instance does not show the main window at startup when
  a tray is available; the scenario runs and the process stays in the tray. Without a tray the
  scenario still runs: `--translate-main` shows the main window, `--translate-popup` shows the
  popup and leaves the main window visible after the grab, and `--translate-inplace` shows its
  messages in a message box and shows the main window only after the in-place reply. No window is
  activated before the selection grab or the paste, and the process does not run invisibly.
- The no-tray warning appears once on a normal start without a tray, says that closing the window
  quits, and is not shown before a command start's grab. With a tray, a command start stays in the
  tray.
- Wayland: one startup warning states that global hotkeys need the `GlobalShortcuts` portal, that
  the selection must be copied manually when the portal is unavailable, and that in-place puts the
  translation on the clipboard instead of pasting; the warning is not repeated for every failed
  hotkey; no synthetic input is attempted. The existing portal behavior and the X11 hotkey paths
  are unchanged.
- `tst_cli` covers the parser and the two-process forwarding with a temporary lock/socket key; the
  legacy `activate`/`noop` wire test passes. Linux has 28 CTest tests and the 20 shared tests still
  pass; `tst_translations` passes with the new strings in all 14 `.ts` files; Debug and Release
  build without `-Wall -Wextra` warnings in `debian:trixie` and on Astra Linux; Astra is checked
  after `sudo /usr/bin/fly-set-execid.sh`.
- The `.desktop` file, hicolor icons, and `share/applications` are not changed here (they belong to
  `07-packaging-and-ci`); no `ydotool`/`wtype`/RemoteDesktop portal or other synthetic input is
  added; the Wayland scope stays portal + manual copy + CLI + warnings.
- Windows Debug/Release and the 23 Windows tests are recorded as not performed (no MSVC), not as
  passed; the diff review confirms that the Windows-specific code and `if(WIN32)` blocks are
  unchanged apart from the shared command enum and parser.

## Risks

- `QCommandLineParser::process()` treats unknown options as errors and exits 1; keep it for the
  standard message. The parser tests must use `parse()` and `errorText()` instead of `process()`,
  which would end the test process. A desktop shortcut with extra positional arguments must not
  break: positional arguments are ignored.
- The single-instance protocol is a line-based local socket. Keep `activate` and `noop` byte-exact
  and test the old and new lines; an unknown line from a future version must stay ignored. The
  temporary test key must be unique to avoid clashing with a real running instance, and the child
  process must be killed if the test fails.
- A command-started instance must not steal focus from the application the user is translating in:
  never call `showMainWindow()`, `activateWindow()`, or `Platform::allowAnyProcessToActivate()`
  before the grab; `showMainWindow()` for the no-tray popup case belongs in the grab callback, and
  for in-place only after the reply. `MainWindow::showAndActivate()` calls `activateWindow()`
  (`src/ui/mainwindow.cpp:141-148`), and the popup closes on focus loss
  (`src/ui/popupwindow.cpp:499-527`), so an early window would break the scenario.
- Message boxes must be non-modal (`open()` + `WA_DeleteOnClose`): a nested loop before the grab
  would move the focus, and a modal box would delay the startup command. The Wayland warning is
  safe because that session uses the manual clipboard, but it must still not repeat.
- The tray warning on a command start is deliberately omitted before the grab; the scenario's own
  window or message box is the feedback. Do not add a second warning path that activates a window.
- The new UI strings are in the `main` and `AppController` contexts; `tst_translations` fails on
  any unfinished or empty message, so all 14 files must be regenerated and translated. The Wayland
  and tray messages have no placeholders; the mutual-exclusion message contains none either.
- `tst_cli` compiles app sources into a test for the first time. Keep it guiless, link
  `windowactivation_linux.cpp` for `allowAnyProcessToActivate`, and do not open an X display; the
  test must not depend on a session tray or the user's settings.
- Linux is 28 tests after this task; Windows remains 23. Windows cannot be built here: review the
  shared `main.cpp`/`SingleInstance` changes by diff and record the Windows tests as not performed.
- The X11 manual checks need the trusted-client attribute on Astra after every rebuild; if the real
  `:0` session is unavailable, use Xvfb and record the real-session check as not performed.

## Outcome

Done. The three CLI commands are parsed by the shared `CommandLineOptions`, forwarded over the
single-instance socket with the old lines unchanged, and executed by `AppController` through the
existing hotkey paths. Linux has 28 tests; Windows keeps 23.

- Files: new `src/app/commandline.{h,cpp}` and `tests/tst_cli.cpp`; modified `src/main.cpp`,
  `src/app/singleinstance.{h,cpp}`, `src/app/appcontroller.{h,cpp}`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`, and all 14 `i18n/qtranslate_*.ts`. The `.desktop` file, hicolor icons,
  `share/applications`, dependencies, READMEs, architecture, `AGENTS.md`, the handoff, the map
  files and `i18n/README.md` are unchanged.
- CLI: `--translate-popup`, `--translate-main` and `--translate-inplace` are mutually exclusive;
  each combines with `--minimized` and `--verbose`; `--help` lists them; positional arguments are
  accepted and ignored (a stray `%U`/`%F` in a desktop entry does not break the command); unknown
  options keep the `QCommandLineParser` message and exit code 1. `QCommandLineParser` has no public
  `showError()` in Qt 6, so the mutual-exclusion error is printed as `QTranslate: <message>` with
  exit 1, the same format `process()` uses.
- Protocol: `SingleInstance::Command` carries the three commands as `translate-popup\n`,
  `translate-main\n` and `translate-inplace\n`; `activate\n` and `noop\n` are byte-exact, unknown
  lines are ignored, the enum is a registered metatype, and the primary logs `Received command` once
  at info level (never selection or translated text). A secondary forwards and exits 0 before
  `SettingsStore`/`AppController` are created. `Platform::allowAnyProcessToActivate()` is still
  called only for `Activate`.
- Startup: only the three translate commands suppress the main window. A plain start keeps the old
  path, including `--minimized` and `settings.general.startMinimized`; counting the default
  `Activate` as a startup command would have bypassed both. With a tray the process stays in the
  tray after the command. Without a tray, `--translate-main` shows the window from its grab callback,
  `--translate-popup` shows the main window in both grab branches before the popup, and
  `--translate-inplace` shows it only after the operation ends (message, error, clipboard fallback or
  paste), so `Platform::isForegroundWindow(m_inPlaceTarget)` stays valid. No window is activated
  before the grab or the paste; a duplicate command during a grab is dropped by the existing
  `isBusy()` guards.
- Warnings: one `Limited Wayland support` notification after the tray exists (tray balloon with a
  tray, non-modal message box without it); `registrationFailed` keeps the `qCWarning` but suppresses
  the per-hotkey tray message on Wayland, while X11 keeps `Hotkey unavailable`. The no-tray normal
  start shows `System tray unavailable` once after the main window and is not shown on a command
  start before the grab. `showInPlaceMessage()` uses a non-modal `QMessageBox` when there is no tray
  and keeps the `Translate and replace` title. All new boxes use `open()` + `WA_DeleteOnClose`, never
  a nested `exec()`.
- Tests: `tst_cli` is a guiless Linux-only test with 20 cases: defaults, each scenario, each
  scenario with `--minimized` and `--verbose`, mutual exclusion, `parse({"--bogus"})` with
  `errorText()` (no `process()`), positional arguments, two-process forwarding with a unique
  `qtranslate-test-<pid>-<uuid>` key and `QTRANSLATE_TEST_KEY` (child exits 0, exits 3 if it becomes
  primary, and is killed on timeout), and a raw `QLocalSocket` that checks `activate\n`,
  `translate-popup\n`, `translate-main\n` and `translate-inplace\n` byte-exactly plus silent
  `noop\n` and `future-command\n`. Linux is 28 CTest tests; Windows keeps 23.
- Verification: `debian:trixie` (Qt 6.8.2, GCC 14.2) Debug and Release 28/28 with
  `timeout 300 xvfb-run -a ctest`, 0 `warning:`/`error:` lines; Astra Linux 1.8.7 (Qt 6.10.2,
  GCC 12.2) Debug and Release 28/28, 0 warnings, with `sudo /usr/bin/fly-set-execid.sh` after each
  rebuild. Counts: `tst_cli` 20, `tst_translations` 4, `tst_hotkeymanager_x11` 16/16,
  `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12,
  `tst_windowactivation_linux` 13/13, `tst_hotkey_selection_x11` 7/7, `tst_autostart` 6/6,
  `tst_secretprotector` 7 + 1 skipped in trixie (no session keyring) and 8/8 on Astra, and the 20
  shared tests pass. `tst_cli` passed 10/10 repeated runs in trixie and Astra; no leftover processes
  or `qtranslate-test-*` sockets.
- Manual checks: on the real `:0` with temporary `USER`/`USERNAME`, `XDG_CONFIG_HOME`,
  `XDG_STATE_HOME` and `XDG_RUNTIME_DIR`, the three commands work without and with a running
  instance; the secondary exits 0 and the primary logs exactly three `Received command` lines;
  `--minimized --translate-popup` stays in the tray; the two-option and `--bogus` invocations exit 1
  with a clear message; `--help` lists the options; Ctrl+Q, Ctrl+W and Ctrl+Shift+Q are unchanged.
  Without a tray (a temporary runtime without D-Bus), the warning appears once, the popup leaves the
  main window visible, and in-place shows its window only after a delayed answer. Wayland was checked
  on Weston headless (no X11 file descriptors, xcb not loaded) and with `WAYLAND_DISPLAY` + xcb (one
  `Limited Wayland support` dialog, no hotkey dialogs, in-place clipboard fallback). A real
  `GlobalShortcuts` portal was not performed.
- Translations: eight new messages in the `main` context (three option descriptions and the
  mutual-exclusion error) and the `AppController` context (`Limited Wayland support`, its body,
  `System tray unavailable`, its body) were added through `lupdate -no-obsolete` and translated in
  all 14 `.ts` files, with no `type="unfinished"`, empty translations, changed placeholders or
  leading/trailing spaces. `lupdate` re-escaped apostrophes as `&apos;` in a few existing fr/it/uk
  translations; the XML semantics are unchanged.
- Review: accepted and fixed — the raw wire test covered only the old lines, so a symmetric typo in
  the new mapping would have passed the round-trip; `legacyWireFormat` now sends the three new lines
  byte-exactly (a mutant test with `translate_popup\n` fails it) and checks silent `noop` and an
  unknown line. Rejected: none. Known verification caveat: the new X11 counters need the `KBD_R`
  `security.exec_id` on `tst_hotkeymanager_x11`/`tst_hotkey_selection_x11`; the
  `/etc/X11/trusted.d/qtranslate` entries cover `build/linux-debug` and `build/linux-release`, so a
  fresh build tree `QSKIP`s those two tests until `fly-set-execid.sh`/`setfattr` is applied. A full
  CTest on the live `:0` fails two focus tests by design (the test headers forbid the real session);
  the gate is `xvfb-run`.
- Not performed: Windows Debug/Release and the 23 Windows tests (no MSVC; the diff review confirms
  that the mutex, `windowactivation_win.cpp`, installer and `if(WIN32)` blocks are unchanged and the
  shared changes are platform-neutral); the real `GlobalShortcuts` portal (no portal-capable
  desktop); the minimum Qt 6.5 build (the gates used Qt 6.8.2 and 6.10.2).