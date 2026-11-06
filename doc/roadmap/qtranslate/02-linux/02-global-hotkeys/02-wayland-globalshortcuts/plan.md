---
kind: task
status: in-progress
detailed: true
depends: [01-x11-xgrabkey]
---

# Wayland GlobalShortcuts portal

## Task

Add the Wayland backend for `HotkeyManager` through the
`org.freedesktop.portal.GlobalShortcuts` portal. Register the four `HotkeyAction` values with stable
shortcut IDs and user-visible descriptions, create a portal session, bind the shortcuts, map
`Activated` to `HotkeyManager::activated`, and handle `Deactivated` and `ShortcutsChanged` without
changing the public API. Close the session and the DBus connections on destruction.

This is the Wayland scope the user approved: the portal where the desktop provides it, Ctrl+Q with
manual copying, the CLI commands as the fallback, and explicit warnings. Do not add `ydotool`,
`wtype`, the RemoteDesktop portal, XTest, or any other synthetic input. Do not open an X11 display on
a Wayland session. If the portal is missing, the session or bind fails, or the desktop version is
too old, `registerHotkey` must return false with a clear reason so the UI can show one warning and
rely on CLI commands bound in the desktop settings.

## Steps

1. Rename the Linux backend and wire the build.
   - `git mv src/platform/hotkeymanager_x11.cpp src/platform/hotkeymanager_linux.cpp`. The renamed
     file keeps the X11 code and `X11State` and becomes the single translation unit that owns every
     `HotkeyManager` method and dispatches between X11 and the portal. The old plan already names
     this file (`legacy-plan.md`), and the X11 task explicitly allowed the rename.
   - `CMakeLists.txt`: in the existing Linux block add
     `find_package(Qt6 REQUIRED COMPONENTS DBus)` and `Qt6::DBus` to
     `target_link_libraries(qtranslate PRIVATE ...)`; replace
     `src/platform/hotkeymanager_x11.cpp` with `src/platform/hotkeymanager_linux.cpp`. Do not add
     `DBus` to the top-level `find_package` (Windows has no QtDBus) and do not link it into
     `qtranslate_core`.
   - `packaging/linux/trixie.Dockerfile`: add `dbus` under the test tools (it provides
     `dbus-daemon` for the mock portal test); `qt6-base-dev` already provides the QtDBus headers.
     Keep the existing task 06 packaging note.
   - Update the stale `hotkeymanager_x11.cpp` references in the header comment and
     `tests/CMakeLists.txt` to the new name.

2. Extend the private Linux seam in `src/platform/hotkeymanager.h` without touching the public API.
   - Update the class comment: `hotkeymanager_win.cpp` on Windows, `hotkeymanager_linux.cpp` on Linux
     (X11 and the `GlobalShortcuts` portal).
   - In the `#elif defined(Q_OS_LINUX)` branch add `struct WaylandState;`,
     `std::unique_ptr<WaylandState> m_wayland;`, `bool m_waylandSession = false;`, and
     `bool desktopAssigned = false;` to `Registration`. Keep the `Q_OS_WIN` block byte-identical and
     the public methods, signals, `HotkeyAction`, `Registration::sequence`, and
     `Registration::registered` unchanged. No QtDBus or D-Bus type may appear in the header;
     `WaylandState` is defined in the `.cpp` and the destructor stays out-of-line.
   - Set `m_waylandSession = isWaylandSession()` in the constructor and keep `isWaylandSession()` in
     the anonymous namespace. Decide the backend once per manager so a changing environment cannot
     mix an X11 display with a portal session. Keep the X11 task's ordering: on Wayland
     `registerHotkey` reports the portal problem before the suspended check.
   - `hotkey()` returns an empty sequence when `desktopAssigned` is set and the stored sequence
     otherwise; the X11 path and Windows leave the flag false.

