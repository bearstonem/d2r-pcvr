# Handheld Dynamic Torches: 1H Club Fire Imbution & Spatial Lighting

## 1. Overview & Motivation

In vanilla *Diablo II: Resurrected*, the player's light radius (`STAT_LIGHTRADIUS`) is an omnidirectional radial gradient centered passively at the character's feet. On a flat monitor, this abstraction is functional.

In Virtual Reality (VR), however, having light radiate passively from your waist breaks presence. When descending into pitch-black dungeons such as the Catacombs, the Kurast Sewers, or the Tombs of Tal Rasha, true immersion demands that light originates **from a physical torch held in your hand**, dynamically casting shadows and illuminating stone walls, ceilings, and archways in real time as you rotate your wrist and reach into dark corridors.

Furthermore, a torch in a dark fantasy world is not merely an ornament: it is a **one-handed bludgeoning weapon (1H Club)** that can strike enemies, deal physical and fire damage, and illuminate dark corridors.

---

## 2. Project Guardrails & Canonical D2 Design

### The "No Save Corruption" Rule
Per `AGENTS.md` and `CONTRIBUTING.md`, the mod strictly mandates:
> *"Preserve upstream credits, licensing, player saves and unmodded desktop play."*
> *"There are no permanent equipment, skill-binding, item-stat or save edits."*

Creating a custom `.txt` item row (such as a fictitious `trc` item code) would serialize foreign data into the player's `.d2s` character save file. Opening that character in unmodded desktop D2R would result in immediate save corruption or load rejections (*"Bad character data"*).

### The Canonical RPG Solution: The Fire-Imbued Club
Instead of fabricating a foreign item, D2R-PCVR qualifies **native one-handed wooden clubs (`clb` - Club, `spc` - Spiked Club)** as functional torches when infused with fire through authentic Diablo II game mechanics:

1. **Native Fire Damage (`fireDamage > 0`):**
   * Evaluated via native engine stat queries (`combat::EquippedFireDamage` checking `STAT_FIREMINDAM` / `STAT_FIREMAXDAM`, stat IDs 48 and 49).
   * **Socketed Fire (Ruby or Ral Rune):** Inserting a Ruby (Chipped through Perfect) or a Ral rune into a socketed club grants legitimate fire damage, igniting the club's head into a live burning torch.
   * **Unique Club *Felloak* (`clb`):** The canonical level-3 unique club *Felloak* inherently possesses +6-8 Fire Damage and fire resistance, serving as the ultimate early-game unique torch.
   * **Magic & Rare Fire Affixes / Jewels:** Any club rolling fire damage prefixes (*Fiery*, *Flaming*, *Smoking*) or socketed with fire jewels immediately qualifies.
   * **Empty Sockets:** Plain clubs with empty sockets deal 0 fire damage and do **NOT** ignite. Non-fire socketed items (Sapphire, Emerald, Topaz, Skull) deal non-fire damage and do **NOT** ignite.
2. **The *Enchant* Spell Buff:**
   * When affected by the Sorceress's *Enchant* spell (native state ID 16, skill ID 52) or item charges (e.g., *Demon Limb*), any standard wooden club bursts into intense flame and casts brilliant light for the spell's duration.

---

## 3. Core Architectural Pillars

```
                     +---------------------------------------+
                     |           PLAYER (HMD & Hands)        |
                     |  - Left/Right OpenXR Controller Pose  |
                     |  - Linear / Angular Velocity          |
                     |  - D2RVR_Shared (p, q, handsValid)    |
                     +---------------------------------------+
                                         |
                 +-----------------------+-----------------------+
                 |                                               |
       [Physical Weapon Layer]                         [Spatial Lighting Layer]
    - Evaluates `code[0]` & `code[1]`                - OpenXR Real-time hand tracking
    - Checks `IsTorchClubCode` ("clb", "spc")        - Aligns along weapon bone gripAxis
    - Queries Native Fire Damage (stats 48/49)       - Multi-harmonic organic flicker
    - Checks Active *Enchant* state (16)             - Flame drag from hand velocity
    - Maps 1H Club swing cadence ("1hs")             - Injects Tip into `GameLightPos[0]`
                 |                                               |
                 +-----------------------+-----------------------+
                                         |
                       +-----------------------------------+
                       |    D2R Deferred / ReShade Pass    |
                       | - Illuminates ceilings & walls    |
                       | - Feeds `D2R_DepthFog.fx`         |
                       +-----------------------------------+
```

