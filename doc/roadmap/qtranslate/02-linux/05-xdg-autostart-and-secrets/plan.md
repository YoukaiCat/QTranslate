---
kind: task
status: done
detailed: true
depends: [01-linux-build-baseline]
---

# XDG autostart, secrets, and per-user paths

## Task

Implement the Linux platform modules for the user environment: XDG autostart, libsecret-based
secret storage with a 0600-file fallback and a visible warning, and the Linux settings and log paths.
The primary target is Debian 13 (trixie, Qt 6.8.2); Astra Linux (Qt 6.10.2) must also work. Windows keeps its
`%APPDATA%`/`%LOCALAPPDATA%` paths and DPAPI behaviour.

Autostart is `~/.config/autostart/qtranslate.desktop` (respect `$XDG_CONFIG_HOME`): write or remove a
`[Desktop Entry]` with `Type=Application`, `Name=QTranslate`, `Exec="<path>" --minimized`,
`Icon=qtranslate`, `Terminal=false`, and `X-GNOME-Autostart-enabled=true`; `sync` repairs a moved
executable path and removes the entry when disabled; `isApproved` has no Task Manager equivalent and
returns true. Use the Desktop Entry quoting rules, not shell quoting.

Secrets: use libsecret when the library is present at build time and the keyring is available;
otherwise store the secret in a 0600 file/token and make the fallback visible in the settings
dialog. Plain text must never be written silently. Make libsecret optional in CMake
(`PkgConfig`/`libsecret-1`) so a build without the development package still works.

## Steps

1. Add the Linux per-user path helper and move settings and logs to XDG.
   - Create a header-only `src/core/apppaths.h` in `qtranslate_core` with
     `AppPaths::configDirectory()` and `AppPaths::stateDirectory()`. On Linux read
     `qEnvironmentVariable("XDG_CONFIG_HOME")` and `XDG_STATE_HOME` directly, fall back to
     `~/.config` and `~/.local/state`, and append `qtranslate` with `QDir::filePath()`. On other
     platforms return the current `QStandardPaths::writableLocation(AppDataLocation)` and
     `AppLocalDataLocation` values, so the Windows `%APPDATA%`/`%LOCALAPPDATA%` behavior is
     unchanged. Do not use `QStandardPaths` for the Linux XDG path: it caches locations and uses the
     application name `QTranslate`, not the required lowercase `qtranslate`.
   - `src/core/settings.cpp:187-209`: replace the `QStandardPaths::AppDataLocation` lookup with
     `AppPaths::configDirectory()`; keep the `~/.qtranslate` fallback only for the non-Linux case
     where the helper returns empty. Remove the now-unused `QStandardPaths` include from
     `settings.cpp`. `SettingsStore::defaultFilePath()` and the portable `qtranslate.ini` rule stay
     unchanged, so a portable file next to the executable still wins.
   - `src/app/logging.cpp:59-66`: use `AppPaths::stateDirectory()` for the log directory and keep the
     `qtranslate.log` name and the 5 MB rotation. Update the `%LOCALAPPDATA%` comment in
     `src/app/logging.h:12-13` to mention the Linux `$XDG_STATE_HOME` path; remove the unused
     `QStandardPaths` include from `logging.cpp`.
   - `CMakeLists.txt:31-51`: list `src/core/apppaths.h` in `qtranslate_core` (header only, no new
     `.cpp`).
   - `tests/tst_settings.cpp`: add `settingsPathUsesXdgConfigHome()` under `#if defined(Q_OS_LINUX)`.
     Set `XDG_CONFIG_HOME` to a `QTemporaryDir`, assert that
     `AppPaths::configDirectory() == <tmp>/qtranslate` and that
     `SettingsStore::defaultFilePath(<empty app dir>, configDir) ==
     <tmp>/qtranslate/qtranslate.ini`; restore the previous environment value. Keep the existing
     `portablePath()` test as the portable-mode check.

