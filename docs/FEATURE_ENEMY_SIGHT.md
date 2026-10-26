# Feature: Enemy Sight Distance (`enemy_sight_pct`)

## 1. Functional Overview

This feature adds a real-time slider to the in-game menu under **VR Settings -> Combat -> Enemy sight distance** (persisted in `d2r_pcvr.ini` under `[gameplay] enemy_sight_pct`) that scales the distance at which hostile monsters detect, wake up, and engage the player in **Offline VR mode**:

- **Range:** `100%` (native baseline game value) to `300%` (three times native distance), in steps of `10%`.
- **Default value:** `100% (Default)`, which preserves the exact unmodded behavior of *Diablo II: Resurrected*.
- **Lossless Restore:** When resetting to `100%`, switching to desktop mode (`platform=0`), exiting an offline game (`mode() != 0`), or unloading the plugin, the adapter restores the exact original in-memory table bytes (including zeros `0`, which signal the engine to use its default `35` sub-tile radius).
- **Safety exclusions:** Town NPCs (`inTown` / `npc`), mercenaries, player summons, allies (`Align != 0`), and invulnerable units (`!killable`) are never modified.

---

## 2. Architecture and Reverse Engineering (`D2R 3.3.93787`)

### 2.1 Direct access to the live `MonStats` buffer
By auditing `D2RCore.dll` (D2RLoader 1.3.1, function `DataTableService::getTable` at `0xc0de540b8d0`), we verified that calling:
```cpp
tables->getTable(ctx, bank, D2RL::DataTables::TableId::MonStats, &view);
```
on the UI/game thread (`ThreadService::runOnUiThread`) for `Bank::Classic`, `Bank::Lod`, and `Bank::Rotw` invokes `GetDataTables(bank)` (`RVA 0x300a90` in `D2R.exe`) and returns in `view.rows` a **direct pointer to the heap array** (`*(sgptDataTables + 0xF58)`) read by the engine's AI state machine on every tick.

### 2.2 Binary Layout of `MonStats` (`rowSize = 508` bytes = `0x1FC`)
Verified against `monstats.txt` / `monstats.bin` and `base/monstats.txt` / `base/monstats.bin` (`751` rows across banks):

| Offset | Type | Field in `monstats.txt` | Description |
| :--- | :--- | :--- | :--- |
| `0x00` | `uint16_t` | `hcIdx` | Row index (`0..750`). |
| `0x3D` | `uint8_t` | `flags1` | Bit 0 (`0x01`): `npc`, Bit 2 (`0x04`): `inTown`, Bit 7 (`0x80`): `killable`. |
| `0x3F` | `uint8_t` | `flags3` | Bit 1 (`0x02`): `enabled`. |
| `0x52` | `uint16_t` | `AI` | Index into `monai.txt` (`TableId::MonAi = 15`). |
| `0x87` | `uint8_t` | `Align` | `0` = hostile, `1` = ally/mercenary/summon, `2` = neutral. |
| `0x89` | `uint8_t` | `threat` | Threat priority level (`10` for skeletons/zombies, `14` for bosses). |
| `0x8A..0x8C` | `uint8_t[3]` | `aidel`, `aidel(N)`, `aidel(H)` | Delay between AI decision ticks in Normal, Nightmare, and Hell. |
| `0x8D..0x8F` | `uint8_t[3]` | **`aidist`, `aidist(N)`, `aidist(H)`** | **AI activation/sight radius in sub-tiles** (`0` = engine default `35` sub-tiles). |
| `0x90..0xBF` | `int16_t[8][3]` | `aip1..aip8` (N / NM / H) | 8 AI parameters per difficulty (`6` bytes per `aip`: `+0` Normal, `+2` Nightmare, `+4` Hell). |

### 2.3 AI State Machines (`AiThink.cpp` / `AiUtil.cpp` / `Path.cpp`) & Parameter Optimizations
Auditing the 143 `AITHINK_Fn*` functions and engine target-acquisition helpers (`sub_6FCF2110`, `sub_6FCF2CC0`, `PATH_SetStepNum`, `AITACTICS_MoveInRadiusToTarget`) identified and resolved specific state machine bottlenecks when `aidist` is scaled:

