#pragma once

// Helpers around the OS rules for bringing windows to the foreground.
namespace Platform {

// Lets any other process take the foreground (Windows: AllowSetForegroundWindow).
// Called by a secondary instance before asking the primary to show its window.
void allowAnyProcessToActivate();

} // namespace Platform