2. Implement XDG autostart.
   - Create `src/platform/autostart_linux.cpp` with the same public functions as
     `autostart_win.cpp` and the logging category `qtranslate.autostart`.
   - `commandForExecutable()` must use Desktop Entry quoting, not shell quoting: quote the argument
     with double quotes; inside the quoted argument escape `\`, `"`, `` ` `` and `$` with a backslash,
     and replace a literal `%` with `%%` (quoted arguments are still subject to field-code
     expansion). Append ` --minimized` after the quoted executable. A small helper such as
     `quoteExecArgument()` keeps this testable. Do not use `QProcess::splitCommand` or shell
     escaping.
   - The desktop file path is
     `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` (fallback `~/.config/autostart`); the
     autostart directory, not `qtranslate`, is appended to the XDG config home. Create the directory
     with `QDir().mkpath()` when writing.
   - Write the content `[Desktop Entry]`, `Type=Application`, `Name=QTranslate` (`AppInfo::name()`),
     `Exec=<commandForExecutable(QCoreApplication::applicationFilePath())>`, `Icon=qtranslate`,
     `Terminal=false`, `X-GNOME-Autostart-enabled=true`, each on its own line, UTF-8 with a trailing
     newline.
   - `isEnabled()` returns `QFileInfo::exists(path)`; `setEnabled(enabled)` calls `sync(enabled)`;
     `sync(wanted)` writes the file when it is missing or its content differs, removes it when
     `wanted` is false, returns true when the requested state already holds, and returns false with
     a warning when the directory or the file cannot be created or removed. `sync(true)` therefore
     repairs a moved executable path. `isApproved()` returns true; there is no Task Manager
     equivalent.
   - Update the comments in `src/platform/autostart.h:5-20` to name the Windows Run key and the
     Linux XDG desktop entry; the public API stays unchanged.
   - `CMakeLists.txt:92-107`: replace `src/platform/autostart_stub.cpp` with
     `src/platform/autostart_linux.cpp` in the Linux block; keep the stub in the final `else()`
     block for macOS and other platforms, and keep the Windows block untouched.
   - `tests/tst_autostart.cpp`:
     - Keep the existing generic command tests; they also pass on Linux.
     - Add Linux-only tests under `#if defined(Q_OS_LINUX)`:
       `commandUsesDesktopEntryQuoting()` with a path containing a space, `"`, `\`, `` ` ``, `$`
       and `%` (assert the exact escaping and `%%`); `writesReadsAndRemovesDesktopFile()` using a
       temporary `XDG_CONFIG_HOME`, `setEnabled(true)`, `isEnabled()`, the required keys in the
       file, `isApproved() == true`, a manual `Exec=` replacement followed by `sync(true)` to prove
       the repair, `setEnabled(false)`/`sync(false)` to prove removal and idempotence.
     - Use `QCoreApplication::applicationFilePath()` for the written command; set and restore
       `XDG_CONFIG_HOME` in an `init()`/`cleanup()` pair or a local environment guard so the test
       never touches the real `~/.config/autostart`.
   - `tests/CMakeLists.txt:78-136`: add the Linux `tst_autostart` target with
     `src/platform/autostart.h` and `src/platform/autostart_linux.cpp`; keep the Windows block
     unchanged.