### Pillar 1: 1:1 Real-Time Hand Tracking & Decoupled Emitter
* OpenXR controller tracking (`D2RVR_Shared::rightHand` / `leftHand`, `rightRot` / `leftRot`) is read every frame at 90/120 Hz.
* The torch head emitter extends along the tool handle:
  $$\mathbf{P}_{\text{tip}} = \mathbf{P}_{\text{hand}} + \mathbf{R}_{\text{hand}} \cdot \mathbf{Offset}_{\text{shaft}}$$
  where $\mathbf{Offset}_{\text{shaft}}$ defaults to $(0.0\text{ m}, 0.44\text{ m}, -0.06\text{ m})$, or aligns dynamically to the weapon skeleton's `wpose.gripAxis` when active.
* In `vr/vrcam.cpp`, the light focus position $\mathbf{P}_{\text{tip}}$ is transformed to D2R world space via the camera frame vectors ($\mathbf{CamRight}$, $\mathbf{CamForward}$, $\mathbf{CamUp}$) scaled by `upm` (units per metre), ensuring that the light focus moves with absolute 1:1 fidelity as the player gestures, turns their wrist, or walks.

### Pillar 2: Dynamic Flame Inertia & Drag
* Real fire is buoyant and floats upward, but air resistance pulls it backward during swings.
* The apparent flame vector is:
  $$\mathbf{V}_{\text{flame}} = \text{Normalize}\left((0, 1, 0) - \mathbf{v}_{\text{hand}} \cdot k_{\text{drag}}\right)$$
  ensuring the flame trails behind rapid weapon swings before rising upright when still.
* **VR Settings > Video > Torch flame** (`[torch] flame`, On by default) shows or hides the flame drawn on the club's head; the torch's light stays either way.

### Pillar 3: Multi-Harmonic Organic Sinusoidal Flicker
* Static light radii look sterile in VR. Natural flame turbulence is simulated by summing non-harmonic prime-frequency sine waves:
  $$W(t) = 0.45\sin(2\pi \cdot 3.1 t) + 0.30\sin(2\pi \cdot 7.3 t) + 0.15\sin(2\pi \cdot 13.7 t) + 0.10\sin(2\pi \cdot 23.9 t)$$
* This wave $W(t) \in [-1.0, 1.0]$ continuously modulates:
  * **Radius:** $R(t) = R_{\text{base}} \cdot (1.0 + 0.06 \cdot W(t))$ (default $18.0 \pm 1.1$ world units).
  * **Color Temperature:** shifts dynamically between saturated golden amber and hot fire-orange.

### Pillar 4: Authentic D2R Spell Sprite Extraction & Decoupled 1:1 Anchoring (`steamvr/`)
* **Live Spell Sprite Pipeline (`sprites.ssp`):** Authentic Diablo II: Resurrected spell particle textures (`firePlate`, `fireballPlate`, `softFlame`, `flameRamp`) are extracted directly from installed game archives into `d2rloader/plugins/d2r_vr_spell_sprites/sprites.ssp`. Instead of rudimentary procedural fallback dots, the torch renders genuine Diablo II high-resolution volumetric flame textures.
* **Direct Room-Space Hand Anchoring:** In `steamvr/transport.cpp`, `tipPos` and `tipRot` prioritize the physical OpenXR controller pose directly:
  $$\mathbf{P}_{\text{tip}} = \mathbf{P}_{\text{controller}} + \mathbf{R}_{\text{controller}} \cdot \mathbf{Offset}_{\text{shaft}}$$
  Decoupling flame rendering from head orientation eliminates artificial pitch/roll torque when the player tilts their head, ensuring the flame stays strictly attached to the 3D club head.
* **Physical Base Quad Alignment:** In `steamvr/elemental_hands_renderer.cpp`, flame quads are anchored at their base ($\mathbf{P} = \mathbf{P}_{\text{tip}} + \mathbf{up} \cdot \text{size}_y$), so the flame emerges naturally from the club head and rises vertically into the air without clipping down into the handle or the player's hand.
* **Multi-Layer Volume Composition:**
  * **Core Roaring Pillar (Sprite 0):** High-density flame column ($0.075\text{m} \times 0.140\text{m}$) with authentic fire texture scrolling.
  * **Orbiting 3D Swirls (Sprites 1 & 2):** Opposing chiral flame tongues swirling along orthogonal horizontal axes.
  * **Combustion Core (Sprite 4):** Intense white-gold combustion glow right at the club tip.
  * **Buoyant Embers (Sprites 5-7):** Upward-accelerating glowing sparks with vortex draft physics.

