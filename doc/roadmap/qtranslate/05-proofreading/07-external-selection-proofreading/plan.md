---
kind: stage
status: todo
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

Two children, each with its own result:

- **The hotkey and the command** — a fifth `HotkeyAction` (for example `ProofreadSelection`) in
  `src/platform/hotkeymanager.h`, registered in the Windows backend, in the X11 backend and in the
  Wayland `GlobalShortcuts` portal (with the asynchronous bind semantics recorded in stage 2), the
  `proofreading/hotkey` key (empty by default, so nothing is registered until the user assigns it),
  the row in the **Hotkeys** tab, the tray/`registrationFailed` handling, the CLI command that
  forwards to a running instance, and the updates to `tst_hotkeysequence`,
  `tst_hotkeymanager_x11`, `tst_hotkeymanager_wayland` and `tst_cli`.
- **The confirmation window and the replacement** — a `ProofreadingPopupWindow` near the cursor with
  the issue list and the corrected text, the same buttons as the main-window panel, a confirmation
  step before touching another application, the active-window and fullscreen checks, clipboard
  restoration, the `InPlace` channel of the manager, cancellation on focus loss or on a new request,
  and `tst_proofreadingpopup` (offscreen) plus a Linux integration test that replaces a focused
  editor's selection under X11.

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
- The X11/Wayland test counts and the trusted-client step on Astra change; the Windows build and a
  real portal desktop are recorded as not performed.
