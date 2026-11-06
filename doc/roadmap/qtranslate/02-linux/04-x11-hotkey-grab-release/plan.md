---
kind: task
status: todo
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

Planner to write.

## Done when

Planner to write.

## Risks

Planner to write.
