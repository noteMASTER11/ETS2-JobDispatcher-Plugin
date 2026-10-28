#pragma once

namespace jd::overlay {
struct Diagnostics { bool running; bool attached; bool interactive; bool panel; bool icon; };
// Call from a worker after DllMain has returned. The first game Present attaches UI.
bool Start();
// Call before plugin shutdown, outside DllMain. Restores input and releases hooks.
void Stop();
// Read-only diagnostics for integration checks.
Diagnostics Inspect();
// Used only by the host's explicitly enabled integration command reader.
void QueueTestPanel(bool open);
} // namespace jd::overlay
