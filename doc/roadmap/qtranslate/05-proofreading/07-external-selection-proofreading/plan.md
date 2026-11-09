---
kind: stage
status: in-progress
base: 62e5cea
depends: [05-main-window-check-and-replace]
---

# External selection checking

## Goal

Add the third scenario of `doc/proofreading.md` §2: check the text selected in another application,
show the candidates in a small window, and replace the selection with the corrected text only after
the user confirms. The captured selection goes through the existing `SelectionGrabber`, the window
verifies that the same application is still active before pasting through `WindowActivation`, and the
undo caveat is stated where the user sees the confirmation.

## What to do, what is touched

Four children in execution order, each with its own result and test gate. The external replacement is
reachable from the CLI before the hotkey exists, so the risky path is proven first; the fifth global
hotkey comes last because it touches the frozen `HotkeyManager` API across the Windows, X11 and
Wayland backends.

1. [01-proofreading-popup-window](01-proofreading-popup-window/plan.md) — the non-activating
   confirmation window near the cursor, built on `ProofreadingPanel` and
   `ProofreadingText::corrected()`: the issue list, the corrected text, the undo caveat and explicit
   Replace selection / Copy / Cancel; offscreen `tst_proofreadingpopup`; no grab, no paste and no
   manager.
2. [02-selection-check-and-replace](02-selection-check-and-replace/plan.md) — `AppController` wires
   the grab, the `InPlace` channel of `ProofreadingManager`, the popup and the guarded replacement,
   and the new `--proofread-selection` command forwards to a running instance through
   `SingleInstance`; copy fallback when the target is gone or absent, clipboard restore, cancellation
   on focus loss/new request, and the manager's gate message with `proofreading/allowOnlineServices`
   off.
3. [03-x11-replacement-integration](03-x11-replacement-integration/plan.md) — a Linux X11 test that
   replaces the selection of a focused editor through the real grabber, popup and
   `WindowActivation`; the guard paths that cannot be automated without linking `AppController` are
   recorded as manual.
4. [04-proofreading-hotkey](04-proofreading-hotkey/plan.md) — the fifth `HotkeyAction`, the
   `proofreading/hotkey` setting (empty by default), the **Hotkeys** tab row, the Windows, X11 and
   Wayland-portal registrations, duplicate checks, the tray tooltip and the backend tests.

Boundaries recorded by the split:

- The confirmation window is **non-activating** (`Qt::WindowDoesNotAcceptFocus` and
  `Qt::WA_ShowWithoutActivating`, no `activateWindow()`): the source application must stay foreground
  until the paste, otherwise `Platform::isForegroundWindow()` cannot pass and the guarded replacement
  is unusable. The window therefore has no Esc shortcut; Cancel is the explicit close.
- A synthetic paste cannot edit another application fragment by fragment, so the external window
  offers the whole-selection result only: `ProofreadingPanel`'s per-issue Replace/Ignore and the
  replacement combo are hidden in its external mode and Replace all is labelled Replace selection.
  The main-window panel is unchanged.
- The external check uses `ProofreadingManager::Channel::InPlace`; a new invocation cancels the
  previous one, exactly as the translation in-place flow does.
- The hotkey and `--proofread-selection` are explicit actions: they are gated by
  `proofreading/allowOnlineServices` through the manager, not by the main-window
  `proofreading/enabled` switch. Assigning the key (or running the command) is the opt-in; do not add
  a second silent gate.
- No text, issue message or replacement goes to the log. New strings join the 14 `.ts` files; each
  outcome records the measured total.
- Windows is not built here (no MSVC), and a real portal-capable Wayland desktop remains a manual
  gap.

## Done when

- The new hotkey action compiles and registers on Windows, X11 and the Wayland portal; on Windows
  and X11 the combination is configurable and a busy combination reports through
  `registrationFailed`; on Wayland the asynchronous bind rule is unchanged, and an empty
  `proofreading/hotkey` registers nothing.
- The same action is reachable from the CLI while an instance runs, with the existing single-instance
  forwarding and without a new option that breaks the current commands; the parser and forwarding
  tests are updated.
- The captured selection is checked, the candidates are shown, and the text is replaced only after
  an explicit confirmation; a different foreground window aborts the paste, the clipboard is restored
  as the other in-place scenarios do, and the window explains that undo depends on the application.
  While `proofreading/allowOnlineServices` is off, the popup shows the manager's gate message and
  replaces nothing.
- Cancel, focus loss and a new request stop the check and the paste cleanly; no text is logged.
- Tests cover the new hotkey action on each backend, the CLI forwarding, the popup list and
  confirmation, and the X11 replacement; the existing hotkey and selection tests stay green. Debug and
  Release build on Linux without warnings and the full suite passes; the counts are recorded in the
  outcome. Windows is recorded as not performed (no MSVC), and a real portal-capable Wayland desktop
  remains a manual gap.

## Depends on

`05-main-window-check-and-replace` for the panel, the replacement rules and the manager wiring;
`03-languagetool-service` for a real provider. The hotkey plumbing touches the frozen
`HotkeyManager` API across three backends, which is why it comes last in the stage.

## Risks and what is needed from the user

- A fifth global hotkey is a user-visible default only if the user assigns one; the key stays empty,
  so no combination is taken silently. The assigned combination must be checked for duplicates with
  the existing four and on Wayland after the portal assigns it.
- Replacement in another application is the dangerous path: it needs the confirmation, the
  active-window check, the clipboard restore and the undo caveat. Never replace without the user's
  confirmation, and never claim undo works there.
- The confirmation window must not take the foreground before the paste, or the active-window check
  can never pass. Show it only after the grab has returned (showing it earlier steals focus from the
  source application), keep it non-activating, and cancel the check when the captured window is no
  longer foreground.
- The X11/Wayland test counts and the trusted-client step on Astra change; the Windows build and a
  real portal desktop are recorded as not performed.
