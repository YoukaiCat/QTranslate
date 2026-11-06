---
kind: task
status: todo
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

## Done when

- `src/platform/autostart_linux.cpp` is selected on Linux; `Autostart::commandForExecutable`,
  `isEnabled`, `setEnabled`, `sync`, and `isApproved` behave as described; the desktop file is
  written under `$XDG_CONFIG_HOME/autostart` (or `~/.config/autostart`), the directory is created,
  and disabling removes the file. A changed executable path is repaired by `sync(true)`.
- `src/platform/secretprotector_linux.cpp` stores and retrieves secrets through libsecret when
  available, falls back to a 0600 file/token when the library is absent at build time or the keyring
  is unavailable, and round-trips both ways. `SecretProtector::protect`/`unprotect` keep their
  contract; a capability query lets the settings dialog show a warning when the fallback is active.
  A secret is never written in plain text without that warning.
- Linux settings use `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` (fallback `~/.config/qtranslate`)
  and logs use `$XDG_STATE_HOME/qtranslate/qtranslate.log` (fallback `~/.local/state/qtranslate`);
  read `XDG_STATE_HOME` directly (fallback `~/.local/state`) so the path is explicit and works on
  trixie and Astra. The portable
  `qtranslate.ini` next to the executable still wins. Windows paths are unchanged.
- `tst_autostart` and `tst_secretprotector` on Linux pass in Debian 13 (trixie) headless (a temporary
  `XDG_CONFIG_HOME` for the autostart test; the fallback forced through an environment/test hook for
  the secret test, and the libsecret path checked on Astra or recorded as not performed if no keyring
  is available). `tst_settings` still passes.
- New settings strings (the Linux autostart label, the fallback warning, and the hidden Task Manager
  hint on Linux) are added with `lupdate` and translated in all 14 `i18n/qtranslate_*.ts` files;
  `tst_translations` passes. Do not edit the READMEs or architecture; report the user-visible text to
  the lead.
- The 20 shared tests still pass and the build has no warnings.

## Risks

- libsecret may block or prompt when no keyring service is running; the fallback must trigger on a
  failed call without hanging the UI, and the test must force the fallback rather than depend on a
  session keyring.
- `QStandardPaths` caches locations; the autostart test must set `XDG_CONFIG_HOME` before the first
  call, and the implementation should prefer `qEnvironmentVariable`/`GenericConfigLocation` over a
  cached path where practical.
- The 0600 fallback stores a secret in the user's config directory; the INI or the fallback file must
  be chmod 0600, and the settings warning must be visible, not only logged.
- Adding UI strings changes the `.ts` files; `tst_translations` fails on any unfinished or empty
  translation, so all 14 files must be updated in this task.