3. Check the portal and create the session.
   - Define `WaylandState` in `hotkeymanager_linux.cpp` as a `QObject` with `Q_OBJECT` and private
     slots. `HotkeyManager` owns it through `m_wayland` (no QObject parent) and deletes it in
     `unregisterAll`/the destructor. Add `#include "hotkeymanager_linux.moc"` at the end of the
     file.
   - `ensurePortal(QString *reason)`: take `QDBusConnection::sessionBus()`; if
     `!bus.isConnected()`, the reason says that no D-Bus session bus and therefore no
     `GlobalShortcuts` portal is available and names the CLI/desktop-shortcut fallback; if
     `bus.interface()->isServiceRegistered("org.freedesktop.portal.Desktop")` is false, the reason
     names the missing portal and the fallback; otherwise call
     `org.freedesktop.DBus.Properties.Get` for `org.freedesktop.portal.GlobalShortcuts` / `version`
     and require `version >= 1`. A missing property, a failed call, or version 0 means the interface
     is absent or too old and the reason says so. Cache the outcome in `WaylandState` so later calls
     do not repeat the round trips. Log the first failure at warning level and never fall back to
     X11.
   - Use explicit `QDBusMessage` calls through `QDBusConnection::call`/`asyncCall` with timeouts
     (3 s for availability and `CreateSession`, 1 s for `Session.Close`); do not use
     `QDBusInterface` introspection on the GUI thread. All D-Bus work stays on the manager's thread.
   - Register the `a(sa{sv})` type once: define
     `struct PortalShortcut { QString id; QVariantMap options; };`, `Q_DECLARE_METATYPE(PortalShortcut)`,
     the `QDBusArgument` stream operators for `(sa{sv})`, `Q_DECLARE_METATYPE(QList<PortalShortcut>)`,
     and `qDBusRegisterMetaType<QList<PortalShortcut>>()` in a function-local static. Use this type
     for the `BindShortcuts` request, its response, and `ShortcutsChanged`.
   - `createSession(QString *reason)`: generate valid object-path tokens with
     `QUuid::createUuid().toString(QUuid::Id128)` (letters and digits only), for example
     `qtranslate_<id>_create` and `qtranslate_<id>_session`. Compute the request path
     `/org/freedesktop/portal/desktop/request/<sender>/<handle_token>`, where `<sender>` is
     `baseService()` without the leading `:` and with `.` replaced by `_`. Subscribe to
     `org.freedesktop.portal.Request::Response` on that path *before* calling `CreateSession`
     (`a{sv}` with `handle_token` and `session_handle_token`, output `o`). Wait for `Response` in a
     `QEventLoop` with a 3 s `QTimer`: response 0 is success, 1 is cancelled, 2 is another error.
     Read `session_handle` from the results as a `QString` (the portal spec says it is a string, not
     an object path) and use it as the authoritative session path. A timeout, cancellation, or error
     fails with a reason naming the desktop/session. Connect `Activated`, `Deactivated`, and
     `ShortcutsChanged` for this session only after a successful creation.

4. Map actions to stable IDs, descriptions, and preferred triggers.
   - Keep one table in the anonymous namespace: `translate-popup` / "Translate selected text in a
     popup window" / `Ctrl+Q`; `translate-replace` / "Translate selected text and replace it" /
     `Ctrl+Shift+Q`; `translate-main-window` / "Translate selected text in the main window" /
     `Ctrl+W`; `show-main-window` / "Show the main window" / no default. IDs are ASCII and stable
     across releases. Descriptions are `QStringLiteral` English; do not use `tr()` here because the
     i18n gate requires all 14 `.ts` files to be filled (`tst_translations`), which is out of this
     task's scope (see Risks).
   - Convert the stored `QKeySequence` to the XDG shortcuts-spec `preferred_trigger` string:
     modifiers `CTRL`, `ALT`, `SHIFT`, `LOGO` in that order, then the xkbcommon keysym name from
     `XKeysymToString`. One chord only; `ControlModifier`→`CTRL`, `AltModifier`→`ALT`,
     `ShiftModifier`→`SHIFT`, `MetaModifier`→`LOGO`, `GroupSwitchModifier`→`CTRL+ALT`; keypad keys
     keep their `KP_*` keysym; shifted punctuation uses the base keysym plus `SHIFT`, exactly like
     the X11 table. Reuse `mapSequence` for validation and the keysym. Empty, modifier-only,
     multi-chord, and unsupported sequences fail before any D-Bus call with the same reason style as
     X11. Omit `preferred_trigger` when the action has no sequence.
   - `parent_window` is the empty string: the tray application has no stable XDG window identifier
     and the portal permits an unparented dialog. Log this choice.

