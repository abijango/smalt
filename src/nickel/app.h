// The composition root inside Nickel: creates and wires Smalt's modules.
// Task 00 (orchestrator integration); task 30 adds the home takeover.
//
// appInit() runs from NickelHook's init (library load). It decides what this
// boot may do (Safety, switch files, two-strike recovery), writes the startup
// marker, and waits for Nickel to be ready. Everything else is created on
// Nickel's UI thread once Nickel is ready:
//
//   settings -> ledger (open) -> outbox -> recorder (start)
//   -> client + sync engine (only when sync is allowed and config.ini has a
//      login) -> Wi-Fi watcher
//
// and the startup marker is cleared (Nickel's own home stays until task 30).
// Nothing waits on Wi-Fi or the server; a module that fails to start is logged
// and left out, never retried in a loop.
#pragma once

namespace smalt {
namespace kobo {

// From NickelHook's init. Returns nonzero only if the mod must not load.
int appInit();

// From NickelHook's uninstall callback (the library is already removed).
bool appUninstall();

} // namespace kobo
} // namespace smalt