3. Implement the secret backend with libsecret and the 0600 fallback.
   - `src/platform/secretprotector.h:6-16`: add `bool isAvailable();` with a comment that Windows
     returns true (DPAPI), Linux reports whether libsecret is compiled in and the keyring service is
     usable, and false means the fallback is active. Update the module comment to name DPAPI,
     libsecret and the 0600 fallback.
   - `src/platform/secretprotector_win.cpp`: add `isAvailable()` returning true. The DPAPI
     `protect`/`unprotect` code is unchanged.
   - `src/platform/secretprotector_stub.cpp`: add `isAvailable()` returning false; the stub still
     refuses to store secrets.
   - Create `src/platform/secretprotector_linux.cpp` with `qtranslate.secret` logging:
     - `protect()`/`unprotect()` keep their contract and return empty for empty/invalid input. Use a
       small prefix to distinguish backends: `l:<uuid>` for a libsecret entry and `f:<uuid>` for a
       fallback file. The INI then contains only an opaque token, not the secret.
     - With `QTRANSLATE_HAVE_LIBSECRET`, define a `SecretSchema` (for example
       `org.qtranslate.Secret`) with string attributes `application=qtranslate` and `id=<uuid>`.
       `protect()` generates a `QUuid::createUuid().toString(QUuid::WithoutBraces)`, calls
       `secret_password_store_sync()` with `SECRET_COLLECTION_DEFAULT` and a label such as
       `QTranslate secret`, and returns `l:<uuid>` on success. `unprotect()` calls
       `secret_password_lookup_sync()` with the same schema/attributes, copies the returned
       `gchar *` with `QString::fromUtf8()` and frees it with `secret_password_free()`. Log and clear
       every `GError`; on failure set a process-local `g_libsecretFailed` flag and fall back.
     - `isAvailable()` returns false when libsecret is not compiled in, when the
       `QTRANSLATE_SECRET_FALLBACK` test hook is set, when `g_libsecretFailed` is set, or when the
       session D-Bus has neither a registered nor an activatable `org.freedesktop.secrets` service.
       It returns true otherwise. This is a cheap, non-blocking capability probe; do not call
       `secret_service_get_sync()` from the settings dialog.
     - The fallback writes the plaintext to
       `AppPaths::configDirectory()/secrets/<uuid>` with owner-only directory and file permissions:
       create the directory with `0700` and the file with `0600` (for example
       `QFile::open(QIODevice::WriteOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner)` plus
       `setPermissions()`). The token is `f:<uuid>`; `unprotect()` reads the file and returns the
       UTF-8 text, or empty with a warning when the file is missing or unreadable. A missing or
       corrupt fallback file must return empty, never the plaintext of another secret.
     - The fallback path is stored under the XDG config directory even in portable mode; the
       portable rule applies to `qtranslate.ini`, and the libsecret path is unaffected. Record this
       as a limitation in the task outcome, not as a silent behavior.
   - `CMakeLists.txt:9-14`: in the Linux branch add `find_package(PkgConfig QUIET)` and, when
     found, `pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)`. When
     `LIBSECRET_FOUND`, link `PkgConfig::LIBSECRET` and define `QTRANSLATE_HAVE_LIBSECRET` on
     `qtranslate`; when not found, build the fallback-only implementation without failing. Keep
     `qtranslate_core` free of libsecret and D-Bus.
   - `CMakeLists.txt:92-107`: replace `src/platform/secretprotector_stub.cpp` with
     `src/platform/secretprotector_linux.cpp` in the Linux block; keep the stub in the `else()`
     block. Link the optional libsecret target only in the Linux branch.
   - `tests/CMakeLists.txt:78-136`: add the Linux `tst_secretprotector` target with
     `src/platform/secretprotector.h` and `src/platform/secretprotector_linux.cpp`, link
     `Qt6::DBus` (for the capability probe) and `PkgConfig::LIBSECRET` plus the compile definition
     when found; keep the Windows target unchanged.
   - `tests/tst_secretprotector.cpp`:
     - Keep `roundTrip`, `emptyText` and `corruptedData`; on Linux they work in either backend and
       must not contain the plaintext in the protected bytes.
     - Add Linux-only tests under `#if defined(Q_OS_LINUX)`. Use a temporary `XDG_CONFIG_HOME`
       shared by the Linux tests and restore it in `cleanupTestCase()`.
     - `fallbackRoundTrip()`: set `QTRANSLATE_SECRET_FALLBACK=1`; `protect()` returns a token
       starting with `f:`, the plaintext is not in the token, `unprotect()` round-trips, the
       `secrets/<uuid>` file exists, contains the plaintext, is `0600`, and its directory is `0700`;
       `isAvailable()` is false. Restore the environment variable.
     - `libsecretRoundTrip()`: unset the force hook; if `isAvailable()` is false, `QSKIP` with the
       keyring/service reason; `protect()` must return a token starting with `l:`, otherwise
       `QSKIP` as not performed; `unprotect()` round-trips. This is the Astra keyring gate; on
       trixie without a keyring it is recorded as a skip, not a pass.
     - Keep the test guiless and independent of a display.
   - Do not edit `src/core/settings.cpp` secret handling: it already base64-encodes the protected
     bytes and never writes plaintext without a codec. The new capability query is only used by the
     UI.