5. Bind the shortcuts in one batched request.
   - The portal allows an application to attempt `BindShortcuts` only once per session, while
     `AppController` calls `registerHotkey` four times synchronously at startup. Keep one session per
     manager and collect the shortcuts: `registerHotkey` adds or updates the pending entry and arms a
     zero-timer (`QTimer::singleShot(0, this, &WaylandState::bindPending)`); the timer sends exactly
     one `BindShortcuts(session_handle, shortcuts, "", options)` with every remembered sequence, its
     ID, description, and `preferred_trigger`, plus a fresh `handle_token`. Do not send four calls.
   - `registerHotkey` returns false for a missing portal, a missing or old interface, or a failed
     `CreateSession` with the cached reason, and returns true once the session exists and the
     shortcut is queued. The `BindShortcuts` reply is asynchronous because the portal can show a
     confirmation/configuration dialog; subscribe to `Request::Response` before the call and wait up
     to 60 s for the result. A non-zero response, a timeout, or a shortcut missing from the returned
     list emits `registrationFailed(action, sequence, reason)` for each affected action, leaves
     `registered=false`, and logs the D-Bus error; the application keeps running. This is the only
     failure mode `registerHotkey` cannot return synchronously; document it in the code and the
     outcome.
   - Parse `results["shortcuts"]` as `QList<PortalShortcut>`: mark only the returned IDs registered,
     keep their `description` and `trigger_description` for the log, and set `desktopAssigned=true`
     for a bound ID because the desktop has assigned the trigger, so `hotkey()` becomes empty. Never
     derive a `QKeySequence` from `trigger_description`: it is human-readable and locale-dependent
     and may differ from the preference.
   - Repeating `registerHotkey` with a changed sequence for an already bound action cannot reuse the
     session. Close the session, create a new one, and bind the full current set in one
     `BindShortcuts`; use the same zero-timer so the unregister+register pair from
     `AppController::applySettings` coalesces into one new session. A same-sequence repeat is a no-op
     when the action is already bound. `unregisterHotkey` removes the entry and recreates the session
     with the remaining shortcuts, or only closes it when none remain. A failed bind must not leave
     the old session's shortcuts marked registered.

6. Handle the portal signals.
   - `Activated(session_handle o, shortcut_id s, timestamp t, options a{sv})`: ignore a signal whose
     session handle is not the current one, an unknown ID, an action that is not registered, and
     every activation while suspended; otherwise log the ID and timestamp and
     `emit manager->activated(action)`. The portal owns repeat semantics, so no pressed-key set is
     needed; `Deactivated` is informational.
   - `Deactivated`: debug log only; do not emit `activated` and do not change the registration.
   - `ShortcutsChanged(session_handle, shortcuts a(sa{sv}))`: this replaces the current set. Update
     the description and trigger of the returned IDs, mark them registered and `desktopAssigned`, and
     for a previously registered ID that is now absent set `registered=false`,
     `desktopAssigned=false`, and emit `registrationFailed` with a reason saying the desktop removed
     the shortcut. Do not change the stored sequence and do not treat the new list as a rebind.
   - Disconnect all portal, request, and session subscriptions when the session is closed; a stale
     `Activated` from a closed session must not emit.

