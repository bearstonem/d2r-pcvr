#pragma once
// Collection tabs, step 1 (spike): the read-only report.
//
// This module answers the questions the catalog needs before any storage or UI
// exists: does the inventory service resolve, what does the local character
// carry, and which uniques, sets and runewords are already in the stash and the
// mules. It never registers a page, moves an item or writes a save.
//
// `collection` in the D2RLoader developer console queues the scan on the UI
// thread and prints the report to the plugin log (d2r-vr-frame.log).
namespace D2RL {
struct PluginContext;
}

namespace framevr::collection {

// Stores the context, queries the inventory service and registers the console
// command. Safe to call when the service is missing: the module stays dormant
// and the command says so instead of failing the plugin load.
void Init(const D2RL::PluginContext* ctx);

// Drops the cached context. Called from D2RLoaderUnloadPlugin.
void Shutdown();

}   // namespace framevr::collection