4. Surface the fallback in the settings dialog and after saving.
   - `src/ui/settingsdialog.h:26-28`: add a `bool secretStorageFallback = false` parameter before
     `QWidget *parent`, and pass it to `loadValues()`. The default keeps the existing four-argument
     test calls compiling.
   - `src/ui/settingsdialog.cpp:56-89`: create a word-wrapped red `QLabel` with object name
     `secretStorageHint` on the General tab (next to the autostart hint), with the single source
     string `Secrets are stored unencrypted in a file readable only by your user because the system keyring is unavailable.`;
     `loadValues()` (around lines 369-382) sets its visibility from the new boolean. The existing
     Task Manager hint stays unchanged; on Linux `Autostart::isApproved()` returns true, so it
     remains hidden and needs no new message.
   - `src/app/appcontroller.cpp:546-629`: include `platform/secretprotector.h`; pass
     `!SecretProtector::isAvailable()` as the new `SettingsDialog` argument (before
     `m_mainWindow.get()`, otherwise the `QWidget *` binds to the bool). After an accepted
     `m_settings->save(...)`, if the capability was true before the save and is false afterwards,
     show `QMessageBox::warning(m_mainWindow.get(), QString(), tr("Secrets are stored unencrypted in
     a file readable only by your user because the system keyring is unavailable."))`; this is the
     "never silently" gate for a keyring that fails during the save. Include `<QMessageBox>`.
   - `tests/tst_settingsdialog.cpp`: add `secretStorageHint()` constructing the dialog with
     `secretStorageFallback` false and true; verify the `secretStorageHint` label exists, is hidden
     in the first case, is visible in the second, and contains `unencrypted`.

5. Update the translations for the new user-visible strings.
   - Source strings and contexts:
     - `SettingsDialog`: `Start %1 when you log in` — the Linux autostart label in
       `settingsdialog.cpp:67`; Windows keeps `Start %1 when Windows starts`. Both strings must
       stay in the source and both must be translated; the Linux string keeps the `%1` placeholder.
     - `SettingsDialog`: the single source string
       `Secrets are stored unencrypted in a file readable only by your user because the system keyring is unavailable.`
       — the settings warning.
     - `AppController`: the same warning text — the after-save message box. The message box title is
       empty on purpose, so no extra title string is added.
     - The Task Manager hint `Autostart is disabled in Windows Task Manager.` is unchanged and
       stays in the `.ts` files; on Linux it is hidden by the `isApproved()` result. No new string
       is added for it.
   - Regenerate the `.ts` files with `lupdate` over `src` for all 14 files in `QTRANSLATE_TS_FILES`
     (`i18n/qtranslate_ru_RU.ts`, `es`, `de`, `fr`, `pt_BR`, `it`, `pl`, `uk`, `tr`, `zh_CN`, `ja`,
     `ko`, `ar`, `hi`), or use the CMake `update_translations` target; run with `--no-obsolete` as
     in `i18n/README.md`.
   - Translate every new message in all 14 files, remove the `type="unfinished"` attribute, keep
     `%1` in the autostart label, and do not add or remove leading/trailing spaces. `tst_translations`
     must report no unfinished or empty messages.
   - Do not edit `README.md`, `README.ru.md`, `doc/architecture.md`, `AGENTS.md`, `doc/handoff.md`
     or `i18n/README.md`; report the source strings, contexts and translations to the lead.

6. Build and verify on Debian 13 (trixie) and Astra Linux; review Windows by diff.
   - `debian:trixie` (Qt 6.8.2, GCC 14.2): build `packaging/linux/trixie.Dockerfile` (it already has
     `libsecret-1-dev`; verify `pkg-config` exists and add `pkg-config` to the image only if the
     libsecret module cannot be found without it). Debug and Release from a read-only source mount
     in `/tmp`, with `-Wall -Wextra`; grep the logs for `warning:` and expect none.
   - Run `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure`; expect 27/27 in
     Debug and Release (the 25 existing Linux tests plus `tst_autostart` and `tst_secretprotector`).
     Run `tst_autostart`, `tst_secretprotector`, `tst_settings`, `tst_settingsdialog` and
     `tst_translations` individually and record the QTest pass/skip counts. The fallback secret test
     is forced; the libsecret path in trixie is expected to be a skip because the container has no
     session keyring — record it as not performed with the exact reason.
   - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2): native Debug and Release in `build/linux-debug` and
     `build/linux-release`; 0 warnings; run CTest 27/27. After every rebuild run
     `sudo /usr/bin/fly-set-execid.sh` so the existing X11 tests keep their trusted-client attribute;
     record the existing 16/16, 20/20, 12/12, 13/13 and 7/7 counts. Check
     `pkg-config --exists libsecret-1`; if libsecret is present and a session keyring is available,
     run the libsecret path (the test and a manual keyring round-trip); if the library or the keyring
     is absent, record the libsecret path as not performed with the reason and confirm that the
     fallback path and the no-libsecret build are covered.
   - Manual XDG check on the real session with temporary `XDG_CONFIG_HOME` and `XDG_STATE_HOME`:
     start the release binary with `--verbose`, enable autostart in settings, verify
     `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` and its keys, disable it and verify removal;
     verify the settings file is `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` and the log is
     `$XDG_STATE_HOME/qtranslate/qtranslate.log`. If the session or the binary is unavailable,
     record the check as not performed; the unit tests are the automated gate.
   - Windows: do not build (no MSVC). Review by `git diff` that the Windows paths, DPAPI
     `secretprotector_win.cpp`, `autostart_win.cpp`, the `if(WIN32)` CMake/test blocks and the
     `SecretCodec` call in `main.cpp` are unchanged or only gain the `isAvailable()` true result;
     record the Windows 23 tests as not performed.
   - `git diff --check`; confirm the change touches only this task's source, tests, CMake and `.ts`
     files, no `doc/`, README, `AGENTS.md` or `handoff.md`; do not commit, the lead commits.

