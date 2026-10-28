# Extra Gore Mode in VR (Technical Specification & Architecture)

**Project:** Diablo II: Resurrected VR Unleashed (`d2r-pcvr`)  
**Document:** `docs/VR_EXTRA_GORE.md`  
**Status:** Design Specification & Implementation Details  
**Date:** October 2026  
**Branch:** `feat/extra_gore_for_vr`  

---

## 1. Executive Summary & Technical Feasibility

### Verdict: 100% Feasible
Implementing an **Extra Gore VR system** in Diablo II: Resurrected is fully feasible without altering player saves (`.d2s`), game master data tables, or breaking stock desktop compatibility.

The architecture relies on 4 core reverse-engineered pillars in `d2r-pcvr`:
1. **Combat Interception & Life Write Cycle:** Hooks in `ApplyDamage` (RVA `0x44CE80`) and `SetStat` (RVA `0x44D08F`, stat 6 HP) in [`vr/combat.cpp`](../vr/combat.cpp) and [`vr/melee.cpp`](../vr/melee.cpp).
2. **Real-time Skeletal Modification (Granny 3D):** Hook in `SkeletonInstance::ComputeSelfWorldPose` (RVA `0xF78740`) in [`vr/skeletons.cpp`](../vr/skeletons.cpp).
3. **Dedicated 3D Particle & Shading Pipeline (Direct3D 12):** Custom GPU pipeline in [`steamvr/severed_head_renderer.cpp`](../steamvr/severed_head_renderer.cpp).
4. **Multi-channel Haptic & Spatial Audio Feedback:** [`steamvr/haptic_strength.h`](../steamvr/haptic_strength.h) and [`steamvr/transport.cpp`](../steamvr/transport.cpp).

---

## 2. Resolving the UX Conflict: Gore vs. Low-Health HUD Indicator

### The Problem
In `d2r-pcvr`, [`steamvr/blood_screen.h`](../steamvr/blood_screen.h) renders peripheral blood splatters across the HMD display when the player is low on health (<60%, <40%, <20% HP). If hostile gore were to splatter the headset glass in the same manner, players would suffer from **false-positive damage confusion**.

### Architectural Solution
1. **In-World Focus (3D Spatial):**
   - Extra gore particles (bone splinters, meat chunks, arterial droplets) exist exclusively in 3D world space with stereoscopic depth.
   - The HMD lens remains clear to preserve health indicator readability.
2. **Chromatic Differentiation:**
   - Player low-health vignette: bright crimson human arterial blood.
   - Monster gore: dark coagulated ichor (demons), rotting ichor / marrow dust (undead), yellow-green bile (insects/beasts).
