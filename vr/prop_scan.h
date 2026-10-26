#pragma once
#include "../steamvr/props.h"
// The level's catalogued small props near the player, as the HD renderer has
// them (steamvr/props.h), and their matrices put back where the physics moved
// them. Reads go through guarded copies on a worker thread; writes happen
// only inside the renderer's own transform pass, each one re-checked first.
namespace propscan {
void Init();              // loads d2r_vr_props/props.bin beside the plugin
void Configure(bool on);  // [interactions] prop_physics
// Where to look: the player's position and floor (render units).
struct Request {uint64_t session=0;float centre[3]{};float floor=0,unitsPerMetre=0;};
// The XR thread, every frame while props are wanted; a scan starts every
// half second. Latest gives each finished scan once.
void Want(const Request&,uint64_t now);
bool Latest(framevr::props::Scan&);
void Publish(const framevr::props::Poses&); // the XR thread: matrices for the moving props
const char* KindName(uint16_t kind);        // "cup", for the log
void CountKnocks(unsigned byWeapons,unsigned byMovingThings); // for the report
void Transformed(); // the renderer's transform hook, after each original call
void Report();      // a log line every ten seconds while used
}