## Done when

- `src/platform/autostart_linux.cpp` is selected on Linux; `Autostart::commandForExecutable`,
  `isEnabled`, `setEnabled`, `sync` and `isApproved` behave as described; the desktop file is
  written under `$XDG_CONFIG_HOME/autostart` (fallback `~/.config/autostart`) with `Type=Application`,
  `Name=QTranslate`, `Exec="<path>" --minimized` using Desktop Entry quoting, `Icon=qtranslate`,
  `Terminal=false` and `X-GNOME-Autostart-enabled=true`; the directory is created, disabling removes
  the file, and a changed executable path is repaired by `sync(true)`. `isApproved()` returns true on
  Linux and the Task Manager hint stays hidden.
- `src/platform/secretprotector_linux.cpp` stores and retrieves secrets through libsecret when the
  library is compiled in and `org.freedesktop.secrets` is available, and otherwise writes a
  `0600` token file under `$XDG_CONFIG_HOME/qtranslate/secrets` (directory `0700`) and returns
  `f:<uuid>`; `protect`/`unprotect` keep their contract and round-trip both backends. `isAvailable()`
  reports the capability; the settings dialog shows the unencrypted-storage warning when the fallback
  is active, and a keyring failure during save produces the visible message box instead of a silent
  plaintext write. libsecret is optional in CMake: a build without `pkg-config`/`libsecret-1` still
  configures and builds the fallback.
- Linux settings use `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` (fallback `~/.config/qtranslate`)
  and logs use `$XDG_STATE_HOME/qtranslate/qtranslate.log` (fallback `~/.local/state/qtranslate`);
  `XDG_STATE_HOME` is read directly. The portable `qtranslate.ini` next to the executable still wins.
  Windows paths are unchanged, and `qtranslate_core` remains free of libsecret and D-Bus.
- `tst_autostart` and `tst_secretprotector` on Linux pass in Debian 13 (trixie) headless: the
  autostart test uses a temporary `XDG_CONFIG_HOME` and covers quoting, writing, reading, repair and
  removal; the secret test forces the fallback through `QTRANSLATE_SECRET_FALLBACK`, checks the
  `0600` file, and has a libsecret test that passes on Astra when a keyring is available or is
  recorded as not performed with the reason otherwise. `tst_settings` passes with the XDG path test,
  `tst_settingsdialog` covers the warning, and `tst_translations` passes.
- New user-visible strings (the Linux autostart label `Start %1 when you log in` and the fallback
  warning `Secrets are stored unencrypted in a file readable only by your user because the system
  keyring is unavailable.`) are added through `lupdate` and translated in all 14
  `i18n/qtranslate_*.ts` files without `type="unfinished"`; the existing Task Manager hint stays
  translated and hidden on Linux. The READMEs, architecture, `AGENTS.md`, `doc/handoff.md` and
  `i18n/README.md` are not edited; the strings and contexts are reported to the lead.
- Debug and Release build in `debian:trixie` (Qt 6.8.2) and on Astra Linux 1.8.7 (Qt 6.10.2) with no
  `-Wall -Wextra` warnings; `xvfb-run -a ctest` is 27/27 in both configurations; the 25 existing
  Linux tests still pass (including the X11 hotkey and selection tests with their existing counts),
  and Linux has 27 CTest tests. On Astra the rebuild is followed by `sudo
  /usr/bin/fly-set-execid.sh` before the X11 tests.
