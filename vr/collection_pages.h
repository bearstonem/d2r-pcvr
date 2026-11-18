#pragma once
// Collection tabs, step 1 (spike): the page probe.
//
// The catalog needs a place to keep one copy of each item. D2RLoader gives a
// plugin "custom player pages" (D2RLPlugin/inventory.h). Two things must be
// proven before any UI is built:
//   1. can this install register more than one page (retail saves allow one,
//      extended saves allow up to 247)?
//   2. can an item be moved cursor -> page -> cursor?
//
// Both change how a character is saved, so nothing here runs on its own. The
// D2RLoader developer console drives it, and the owner runs it on a disposable
// offline character first:
//   collectionpages register    registers the three category pages (16x16)
//   collectionpages info        logs the current page state
//   collectionpages move        moves the cursor item onto page 0 and back
//   collectionpages unregister  releases the pages again
namespace D2RL {
struct PluginContext;
}

namespace framevr::collection {

// Stores the context and queries the inventory service. Registers the console
// command. When probeAtLoad is true (d2r_pcvr.ini [collection] pages_probe=1) it
// also registers the three category pages as soon as the plugin loads, which is
// the console-free route for a headset test. Off by default.
void InitPages(const D2RL::PluginContext* ctx, bool probeAtLoad);

// Releases any page this session registered. Called from D2RLoaderUnloadPlugin.
void ShutdownPages();

}   // namespace framevr::collection
