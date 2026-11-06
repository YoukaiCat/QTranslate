---
kind: task
status: done
detailed: true
---

# Linux build baseline

## Task

The Linux tree configures and 20 of 20 tests pass on the current Astra machine, but the
application target fails at `src/platform/selectiongrabber_stub.cpp`: the header forward-declares
`QMimeData` and holds `std::unique_ptr<QMimeData>`, and the stub's out-of-line destructor needs the
complete type. Debian 13 (trixie) is the primary build and package target, and it ships Qt 6.8.2, so
the project's Qt 6.5+ minimum stays unchanged. This task makes a clean, reproducible Linux baseline
before platform work starts: fix the build, enable GCC/Clang warnings, and record Debug and Release
builds with the full test suite in a `debian:trixie` container and on Astra Linux (Qt 6.10.2).

This is first because every later Linux task builds on this baseline. A trixie container was checked
with the current `find_package(Qt6 6.5 REQUIRED ...)` and the existing deploy block: it configures
with Qt 6.8.2, so the Qt requirement is not lowered and no deploy change is needed unless the build
or install proves otherwise. The documentation is the lead's: report the trixie/Astra and Qt facts
instead of editing `doc/`, the READMEs, or `doc/architecture.md` §4.14.

## Steps

1. Fix the stub: add `#include <QMimeData>` to `src/platform/selectiongrabber_stub.cpp`. The header
   `src/platform/selectiongrabber.h` only forward-declares the class; the destructor and the
   `std::unique_ptr` member need the definition. Check other stubs for the same pattern
   (`grep -rn "unique_ptr<" src`), but change only what the full application build needs.
2. Keep the Qt requirement at 6.5+; do not lower it. On trixie, Qt 6.8.2 satisfies it. If the Linux
   configure or `cmake --install` fails because of the Windows-specific `qt_generate_deploy_app_script`
   options (`NO_COMPILER_RUNTIME`, `DEPLOY_TOOL_OPTIONS` with `--no-system-d3d-compiler`,
   `--no-opengl-sw`, `--translations`), guard the whole deploy block with `if(WIN32)` and report the
   change; the Linux packaging path is task 07's script. Do not delete the Windows block.
3. Enable GCC/Clang warnings in `qtranslate_set_compile_options()`: add `-Wall -Wextra` under
   `elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")`; keep MSVC `/utf-8 /W4 /permissive-`. Do not
   add `-Werror`; the stage requires no warnings, so inspect the log. Fix every warning in shared
   code rather than silencing it with casts; if the count is large, stop and report before expanding
   the task.
4. Add a reproducible trixie build container, `packaging/linux/trixie.Dockerfile`, based on
   `debian:trixie` and installing `build-essential`, `cmake`, `ninja-build`, `git`,
   `ca-certificates`, `qt6-base-dev`, `qt6-tools-dev`, `qt6-tools-dev-tools`, `libx11-dev`,
   `libxtst-dev`, `libsecret-1-dev`, and `xvfb`. Keep it minimal; task 07 adds `dpkg-dev`,
   `desktop-file-utils`, and the AppImage tooling. If a Dockerfile is overkill, a documented
   `docker run` command is acceptable, but the CI job in task 07 must use the same package list.
5. Build Debug in the container from a read-only source mount and a `/tmp` build directory, so the
   container does not leave root-owned files in the repository:
   `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .`, then
   `docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c 'cmake -S /src -B
   /tmp/build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/build-debug --parallel'`.
   Check `/tmp/build-debug`'s log for `warning:`.
6. Run the tests in the container under a display:
   `xvfb-run -a ctest --test-dir /tmp/build-debug --output-on-failure` in the same container run.
   Expect 20/20. A bare `ctest` in the container fails `tst_hotkeysequence` because it is a
   `QTEST_MAIN` window test with no display; always use `xvfb-run` for the full suite. Run
   `xvfb-run -a /tmp/build-debug/qtranslate --version` to prove the application links and starts.
7. Repeat the container build with `-DCMAKE_BUILD_TYPE=Release` and run the same `xvfb-run -a ctest`.
   Record both logs.
8. Build and test natively on Astra Linux with Qt 6.10.2 in a repository-local ignored build dir
   (`build/linux-debug`, `build/linux-release`): configure, build, `ctest` (the current session has
   `DISPLAY=:0`; use `xvfb-run` if it is not available), and `./build/linux-debug/qtranslate
   --version`. This proves the build on the machine used for the `.deb`/portable checks.
9. Record the exact commands, Qt and compiler versions, test counts, and any warning left in the task
   outcome. List the facts the lead needs for `doc/architecture.md` and the READMEs: Debian 13
   (trixie) with Qt 6.8.2, the Qt 6.5+ minimum unchanged, Astra Linux with Qt 6.10.2, and any
   `if(WIN32)` guard if one proved necessary. Do not edit those documents.

## Done when

- `src/platform/selectiongrabber_stub.cpp` compiles and the `qtranslate` target builds in Debug and
  Release in a `debian:trixie` container with Qt 6.8.2.
- `xvfb-run -a ctest` passes 20/20 in the trixie container for both Debug and Release, and the
  native Astra Linux build with Qt 6.10.2 passes 20/20.