- Windows Debug/Release and the 23 Windows tests are recorded as not performed (no MSVC); the diff
  review confirms that the Windows paths, DPAPI `secretprotector_win.cpp`, `autostart_win.cpp`, the
  `if(WIN32)` blocks and the `SecretCodec` wiring are unchanged or only gain the
  `isAvailable() == true` result. `doc/handoff.md`, the READMEs, `AGENTS.md`, `doc/architecture.md`,
  the stage and other roadmap nodes and the journal are untouched; the lead commits.

## Risks

- libsecret may block or prompt when no keyring service is running. The capability probe must be the
  cheap D-Bus registered/activatable check, not `secret_service_get_sync()`, and `protect()` must fall
  back on a failed call without hanging the settings dialog. The test must force the fallback with
  `QTRANSLATE_SECRET_FALLBACK` instead of depending on a session keyring; a real keyring test on
  trixie is expected to skip and must be reported as not performed, never as passed.
- The D-Bus capability probe can be a false negative when the keyring service is installed but not
  running and not listed as activatable. `protect()` uses the probe result, so such a session writes
  the fallback with the visible warning instead of trying libsecret; an unnecessary warning and a
  plaintext fallback are preferable here to a blocking service call, and the behavior must be
  recorded. Do not call a blocking libsecret service getter from the dialog.
- Both backends leave the previous token behind when a secret is overwritten: a libsecret entry or a
  `secrets/<uuid>` file is not removed because `protect()` does not receive the old token. This is a
  storage-growth limitation, not a correctness failure; record it in the outcome rather than adding a
  key-name API to `SecretCodec` in this task.
- The fallback writes plaintext to a file. The directory must be `0700` and the file `0600`; create
  the file with owner-only permissions rather than creating it `0644` and fixing it later. The
  warning must be visible in the settings dialog and again in the message box when a keyring fails
  during save. Do not log the secret or the file content.
- Desktop Entry quoting is not shell quoting. Escape `\`, `"`, `` ` `` and `$`, and write a literal
  `%` as `%%`; a path with spaces, quotes or a percent sign must round-trip through the test. Do not
  use `QProcess::splitCommand` or shell escaping, and do not add `--minimized` inside the quoted
  executable argument.
- `QStandardPaths` caches locations and uses the application name `QTranslate`; read
  `XDG_CONFIG_HOME`/`XDG_STATE_HOME` directly and use the lowercase `qtranslate` directory. The
  autostart and settings tests must set the temporary XDG variables before the first call and restore
  them afterwards; they must never write to the real `~/.config/autostart` or the real user config.
- The portable `qtranslate.ini` next to the executable must still win over the XDG settings path.
  `defaultFilePath()` stays the pure rule; the fallback secret token file lives under the XDG config
  directory even in portable mode and this limitation must be recorded.
- Adding the `SettingsDialog` bool changes the constructor. The default keeps the existing test calls
  compiling, but `AppController::showSettings()` must pass the capability before the `QWidget *`
  parent; passing only the bool in the old position would silently convert a `QWidget *` to bool.
  `tst_settingsdialog` must cover both the fallback and the normal label state.
- libsecret is optional: `find_package(PkgConfig QUIET)` and `pkg_check_modules(... QUIET ...)` must
  not fail the configure when the development package is absent. In trixie `libsecret-1-dev` is in
  `packaging/linux/trixie.Dockerfile`; if `pkg-config` itself is missing there, add it to the image.
  On Astra the development package may be absent, in which case the build exercises the fallback and
  the libsecret path is recorded as not performed.
- `tst_translations` fails on any unfinished or empty message, so all 14 `.ts` files must be
  updated, the `%1` in the Linux autostart label preserved, and `type="unfinished"` removed. The
  Task Manager hint stays as an existing translated message; hiding it on Linux must not remove it
  from the `.ts` files.
- Windows cannot be compiled here. Keep `secretprotector_win.cpp`, `autostart_win.cpp`, the
  `if(WIN32)` CMake/test blocks and the Windows `%APPDATA%`/`%LOCALAPPDATA%` paths unchanged; the
  only shared API addition is `SecretProtector::isAvailable()` returning true on Windows. Review the
  Windows side by diff and record the 23 tests as not performed.
- The task is broad (two new platform files, a path helper, UI, two test adaptations and 14 `.ts`
  files). If the libsecret/desktop-entry work grows beyond the plan, stop and report instead of
  silently taking a shortcut; do not drop the fallback, the warning or the translations.