### Pillar 5: Directional Normal Shading & Inverse-Square Lighting (`reshade/D2R_DepthFog.fx`)
* **Surface Normal Reconstruction:** In `reshade/D2R_DepthFog.fx`, screen-space derivatives of world-reconstructed depth ($\mathbf{dX} = \text{ddx}(\mathbf{worldP})$, $\mathbf{dY} = \text{ddy}(\mathbf{worldP})$) generate dynamic 3D surface normals $\mathbf{N} = \text{Normalize}(\mathbf{dX} \times \mathbf{dY})$.
* **Lambertian Shading:** Light from the torch tip direction $\mathbf{L} = \text{Normalize}(\mathbf{P}_{\text{torch}} - \mathbf{worldP})$ illuminates surfaces directionally:
  $$I_{\text{diffuse}} = 0.25 + 0.75 \cdot \text{saturate}(\mathbf{N} \cdot \mathbf{L})$$
  Surfaces facing the torch receive full direct illumination while opposing surfaces remain in shadow, delivering a strong 4:1 contrast ratio that immediately reveals the torch's 3D position in the room.
* **Inverse-Square Quadratic Attenuation:** The previous flat radial glow was replaced with realistic distance attenuation:
  $$\text{atten} = \frac{1.0}{1.0 + 0.35 d + 0.04 d^2} \cdot \text{Falloff}(d / R)$$
  Surfaces near the torch (walls, floor, pillars within 1-3 metres) catch a crisp, vibrant focal spot that moves dynamically across the environment as the player waves their arm or rotates their hand.

---

## 4. Edge Cases & Robustness

| Edge Case | Failure Mode | Architectural Solution |
| :--- | :--- | :--- |
| **Weapon Swap ('W' key)** | Player swaps to alternate set (e.g., bows). Light could detach or float. | `gamestate::WeaponSet()` is tracked on every tick; if the active weapon set lacks a qualified club, the emitter immediately deactivates. |
| **Empty Sockets / Non-Fire Gems** | Player equips socketed club with no gems or Sapphire/Emerald/Topaz. | Native stat check `combat::EquippedFireDamage` queries stats 48/49; with 0 fire damage, torch does not ignite. |
| **Tracking Loss / Controller Sleep** | Hand tracking drops or controller disconnects. | Falls back gracefully to `skel::MeleeWeapon` bone pose, then to hero eye/chest anchor. |
| **Enchant Buff Expiration** | Club was ignited solely via Sorceress *Enchant*. | The active state status `Has(16)` is continuously polled; upon expiration, the flame and light decay to zero over 0.5 s. |
| **Two-Handed Weapons** | Two-handed mauls (`whm`) or staves (`stf`) with fire damage. | Excluded by `IsTorchClubCode`; only light, one-handed clubs (`clb`, `spc`) qualify as hand torches. |

---

## 5. Development Scenario & Testing

* The developer scenario system ([steamvr/dev_scenarios.h](file:///c:/Users/Player%201/Desktop/d2r-pcvr/steamvr/dev_scenarios.h)) provides the **"Torch kit"** option (Choice 41) on the **EQUIPMENT** page.
* In isolated testing (`pcvr-dev`), this supplies a level-1 club (`clb`) ready for immediate socketing or melee testing in Blood Moor and the Catacombs.
* Verified with passing CTest test cases (`torch-light-test`, `dev-scenarios-test`, `elemental-hands-test`, `elemental-hands-render-test`).

---

## 6. Known Issues (Pending Investigation)

* **Left-Hand (Off-Hand) Equipment Desync (Discovered during Barbarian Dual-Wield Testing):**
  * When the torch club is equipped in the right-hand slot (primary), everything renders correctly anchored to the weapon head.
  * When the torch is equipped in the left-hand slot (off-hand, e.g. tested on a dual-wielding Barbarian), the character's 3D weapon mesh in VR remains attached to the right hand, while the flame emitter/particles float disconnected in empty space at the coordinates where the right-hand club head would be located. Pending investigation into off-hand weapon bone mapping and dual-wielding attachment indexing in `skel::MeleeWeapon`.
* **Dropped Item Extinguishment (Ground Item Limitation):**
  * Dropping an active torch club onto the ground extinguishes the flame and dynamic lighting. Dropped ground items exist as native static 2D/3D loot props and do not currently sustain an active light emitter or particle volume while resting on the floor.