1. **Approach/Activation Gates Smaller Than `aidist` (`Fallen`, `Zombie`, `Mummy`, etc.):**
   - In `monai.txt`, Blizzard mislabeled `aip2` for `Fallen` (`AI 6`) as `"Non-command Wander behavior (% Chance)"`, whereas in `AITHINK_Fn006_Fallen` `aip2` is `FALLEN_AI_PARAM_APPROACH_DISTANCE` (`nTargetDistance <= aip2`).
   - In `monstats.txt`, `fallen1` has `aidist = 35` but `aip2 = 10` sub-tiles (like `zombie1` `AI 3` with `aip2 = 10` and `mummy1` `AI 21` with `aip1 = 5`). As a result, when awakened at long range, units with `nTargetDistance > aip2` wandered randomly in place instead of pursuing the player.
   - **Solution (`ScaleGateAip`):** For archetypes with approach/activation gates (`AI 3 Zombie`, `AI 6 Fallen`, `AI 14 QuillRat`, `AI 21 Mummy`, `AI 28 Vampire`, `AI 46 ElementalBeast`, `AI 52 FrogDemon`, `AI 56/57 Tentacle`, `AI 70 FingerMage`, `AI 73 AbyssKnight`, `AI 82/83 Spawner`, `AI 85 HighPriest`, `AI 94 DesertTurret`, `AI 121 MinionSpawner`), when `pct > 100%` the gate parameter is scaled to at least `scaledAiDist + 1`, ensuring awakened monsters pursue the player immediately.

2. **Rogue Archers (`CorruptArcher`, `AI 35` — Dark Ranger, Vile Archer, etc.):**
   - **Line-of-Sight limit in `sub_6FCF2CC0` (`<= 48` sub-tiles):** `AITHINK_Fn035_CorruptArcher` calls `sub_6FCF2CC0` on its first line; this helper has a hardcoded cutoff `if (nDistance > 48) return nullptr;`. If `aidist > 49` wakes an archer at `49..54` sub-tiles, `pTarget` is `nullptr` and the archer idles or circles aimlessly. Therefore, `ScaleAiDistForArchetype` caps `aidist` for `AI 35` at `49` (`nDistance <= 48`, and `46` for `BloodRaven` `AI 59`).
   - **Slow-walk hijack via `aip8` (`WALK_TOW_DISTANCE = 12`) and `PATH_SetStepNum` ceiling (`< 20`):** In `AITHINK_Fn035_CorruptArcher`, `if (aip8 > 0 && nDistance > aip8)` is checked **before** `if (nDistance > aip5)` (`ALWAYS_RUN_DISTANCE = 20`) and before shooting, forcing a `+10%` velocity slow-walk from `48` down to `11` sub-tiles without firing. Furthermore, `PATH_SetStepNum(pDynamicPath, nSteps)` requires `nSteps < 20` (`PATH_MAX_STEPNUM = 20`); passing `aip5 = 20` reset `nStepNum = 0`.
   - **Solution:** When `pct > 100%`, `aip8` is set to `0` (disabling the slow-walk hijack) and `aip5` is set to `17` (`nStepNum = 16`, within `cr_arrow1..5` missile range), making Rogue archers run at `+100%` velocity to `16` sub-tiles, stop, and immediately fire arrows.

3. **Returned Archers (`SkeletonBow`, `AI 37` — Returned Archer, Burning Dead Archer, Horror Archer):**
   - **Hardcoded `< 20` shooting limit & pathfinding stalls:** In `AITHINK_Fn037_SkeletonBow`, bow attacks are gated behind `nDistance < 20`. Beyond 20 sub-tiles, the archer rolls `aip3` (`50%`): half the ticks it freezes for `20` frames (`0.8s`) in `IdleInNeutralMode`, and the other half it walks only `aip4 = 5` steps at `Velocity = 4` (`2.5` sub-tiles/sec), taking 14–17 seconds of slow shambling to reach shooting distance.
   - **Pathfinding failure in `AITACTICS_MoveInRadiusToTarget` with large step counts:** If `aip4` was set to `14`, the engine's diagonal step offset search (`while (nXOffset + nYOffset < nDiff)`) produced Euclidean distances exceeding `nDistMax = 14` (`Path.cpp`), causing `D2GAME_ModeChange_6FC65220` to fail (`return 0`) and leaving the archer completely petrified in place.
   - **Solution:** `aidist` is capped at `49` (`RequiresLosAiDist`), `aip1 = 100` (`SHOOT_CHANCE_PCT`, immediate shooting on entering `< 20`), `aip2 = 4` (`STALL_DURATION`, low recovery freeze), `aip3 = 100` (`APPROACH_CHANCE_PCT`, eliminates the 50% 20-frame idle freeze for continuous movement), `aip4 = 6` (`WALK_STEPS`, safe path steps well within `nDistMax = 14` that never fail route generation), and `aip5 = 16` (`TARGET_DISTANCE`, guides archers steadily into bow range).

