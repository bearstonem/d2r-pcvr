# Offline difficulty scaling

**VR Settings > Combat > Offline difficulty** offers **Game default** and
**Players 1** through **Players 8**. Press Apply to save the choice.

Game default leaves Diablo II's existing offline difficulty scaling in charge.
It is the default for both fresh installations and updates. A numbered choice
uses the game's effective player count for scaling; it does not add players or
monsters, change their levels, or switch Normal/Nightmare/Hell.

Higher counts make monsters tougher and increase experience and loot rewards.
Monster health and experience at Players 8 are 4.5 times their Players 1 values;
loot uses the game's native drop rules, not a blanket eightfold multiplier.
Apply changes the input for subsequent native calculations in the current
session. Monster stats are set when the game creates them: enemies already
spawned keep their existing stats, including those created before you reach
them. For all monsters to use the new value, choose **Save and Exit**, then
start a new game with the same character. There is no need to close the game
application or restart VR. The Combat page and hover help explain this limit;
the save confirmation does not promise immediate rescaling of existing enemies.

This works with every class and either melee rules mode. It is independent of
swing/thrust thresholds, hit calculations, shields, bows and spell controls.
The choice lives in the mod's `d2r_pcvr.ini` as `[gameplay] offline_players`:
`0` follows the game; integers `1` through `8` override it. Invalid values fall
back to the game. Calibration, other settings, saves and the desktop game's
`Settings.json` are unchanged. Choosing Game default immediately releases the
override for subsequent native calculations, including later loot evaluation.

The game's own Options screen and `/players` message continue to show its saved
native preference. The VR Combat setting takes precedence while a numbered
choice is selected; use Game default to follow those native controls again.

## Implementation and guards

`vr/offline_difficulty.cpp` adapts the native effective-player-count function
at RVA `0x4251e0` in D2R 3.3.93787. Its complete body is checked, including its
real-player enumeration, game-kind field and native count read. The native
function still runs once for every call. The replacement count is returned only
for VR platform mode, native offline client mode `0`, and the game kinds `1..3`
that the original function permits to use a simulated player count. Native
client mode `5`/`7` (TCP/IP), all other modes, unreadable game memory, Game
default and desktop platform mode retain the original result.

The native `/players` path at `0x188837` checks the client-mode getter
`0x8af90`; its complete bytes are also checked. No native settings writer is
called, and the global `/players` value is never changed. The game's existing
monster scaling and treasure-class formulas consume the overridden count.
The `0x543029` monster-scaling call and `0x440a7d` drop-count call both reach
this accessor; the monster-count helper at `0x509430` also falls back to it.

If a signature or hook is unavailable, native behavior remains and the Combat
page reports that scaling is unavailable. Installation retries after code
decryption. Logs distinguish adapter readiness from observed native uses of
each selected count. Runtime availability updates never discard pending edits.

## Validation

Automated fixtures exercise the actual adapter: all 1..8 selections, all native
defaults, changing back to default, desktop and non-offline exclusions, invalid
game kinds/memory, signature mismatch and hook retry. Settings tests cover
integer validation, endpoint controls, independent persistence, unchanged
preferences and runtime-status updates. The UI preview checks help-text fit.

Headset/gameplay verification remains required:

1. Keep the same offline character, equipment and melee mode. Start a fresh
   Players 1 game and fight ordinary monsters in a chosen area.
2. Apply Players 8, leave and start another game in that area. Compare several
   monsters of the same type; they should take appreciably more damage to kill.
3. Confirm the log records `effective players=8`, then repeat with Players 1.
4. Choose Game default; confirm it follows the game's native setting. Restart
   to check persistence and verify other VR settings/calibration are preserved.
5. Check both melee modes and a bow or spell. The difficulty choice should not
   change gesture recognition, skill assignment or attack controls.

References: Blizzard's [offline scaling announcement](https://news.blizzard.com/en-us/article/23746020/diablo-ii-resurrected-patch-2-3-highlights-coming-soon)
and [monster scaling reference](https://classic.battle.net/diablo2exp/monsters/basics.shtml).