- The GCC/Clang build has no `-Wall -Wextra` warnings; the build log is recorded.
- `qtranslate --version` runs from the trixie and Astra builds under `xvfb-run`.
- `CMakeLists.txt` still requires Qt 6.5+ (not lowered), the trixie Linux configure works, and any
  deploy-block guard, if added, is limited to `if(WIN32)` and reported; the Windows deploy block is
  still present (reviewed by inspection, not built here).
- `packaging/linux/trixie.Dockerfile` exists and reproduces the container build; no build artifacts
  are committed.
- The outcome records commands, versions, counts, and the facts for the lead; no documentation file
  is edited.

## Risks

- Do not lower Qt to 6.4: Debian 12 is no longer the target, trixie has Qt 6.8.2, and Qt 6.5+ stays
  the project minimum. Debian 12 may be mentioned as optional compatibility only, not as a required
  build.
- The current deploy block configures on trixie with Qt 6.8.2. If `cmake --install` (task 07) fails
  because of the Windows deploy options, guard the block with `if(WIN32)` then; do not pre-emptively
  change the Windows behaviour.
- `-Wall -Wextra` may surface warnings in shared code that were not visible with MSVC `/W4`; fix them
  in this task, and report if the count threatens the task's size.
- A plain `ctest` in the container fails the display-dependent test; this is an environment issue,
  not a test defect. Use `xvfb-run` everywhere.
- The trixie container build runs as root; mount the source read-only and build in `/tmp` so no
  root-owned files appear in the repository.

## Outcome

Done. The Linux tree builds, links, and passes the full suite without warnings on Debian 13
(trixie) with Qt 6.8.2 and on Astra Linux with Qt 6.10.2. The Qt 6.5+ minimum is unchanged, no
deploy guard was needed, and the Windows deploy block is untouched.

- Build fix: `src/platform/selectiongrabber_stub.cpp` includes `<QMimeData>`. The stub's out-of-line
  destructor instantiates `std::unique_ptr<QMimeData>`, so the complete type is required. No other
  stub needed the same fix: `appcontroller.cpp` and `selectiongrabber_win.cpp` already include the
  header before using the type.
- Warnings: `qtranslate_set_compile_options()` adds `-Wall -Wextra` under
  `elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")`; MSVC keeps `/utf-8 /W4 /permissive-`, and
  `-Werror` is not added. Two warnings surfaced in shared code and were fixed there, with no
  behaviour change: the `numpad` test now builds
  `QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::KeypadModifier, Qt::Key_1))` (same
  combination; removes a GCC ISO-ambiguity warning), and the unused `VkF24` constant is removed
  (Clang `-Wunused-const-variable`). All 81 compile commands carry the flags.
- trixie runs: Debian 13.7, GCC 14.2.0, Qt 6.8.2, CMake 3.31.6, Ninja 1.12.1. Source mounted
  read-only; Debug and Release built in `/tmp/build-debug` and `/tmp/build-release`:
  `cmake -S /src -B /tmp/build-<config> -G Ninja -DCMAKE_BUILD_TYPE=<Config>`,
  `cmake --build ... --parallel`, `xvfb-run -a ctest --test-dir ... --output-on-failure`,
  `xvfb-run -a .../qtranslate --version`. Both configs: 20/20 tests passed, 0 `warning:` lines,
  `QTranslate 0.11.0`.
- Astra runs: Astra Linux 1.8.7, GCC 12.2.0, Qt 6.10.2, CMake 3.31.6, Ninja 1.13.0. Clean Debug and
  Release builds in the gitignored `build/linux-debug` and `build/linux-release` with
  `-DCMAKE_PREFIX_PATH=/usr`; `xvfb-run -a ctest` 20/20 in both, 0 warnings,
  `QTranslate 0.11.0`. A Clang 16.0.6 Debug build also had 0 warnings and 20/20.
- 20 tests is the correct Linux count: `tst_hotkeymanager`, `tst_autostart`, and
  `tst_secretprotector` are Windows-only and are not built on Linux; the Windows total stays 23.
- Container: `packaging/linux/trixie.Dockerfile` uses `debian:trixie`, `--no-install-recommends`,
  the package list from the plan, and removes the apt lists. `xauth` was added: `xvfb` only
  recommends it, and `xvfb-run` fails with "xauth command not found" without it. Task 06 must keep
  the same package list (including `xauth`) and add the packaging tooling.
- Deploy block: no `if(WIN32)` guard was added. Configure succeeds on trixie, and
  `cmake --install` (not required by this task) also completes; the deploy script logs a CMake
  warning that `DEPLOY_TOOL_OPTIONS` has no effect with the generic deployment tool. Task 06
  decides whether the install log needs a guard then. The Windows deploy block and MSVC branches
  are unchanged.
- Windows Debug/Release builds and the 23 tests were not performed: this machine has no
  Windows/MSVC toolchain. The Windows code paths were reviewed by inspection only.
- One `xvfb-run -a` invocation in the container hung before starting the command (the
  `xvfb-run`/`Xvfb` race); reruns passed, including with `timeout`. Task 06 should wrap the CI
  test run in `timeout`.
- Facts for the documentation, not edited here: Debian 13 (trixie) ships Qt 6.8.2; Astra Linux
  1.8.7 has Qt 6.10.2; the project minimum stays Qt 6.5+; on Linux the deploy block needs no guard
  for configure/install, only the install-log warning above. Build logs were kept outside the
  repository in `/tmp/opencode/qt-logs/`; no build artifacts are committed.
- Committed locally after review; nothing was pushed.