- `doc/handoff.md`, the READMEs, `AGENTS.md`, `doc/architecture.md`, the stage and the journal are
  the lead's; do not update them in this task. The final report carries the new counts, the
  user-visible strings, the Astra/trixie results and the libsecret not-performed reason.

## Outcome

Done. Linux now uses the XDG user paths, writes `~/.config/autostart/qtranslate.desktop`, and stores
secrets through libsecret with a visible 0600-file fallback. Linux has 27 tests, Windows keeps 23.

- Files: new `src/core/apppaths.h`, `src/platform/autostart_linux.cpp` and
  `src/platform/secretprotector_linux.cpp`; modified `CMakeLists.txt`, `src/core/settings.{h,cpp}`,
  `src/main.cpp`, `src/app/logging.{h,cpp}`, `src/app/appcontroller.cpp`,
  `src/platform/autostart.h`, `src/platform/secretprotector{.h,_stub.cpp,_win.cpp}`,
  `src/ui/settingsdialog.{h,cpp}`, `tests/CMakeLists.txt`, `tests/tst_autostart.cpp`,
  `tests/tst_secretprotector.cpp`, `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp`, and all
  14 `i18n/qtranslate_*.ts`. `qtranslate_core` stays free of libsecret, D-Bus and WinAPI;
  `autostart_win.cpp`, `selectiongrabber_win.cpp`, the `if(WIN32)` blocks and the Windows paths are
  unchanged, and the shared code only gains the `SecretProtector` capability queries.
- Paths: `AppPaths::configDirectory()`/`stateDirectory()` read `XDG_CONFIG_HOME`/`XDG_STATE_HOME`
  directly on Linux, ignore empty and relative values, fall back to `~/.config`/`~/.local/state`
  and append the lowercase `qtranslate`; other platforms return the previous
  `QStandardPaths::AppDataLocation`/`AppLocalDataLocation`. Settings use
  `<config>/qtranslate/qtranslate.ini` and the log `<state>/qtranslate/qtranslate.log` (5 MB
  rotation kept); the portable `qtranslate.ini` next to the executable still wins.
- Autostart: Desktop Entry quoting (escape `\`, `"`, `` ` ``, `$`, a literal `%` as `%%`) with
  `"<path>" --minimized`, a `[Desktop Entry]` with `Type=Application`, `Name=QTranslate`, `Exec=`,
  `Icon=qtranslate`, `Terminal=false` and `X-GNOME-Autostart-enabled=true` under
  `$XDG_CONFIG_HOME/autostart/qtranslate.desktop`; `sync(true)` repairs a moved path, `sync(false)`
  removes the file, `isEnabled()` is the file existence and `isApproved()` is true.
- Secrets: libsecret (`l:<uuid>`, schema `org.qtranslate.Secret`, attributes `application`/`id`)
  when `QTRANSLATE_HAVE_LIBSECRET` is defined and the capability probe (a registered or activatable
  `org.freedesktop.secrets` over D-Bus, 1 s cap, no `secret_service_get_sync`) is true; otherwise an
  `f:<uuid>` file under `<config>/qtranslate/secrets` with a `0700` directory and a `0600` file, no
  plaintext in the token or the log, and empty returns for missing or corrupt tokens.
  `isAvailable()` reports the fallback, including after a libsecret failure (`g_libsecretFailed`).
- Save behavior: `writeSecret()` keeps an unchanged secret's existing token instead of re-protecting
  it, and `SecretProtector::canReuse()` decides: an `l:` token is kept through a transient outage
  (never downgraded to plaintext), an `f:` token is migrated to libsecret once the keyring is
  available, and unknown or corrupt tokens or a missing key are protected again. This closes the
  silent plaintext write on `prepareForQuit()`/`selectService()` saves.
- UI: `SettingsDialog` takes `bool secretStorageFallback` before `QWidget *parent` and shows the red
  word-wrapped `secretStorageHint`; `AppController` passes `!SecretProtector::isAvailable()` and
  shows a `QMessageBox::warning` when the capability was true before an accepted save and false
  after. The Windows Task Manager hint stays for Windows and is hidden on Linux.
- CMake: `find_package(PkgConfig QUIET)` and `pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET
  libsecret-1)` only on Linux; `PkgConfig::LIBSECRET` and `QTRANSLATE_HAVE_LIBSECRET` only on the
  Linux `qtranslate` target and `tst_secretprotector`, so a build without libsecret still configures
  and builds the fallback. The stub stays in the final `else()` branch.
- Tests: `tst_autostart` covers the Desktop Entry quoting and write/read/repair/remove with a
  temporary `XDG_CONFIG_HOME`; `tst_secretprotector` covers the forced fallback (`f:`, plaintext
  only in the file, `0600`/`0700`), the libsecret round-trip (QSKIP without a keyring), malformed
  tokens and `canReuse`; `tst_settings` covers the XDG path (including relative values),
  unchanged-secret reuse, migration and the failed-protect retry; `tst_settingsdialog` covers the
  hidden and visible warning.
- Verification:
  - `debian:trixie` (Qt 6.8.2, GCC 14.2), read-only mount, Debug and Release: 0 warnings;
    `timeout 300 xvfb-run -a ctest` 27/27 in both; `tst_settings` 15/15, `tst_autostart` 6/6,
    `tst_settingsdialog` 20/20, `tst_translations` 4/4; `tst_secretprotector` 7 passed + 1 skipped
    (`The org.freedesktop.secrets service is not registered or activatable`), recorded as not
    performed. A build with a `pkg-config` shim that hides `libsecret-1` also configured and built
    with 0 warnings and the same 7+1 (skip reason `libsecret support is not compiled in`).
  - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2), native `build/linux-debug`/`build/linux-release`:
    0 warnings; CTest 27/27 in both; `tst_settings` 15/15, `tst_secretprotector` 8/8 (real
    libsecret round-trip), `tst_autostart` 6/6, `tst_settingsdialog` 20/20, `tst_translations` 4/4;
    the existing X11 counts are unchanged (16/16, 20/20, 12/12, 13/13, 7/7). The rebuild was
    followed by `sudo /usr/bin/fly-set-execid.sh`.
  - Headless application check with a temporary `XDG_CONFIG_HOME`/`XDG_STATE_HOME`: `autostart=true`
    wrote the desktop file with the required keys and `Exec="<release path>" --minimized`,
    `autostart=false` removed it, and the settings and log landed in the XDG directories. GUI
    clicks were not performed because xvfb has no tray; the dialog is covered by
    `tst_settingsdialog`.