7. Implement the public lifecycle on Wayland.
   - `registerHotkey`: validate as in step 4, replace the stored registration (release or rebind if
     it was bound). If `m_waylandSession`, ensure the portal (missing → false with the cached reason,
     even while suspended, matching the X11 task's order); if `m_suspended`, store and return true
     without creating a session; otherwise call `registerWayland` (create the session if needed,
     queue the bind, return true). The X11 branch keeps its current code.
   - `unregisterHotkey`: on Wayland remove the pending entry and rebind or close as in step 5.
     `unregisterAll`: stop the bind timer, close the session through
     `org.freedesktop.portal.Session.Close` on the session path (1 s cap), disconnect the
     subscriptions, reset `m_wayland`, and clear the registrations. `~HotkeyManager()` continues to
     call `unregisterAll()` first. Do not close the shared session bus; only disconnect the manager's
     subscriptions and close the portal session.
   - `setSuspended`: on Wayland this is a logical pause. Keep the session, set a flag in
     `WaylandState`, ignore `Activated` while suspended, and clear the flag on resume without a
     rebind. The portal has no per-session suspend, and closing the session would force
     `CreateSession`+`BindShortcuts` (and possibly a desktop confirmation) on every settings hotkey
     edit, so document the difference from X11. `registerHotkey` while suspended stores and returns
     true after the portal check; the resume loop in `setSuspended(false)` registers any action that
     was never bound. The X11 suspend and re-grab semantics stay unchanged.
   - `hotkey()`: empty when `desktopAssigned`, otherwise the stored sequence; empty after
     `unregisterAll`. The X11 and Windows behavior is unchanged.

8. Keep the X11 path and the session rule intact.
   - `registerNative` dispatches: `if (m_waylandSession) return registerWayland(...)`, otherwise the
     existing X11 code. `m_x11`/`X11State` must not be created and `XOpenDisplay` must not be called
     on a Wayland session; the constructor-cached decision plus this dispatch is the guarantee.
     Remove the now-redundant per-call Wayland guard in the X11 register path, or keep it only as an
     assertion.
   - The X11 code, reasons, grabs, `BadAccess` detection, auto-repeat handling, suspend/resume, and
     `hotkey()` semantics stay byte-equivalent; the file move is the only structural change. The
     `qtranslate.hotkey` logging category stays defined once in the file.
   - Do not add `ydotool`, `wtype`, the RemoteDesktop portal, XTest, fake key events, or any other
     synthetic input to the application.

9. Add the tests and keep the X11 test green.
   - `tests/CMakeLists.txt`: compile `hotkeymanager_linux.cpp` in `tst_hotkeymanager_x11` and link
     `Qt6::DBus`; keep the Windows block untouched.
   - `tests/tst_hotkeymanager_x11.cpp`: keep all 14 test functions. In `waylandSessionIsRejected()`
     set `DBUS_SESSION_BUS_ADDRESS` to a non-existent bus (for example
     `unix:path=/tmp/qtranslate-no-bus`) with an `EnvironmentGuard`, so the function does not contact
     a real portal on a portal-capable desktop; keep the reason checks for "portal" and "CLI",
     `DISPLAY=:77`, and the XWayland part. The rest of the test is unchanged.
   - New `tests/tst_hotkeymanager_wayland.cpp`, Linux-only, `QTEST_GUILESS_MAIN`; it compiles
     `hotkeymanager_linux.cpp` and links `X11::X11` and `Qt6::DBus` (the X11 symbols are linked
     because the translation unit contains the X11 backend, but the test never opens a display).
     Register it in the Linux block of `tests/CMakeLists.txt`.
   - `initTestCase()`: set `WAYLAND_DISPLAY=wayland-test`, `XDG_SESSION_TYPE=wayland`, and
     `DISPLAY=:77`; start a private `dbus-daemon --session --nofork --print-address=1` with
     `QProcess`, read the address, set `DBUS_SESSION_BUS_ADDRESS` before the first
     `QDBusConnection::sessionBus()` call, and `QSKIP` with a clear message when `dbus-daemon` is
     unavailable. `cleanupTestCase()` kills the daemon and restores the environment.
   - Mock portal: one `QObject` per interface with `Q_CLASSINFO("D-Bus Interface", ...)`, registered
     on the private bus as `org.freedesktop.portal.Desktop` at `/org/freedesktop/portal/desktop`:
     `GlobalShortcuts` with `version=2`, `CreateSession`, `BindShortcuts`, and the three signals; a
     `Request` object with `Response(uint, QVariantMap)` at the spec-derived request path; a
     `Session` object with `Close()` at the session path. Use
     `PortalShortcut`/`QList<PortalShortcut>` for `a(sa{sv})`. Return `session_handle` as a string
     and record the CreateSession, BindShortcuts, and Close calls for assertions.
   - Test functions: no service → `registerHotkey` false with a reason containing "portal" and
     "CLI", the sequence is remembered, and no X11 display is opened; version 0 or absent → false
     with a version reason; happy path → one CreateSession, one BindShortcuts with the four IDs,
     non-empty descriptions and the expected `preferred_trigger` strings (`CTRL+q`,
     `CTRL+SHIFT+q`, `CTRL+w`, `CTRL+ALT+SHIFT+F10`), all four registered, `hotkey()` empty after
     the response, `Activated` emits `activated` exactly once with the right `HotkeyAction`,
     `Deactivated` emits nothing, `ShortcutsChanged` removing one ID emits `registrationFailed`;
     bind cancel/error/timeout → `registrationFailed` per action and no crash; `unregisterAll` and
     the destructor call `Session.Close`; a changed sequence creates a second session with one new
     BindShortcuts and the new trigger; `setSuspended(true)` ignores `Activated` without closing the
     session and resume does not rebind; a stale session's `Activated` is ignored. Use `QTRY_*` with
     timeouts above the 3 s CreateSession cap.
   - Linux test count becomes 22 (20 shared + `tst_hotkeymanager_x11` + `tst_hotkeymanager_wayland`);
     Windows stays 23. Do not rename `tst_hotkeymanager_x11` (the Astra trusted-client setup lists
     it). If the mock cannot be made reliable, keep the no-portal assertions and record the happy
     path as not performed with the reason; do not fake a pass.

10. Build and verify.
    - `debian:trixie` (Qt 6.8.2): clean Debug and Release builds from the read-only source mount with
      `-Wall -Wextra`; grep the log for `warning:`; `timeout 300 xvfb-run -a ctest --test-dir ...
      --output-on-failure` must be 22/22; run `-R 'tst_hotkeymanager'` and the new test alone;
      repeat for Release.
    - Astra Linux 1.8.7 (Qt 6.10.2): native Debug and Release, 0 warnings; CTest 22/22, or 21/22
      with `tst_hotkeymanager_wayland` skipped if `dbus-daemon` is absent (record it honestly);
      after rebuilding, run `sudo /usr/bin/fly-set-execid.sh` so `tst_hotkeymanager_x11` keeps the
      `KBD_R` `security.exec_id` and does not skip (host setup, not code). Clang 16 if time allows.
    - Weston headless: a private `XDG_RUNTIME_DIR`, `weston --backend=headless-backend.so`,
      `QT_QPA_PLATFORM=wayland`, `DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent` (or a private
      daemon without a portal); start `qtranslate --verbose`, confirm the app starts, the
      registration/log carries the portal+CLI reason, `strace -f -e connect` shows no
      `/tmp/.X11-unix` connection, and no `ydotool`/`wtype`/XTest is attempted. Record the
      tray/main-window behavior.
    - Portal happy path: the mock test is the automated gate; perform a real KDE/GNOME Wayland check
      only if such a desktop is available. Otherwise record it as not performed and why (the host is
      Astra X11; Weston headless has no portal). Never claim a desktop check that was not done.
    - Windows Debug/Release and the 23 tests are not performed (no MSVC here); review the header
      `Q_OS_WIN` block and the `if(WIN32)` CMake/test blocks by inspection only.

11. Report the facts for the lead; do not edit `doc/`, the READMEs, `AGENTS.md`, `i18n/`, or
    `doc/handoff.md`.
    - Files: renamed `src/platform/hotkeymanager_linux.cpp`, new
      `tests/tst_hotkeymanager_wayland.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
      `src/platform/hotkeymanager.h`, `packaging/linux/trixie.Dockerfile`.
    - Build: `Qt6::DBus` in `qtranslate` and the tests only, `qtranslate_core` stays D-Bus-free;
      `libqt6dbus6` is a runtime dependency for task 06; `dbus` is a test dependency.
    - Behaviour: portal version >= 1, stable IDs and descriptions, one session and one batched
      BindShortcuts, asynchronous bind failure through `registrationFailed`, logical suspend, an
      empty `hotkey()` after the desktop assigns triggers, the session closed on
      `unregisterAll`/destruction, no X11 display on Wayland, no synthetic input.
    - Verification: trixie Debug/Release 22/22, Astra 22/22 or the recorded skip, the X11 test's 16
      passes, the new test's counts, the Weston result, the real portal check (performed or not and
      why), and the Astra `fly-set-execid.sh` caveat after a rebuild.
    - Limitations for the lead's documentation: the portal descriptions are English,
      `trigger_description` is not a `QKeySequence`, Wayland suspend is logical, and the portal may
      reassign triggers.

## Done when

- `HotkeyManager` on a Wayland session detects `org.freedesktop.portal.Desktop` and
  `GlobalShortcuts` (version >= 1), creates a session with unique handle/session tokens, binds the
  four actions in one `BindShortcuts`, and emits `activated` for the portal's `Activated`. A
  missing portal, interface, or session, or a too-old version, makes `registerHotkey` return false
  with a reason naming the portal and the CLI fallback, without a crash and without opening an X11
  display. Bind failures that arrive after the asynchronous request are reported through
  `registrationFailed` with a clear reason (see Risks).
- `HotkeyManager::hotkey()` returns the stored sequence while the bind is pending or failed, and an
  empty sequence once the desktop has assigned a trigger (the portal reports only `description` and
  `trigger_description`, not a machine-readable trigger). `unregisterAll` and destruction close the
  portal session and disconnect the DBus subscriptions.
- One session is created and one `BindShortcuts` is sent for the four startup registrations; a later
  change recreates the session and rebinds the current set; a stale session's signals are ignored.
- The portal signals are handled: `Activated` maps the stable ID to the right `HotkeyAction`;
  `Deactivated` does not emit; `ShortcutsChanged` updates the registered set and reports a removed
  shortcut.
- No synthetic input is added (no `ydotool`, `wtype`, RemoteDesktop portal, XTest, or fake key
  events). The X11 backend is not opened on a Wayland session (`XOpenDisplay`/`m_x11` untouched),
  proven by the unit test with `DISPLAY=:77` and by the Weston run.
- The fallback is verified under Weston headless (`weston --backend=headless-backend.so`,
  `QT_QPA_PLATFORM=wayland`): the app starts, registration reports the missing portal with the CLI
  fallback, no X11 connection is attempted, and no synthetic input is attempted. The portal happy
  path is verified with the mock DBus service in `tst_hotkeymanager_wayland` or on a portal-capable
  desktop (KDE/GNOME); if the mock is unreliable and no desktop is available, the outcome records it
  as not performed and why.
- `tst_hotkeymanager_x11` still passes under `xvfb-run` in Debian 13 (trixie) with its 14 test
  functions and 16 passes; the new `tst_hotkeymanager_wayland` passes (or `QSKIP`s only when
  `dbus-daemon` is unavailable); the 20 shared tests still pass; Linux has 22 tests, Windows keeps
  23; Debug and Release build in trixie (Qt 6.8.2) and on Astra (Qt 6.10.2) without
  `-Wall -Wextra` warnings; the documentation is the lead's.

## Risks

- `a(sa{sv})` is the hardest QtDBus part: a wrong stream operator or element signature silently
  produces an empty `BindShortcuts` or a marshalling error. Register
  `QList<PortalShortcut>` explicitly, test it against the mock, and log the D-Bus error name and
  message on every failure.
- The portal may show a confirmation dialog for `BindShortcuts`; waiting for its `Response` on the
  GUI thread would freeze the application until the user answers. Therefore the bind is
  asynchronous: `registerHotkey` returns true once the request is accepted, and a rejection or bind
  error arrives through `registrationFailed`. This is the one deliberate deviation from the
  literal "`registerHotkey` returns false when the bind fails"; the alternative, blocking the GUI,
  is rejected. The container/session/version failures still return false synchronously.
- The portal has no per-session suspend. This plan makes Wayland suspension logical (ignore
  `Activated`); the shortcut stays owned by the portal, so the desktop does not return it to other
  applications. Closing the session instead would force a re-bind and a possible desktop dialog on
  every settings hotkey edit. The difference from X11 must be documented.
- `hotkey()` becomes empty after a successful bind because the portal only reports a human-readable
  `trigger_description`, never a `QKeySequence`. Showing the configured sequence would lie when the
  desktop overrides `preferred_trigger`; do not guess.
- A session can only be bound once. The four startup calls must be batched; a runtime change must
  close and recreate the session (and may re-prompt the user). The zero-timer debounce is required
  to keep the unregister+register pair from `AppController::applySettings` in one session.
- `CreateSession` is synchronous with a 3 s cap at the first registration on the GUI thread; a hung
  portal delays startup by up to 3 s. Do not add more synchronous waits.
- The descriptions are English `QStringLiteral`s. `tr()` would add four messages to the
  `HotkeyManager` context and `tst_translations` requires all 14 `.ts` files to be filled, so
  localization is a separate i18n pass, not part of this task.
- `QDBusConnection::sessionBus()` caches the connection and its address. Tests must set
  `DBUS_SESSION_BUS_ADDRESS` before the first use: the X11 test's Wayland function uses a
  non-existent bus, and the new test uses a private `dbus-daemon`; never use the developer's real
  session bus for the mock.
- `dbus-daemon` may be absent; add `dbus` to the trixie Dockerfile and `QSKIP` with a clear message
  instead of failing the suite. The mock's service name is safe only on a private bus.
- The session handle in the `CreateSession` response is a string (`s`), not an object path (`o`),
  despite being an object path; parsing it as `QDBusObjectPath` fails. `BindShortcuts`, `Activated`,
  and `Session.Close` still need `QDBusObjectPath`.
- The portal can return a subset of the requested shortcuts. Only returned IDs are registered;
  missing IDs fail with a reason. Do not assume the desktop honored `preferred_trigger`.
- The request path must be subscribed before the call, otherwise a fast response is lost. Use the
  token-derived spec path and compare the returned handle.
- The X11 code must stay byte-equivalent apart from the rename and the dispatch; the 16 X11 passes
  are the regression gate. Keep the reasons "another program" and "layout" intact, and do not let
  the Wayland branch create `m_x11` or call `XOpenDisplay`.
- `WaylandState` as a `QObject` in a `.cpp` needs the `hotkeymanager_linux.moc` include and a
  complete type at the `unique_ptr` destruction point; the manager's destructor is in the same
  translation unit. Watch `-Wreorder` for the new members and `-Wunused-parameter` in the D-Bus
  slots (`Q_UNUSED`).
- A rebuild on Astra loses `security.exec_id`; re-run `sudo /usr/bin/fly-set-execid.sh` before the
  X11 test, otherwise it `QSKIP`s. This is host setup, not repository content.
- The Windows build cannot be compiled here. Keep the `Q_OS_WIN` header branch and the `if(WIN32)`
  CMake/test blocks byte-identical and review the diff.
- `AppController`'s tray text still says "already used by another program" for any failure, which is
  misleading on Wayland; task 05 adds the one-time Wayland warning and the CLI fallback. This task
  must not change `AppController` or the UI.