3. **Auditory & Haptic Decoupling:**
   - Player low health is accompanied by the heartbeat loop ([`heartHush`](../steamvr/transport.cpp#L2486)).
   - Monster kills double the duration of the player's configured melee haptic pulse (`GoreSampleFor`) on the striking hand (left or right).

---

## 3. Inter-Process Communication (IPC) Queue

Gore events are captured in the game thread (`vr/` / `d2rl-pcvr.dll`) and transferred lock-free to the OpenXR rendering loop (`steamvr/`) via [`shared/d2r_vr_gore.h`](../shared/d2r_vr_gore.h):

```cpp
enum class GoreType : uint8_t {
    None              = 0,
    SlashDecapitation = 1, // Lethal slash / decapitation
    BluntBoneCrush    = 2, // Blunt skull crushing impact
    PiercingArterial  = 3, // Puncture / arterial jet
    CorpseExplosion   = 4, // Necromancer corpse explosion
    ColdShatter       = 5, // Frozen shatter
    GenericLethal     = 6
};

enum class BloodTaxonomy : uint8_t {
    UndeadRot    = 0, // Skeletons, Zombies, Mummies
    DemonIchor   = 1, // Fallens, Shamans, Corrupt Rogues, Greater Evils
    BeastCrimson = 2, // Beasts, Brutes, Goatmen
    InsectBile   = 3  // Sand Maggots, Scarabs, Spiders
};
```

---

## 4. Subsystem Details

### 4.1. Clean Neck Stump & ECS Entity Verification in Granny 3D (`vr/skeletons.cpp` / `shared/d2r_vr_decapitation.h`)
- **Hook:** `SkeletonInstance::ComputeSelfWorldPose` (RVA `0xF78740`).
- **Clean Stump Body:** Head and descendant facial bones (jaw, horns, eyes) are scaled down to `0.0001f` at the neck joint (`CollapseHeadBones`).
- The headless corpse continues rendering completely natively through D2R's deferred PBR pipeline (receiving real-time torch flicker, normal maps, shadows, and depth fog).
- Zero external `.pvr` head models are loaded, completely avoiding flat lighting artifacts, mismatched textures, or alien horns on unrelated enemies.
- **Atomic Event Consumption (`ConsumeRecentDecapitation`):** Ensures that when a lethal strike hits, only the killed monster consumes the decapitation event within a strict 500 ms / 0.8 m window, preventing adjacent living monsters from losing heads.
- **Versioned ECS Entity Verification (`EntityStillOwnsSkeleton`):** Each registered skeleton instance records its versioned ECS entity ID (`ecsEntity`). Before collapsing a tracked skeleton, `DecapitateMonster` verifies that the original ECS entity still owns `self`, dropping the entry if the corpse was freed and a newly spawned monster reused the skeleton address.

### 4.2. Procedural Cranial Burst (D3D12 `steamvr/severed_head_renderer.cpp`)
At the decapitation instant, the severed head erupts into a violent visceral burst:
- **14 Sharp Cranial Bone Shards:** Dynamic 3D fragments launched outward along the strike impulse vector. Simulated with ballistic gravity ($g = 9.8\text{ m/s}^2$), air drag, tumbling rotation, inelastic floor collision, single bounce, and resting slide.
- **9 Visceral Flesh Chunks:** Soft-tissue gore chunks ejected with aerodynamic drag, tumbling and sticking to the floor.
- **Arterial Pressure Geyser:** 48 droplets emitted from the neck stump in rhythmic cardiac pulses over ~1.6 s, fading smoothly as arterial pressure drops.
- **Corpse Collapse Tracking:** The emission origin follows the collapsing body downward between 0.25 s and 0.9 s rather than hovering at an empty static point in space.
- **PBR Gore Shading:**
  - Dark coagulated crimson blood base `float3(0.075..0.16, 0.004..0.010, 0.004..0.008)`.
  - Sharp high-specular wet highlights `specColor = float3(0.60, 0.12, 0.10)`.
  - Bone fragments shaded with calcified exterior and deep blood stains on inner faces.
  - Per-particle variation to avoid visual repetition.

### 4.3. Monster Qualification & Boss Protections (`shared/d2r_vr_gore.h`)
Aligned with official `monstats.txt` extracted from CASC:
- **Decapitable Humanoids:** Fallen (`19..23`), Fallen Shamans (`58..62`), Corrupt Rogues / Blood Clan demonesses (`43..47, 160..169`), Skeletons (`0..4`), Zombies (`5..9`), Goatmen (`53..57`), etc.
- **Boss Protections:** Act Bosses and major Quest Bosses (Andariel `156, 707`, Duriel `211, 708`, Mephisto `242, 333, 401, 704`, Diablo `243, 705`, Baal `543..545, 557, 559, 570, 709`, Radament `229`, Summoner `250`, Izual `256, 406, 706`, Hephasto `409`, Nihlathak `514, 526`, Ancients `537..542`) are strictly non-decapitable.
- **Non-Humanoids:** Maggots (`68..72`), Spiders (`122..126`), Scarabs (`91..95`), Vipers (`73..77`), Swarms (`87..90`), Birds (`15..18`), Bats (`136..140`), Ghosts/Wraiths (`38..42`), and Traps/Barrels are strictly non-decapitable.

### 4.4. Universal Weapon Capability (`vr/combat.cpp`)
All weapon categories (swords, axes, maces, scepters, staves, wands, polearms, spears, javelins, bows, crossbows, claws, and unarmed) can trigger the decapitation burst on lethal hits according to the configured user rate.

---

## 5. VR Settings Configuration

Configurable in-game via **VR Settings > Combat > Decapitations**:
- `0`: **Off** — Monster heads remain intact.
- `1`: **Crits only (Default)** — Guaranteed on critical hits, lethal ripostes, overkill damage (>150% HP), or high melee multiplier ($\ge 1.5\times$).
- `2`: **Dynamic (35%)** — Guaranteed on crits/overkill + 35% chance on normal lethal hits.
- `3`: **High (65%)** — Guaranteed on crits/overkill + 65% chance on normal lethal hits.
- `4`: **Always (100%)** — All lethal hits trigger the decapitation effect.

Persisted in `d2r_pcvr.ini`:
```ini
[gore]
; Decapitations and hostile extra gore effects in VR (VR Settings > Combat > Decapitations)
; decapitation: 0 = Off, 1 = Crits only (default: crits, ripostes and overkill >150% HP explode heads), 2 = Dynamic (35%), 3 = High (65%), 4 = Always (100%)
; Always executed as a clean cut at the neck stump with zero mesh deformation.
decapitation=1
```

---

## 6. Implementation Phases & Current Status

| Phase | Scope | Status |
| :--- | :--- | :--- |
| **Phase 1** | Combat detection, IPC ring-buffer queue, `monstats.txt` taxonomy, left/right hand attribution & double-duration kill haptics | **Completed** |
| **Phase 2** | Granny 3D skeletal clean neck-stump decapitation (`CollapseHeadBones`), atomic event consumption & versioned ECS entity verification | **Completed** |
| **Phase 3** | VR Settings UI (`Page::Combat > Decapitations`, default `1 = Crits only`) & `d2r_pcvr.ini` persistence | **Completed** |
| **Phase 4** | Direct3D 12 3D procedural cranial burst (ballistic bone shards, flesh chunks, collapsing-stump arterial jets & wet PBR shading) | **Completed** |
| **Phase 5** | First-person blood accumulation & dripping on held weapon blades (`weapon_blood.h`) | Planned (Future) |
| **Phase 6** | Spell-specific gore (`Corpse Explosion` shockwave & `Cold Shatter` ice fragments) | Planned (Future) |

---

## 7. Automated Testing & Verification

- `steamvr/gore_events_test.cpp`: Taxonomy verification, weapon classification, queue ring-buffer mechanics, and kill haptic pulse duration checks.
- `steamvr/granny_decapitation_test.cpp`: 10 suites covering bone masking, humanoid filtering, boss/non-humanoid exclusions, taxonomy accuracy, and recycled skeleton address ECS entity protection.
- **CTest Suite:** 148 / 148 native tests passing.
- **Python Verification:** 17 / 17 tests passing in `steamvr/mod_identity_test.py`.