4. **Returned Mages (`SkeletonMage`, `AI 64` — Returned Mage, Burning Dead Mage, Horror Mage):**
   - **Slow-walk hijack in `AITHINK_Fn064_SkeletonMage`:** In Blizzard's original code, `if (nDistance > aip2 && Roll < aip3)` is evaluated **before** the shooting check `if (nDistance < aip6 && Roll < aip1)`. In `monstats.txt`, `aip2` (`APPROACH_DISTANCE`) is only `9` sub-tiles and `aip3` is `30%`. If `aip3` was set to `100%`, the mage was **forbidden from ever shooting** while beyond 9 sub-tiles, crawling at snail speed (`AITACTICS_SetVelocity(pUnit, 0, 10, 0)`, `+10%` over `Vel = 4`) into point-blank range.
   - **Spell distance gate `aip6` (`FIRE_DIST = 18`):** A Skeleton Mage never casts spells beyond `aip6` sub-tiles; beyond that distance, it idles or circles aimlessly.
   - **Solution:** `aidist` is capped at `49` (`RequiresLosAiDist`), `aip3 = 0` (disables the slow-walk hijack completely), `aip6` (`FIRE_DIST`) is scaled up to `MaxLosRange = 48` (placing the player inside spell range as soon as the mage awakens), `aip1 = 100` (`SHOOT_CHANCE_PCT`, top priority for elemental bolt casting), `aip2 = 14`, `aip7 = 0` (eliminates erratic circling), and `aip8 = 3` (reduces neutral stall frames).

5. **Full Ranged & Caster Archetype Audit (107 Hostile AIs):**
   - **Global Line-of-Sight Acquisition Ceiling (`RequiresLosAiDist = 49`):** 17 archetypes (`Bighead 4`, `FallenShaman 13`, `SandMaggot 15`, `GreaterMummy 22`, `Vampire 28`, `CorruptArcher 35`, `SkeletonBow 37`, `Summoner 53`, `TentacleHead 57`, `BloodRaven 59`, `SkeletonMage 64`, `OblivionKnight 74`, `PantherJavelin 95`, `FetishBlowgun 96`, `Succubus 118`, `SuccubusWitch 119`, `Imp 122`) utilize `sub_6FCF2CC0`, which strictly rejects distances `> 48` sub-tiles (`return nullptr`). Capping their `aidist` at `49` (`nDistance <= 48`) guarantees they always obtain a valid target upon waking up and never get stuck dazed in the 49..54 sub-tile dead zone.
   - **Specific Caster and Ranged Unit Tuning:**
     - `AI 14 QuillRat`: added to `GateCoversAiDist` so `aip1` (`ACTIVATE_DISTANCE`) covers the scaled sight distance and immediately unleashes spike volleys.
     - `AI 63 GargoyleTrap`: added `aip1` (`SHOOT_DISTANCE`) to `AwarenessAipIndex`.
     - `AI 70 FingerMage`: scales `aip5` (`CAST_RANGE`) up to `48` alongside `aip7` (`OUT_OF_RANGE`).
     - `AI 74 OblivionKnight`: scales `aip2` (`ENGAGE_RANGE`) up to `48` to cast curses and Bone Spirit from distance.
     - `AI 95 PantherJavelin`: scales `aip6` (`THROW_DISTANCE`) up to `48` and sets `aip1 = 100` (`THROW_CHANCE_PCT`), taking advantage of the javelin's `50` sub-tile flight range (`Vel = 20, Range = 80`).
     - `AI 96 FetishBlowgun`: scales `aip1` (`STAY_IN_RANGE`) up to `48`, firing blowgun darts (range: `56` sub-tiles) from long distance.

---

## 3. Implemented and Modified Files

- [`steamvr/enemy_sight.h`](../steamvr/enemy_sight.h): Core logic (`framevr::enemysight`), normalization (`100%..300%`), structural layout probes (`skeleton1`, `zombie1`, `mephisto`), hostile monster filtering, baseline capture (`CaptureBaseline`), and table scaling (`ApplySightPercent`).
- [`vr/enemy_sight.h`](../vr/enemy_sight.h) and [`vr/enemy_sight.cpp`](../vr/enemy_sight.cpp): Native D2RLoader adapter verifying `ModeRva` (`0x8af90`, `mode() == 0`) and synchronizing `Classic`, `Lod`, and `Rotw` banks on the UI thread via `framevr::CallbackTicket` and memory read/write helpers.
- [`steamvr/vr_settings.h`](../steamvr/vr_settings.h), [`steamvr/vr_settings_ui.cpp`](../steamvr/vr_settings_ui.cpp), [`steamvr/vr_settings_help.h`](../steamvr/vr_settings_help.h): Key `EnemySight` (`[gameplay] enemy_sight_pct`), row 9 in the `Combat` tab, and contextual help card.
- [`vr/vrcam.cpp`](../vr/vrcam.cpp) and [`d2r_pcvr.ini`](../d2r_pcvr.ini): Lifecycle integration (`Init`, `Configure`, `Install`, `Tick`, `Report`, `Shutdown`).
- [`steamvr/enemy_sight_test.cpp`](../steamvr/enemy_sight_test.cpp): Unit and integration tests with synthetic D2RLoader service mocks.