- Translations: `SettingsDialog` `Start %1 when you log in` and the fallback warning in the
  `SettingsDialog` and `AppController` contexts were added through `lupdate` and translated in all
  14 `.ts` files with no `type="unfinished"`; `%1` is preserved and the existing `Start %1 when
  Windows starts` and Task Manager hint stay. READMEs, architecture, `AGENTS.md`, `i18n/README.md`
  and the handoff were not edited.
- Review: accepted and fixed — the silent plaintext fallback on non-dialog saves, the
  backend-aware reuse/migration (`canReuse`), relative XDG values, the failed-protect retry, and the
  test environment guard. Rejected as plan-prescribed or out of scope — the Desktop Entry
  string-level backslash/`%%` parser edge cases, keyring test entries accumulating, and the trixie
  keyring skip. Windows Debug/Release and the 23 tests were not performed (no MSVC); the diff review
  confirms the Windows files and `if(WIN32)` blocks are unchanged apart from `SecretProtector`
  gaining `isAvailable() == true` and `canReuse()`.
- Known limitations: the fallback file lives under the XDG config directory even in portable mode;
  old keyring entries/fallback files remain when a secret is overwritten or migrated (no delete
  API); the D-Bus probe can be a false negative when the service is installed but not running or
  activatable; the settings hint is based on `isAvailable()`, so a stored `f:` token is not
  highlighted when the keyring is otherwise available (it migrates on the next save); a token that
  cannot be decoded (a no-libsecret build or a transient keyring failure at load) is removed from
  the INI on the next save; a literal backslash is written as `\\` and GLib does not load `%%` in
  `Exec`, so an install path containing `\` or `%` can fail to autostart; Astra keyring test entries
  remain; the live `:0` X11 tests occasionally flake under an active desktop (they were stable
  under `xvfb-run` and are not touched by this task).
- Facts for the documentation (not edited here): Linux has 27 tests and Windows 23; the trixie
  `xvfb-run` gate is the automated evidence, the Astra run needs `sudo /usr/bin/fly-set-execid.sh`
  after every rebuild; the new user-visible strings and their contexts are `SettingsDialog`
  `Start %1 when you log in`, and the fallback warning in `SettingsDialog` and `AppController`.
  The portable rule still applies only to `qtranslate.ini`, not to the secret fallback directory.
