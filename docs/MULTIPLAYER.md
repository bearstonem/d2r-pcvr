# Experimental TCP/IP co-op

Work tracked in [#179](https://github.com/bearstonem/d2r-pcvr/issues/179).
This branch is a **connection diagnostic prototype**, not a release of working
physical multiplayer combat. Both testers use the same Unleashed candidate.
Use local offline test characters, separate from the normal save list.

## First connection test

The local **co-op connection test kit ZIP** contains `Setup-Coop-Test.cmd`.
Extract the same kit on both PCs and double-click that file after closing
D2R/D2RLoader. It performs the staging and button-enabling steps below, using
the official installation's private Python. No system Python is needed. The
kit contains the test DLL, source helpers, documentation and license notices;
each PC prepares its own local assets from its existing installation.

1. Install the same current Unleashed release on both PCs. The game builds,
   loader and active mod must match. Do not install another VR mod alongside
   Unleashed in the same plugin folder.
2. Build this branch, or use its matching test DLL on both PCs. Run the helper
   below from this checkout while D2R and D2RLoader are closed. It creates a
   separate `pcvr-coop` mod, copies locally prepared assets from that PC's own
   Unleashed installation, preserves calibration and uses a new character list.
   No game assets or saves are distributed with the candidate DLL.

   ```powershell
   .\deps\python\python.exe steamvr/coop_setup.py --game "YOUR GAME FOLDER" --stage-test --plugin build-frame/windows-developer/d2rl-pcvr.dll --enable-tcpip
   ```

3. On Steam, use the kit's **Launch-Coop-SteamVR.cmd** or
   **Launch-Coop-VDXR.cmd**. Alternatively, in the shared launcher select
   **pcvr-coop** under Other mods and choose the usual VR runtime. The shared
   launcher is optional. Do not select `D2R-PCVR` or `pcvr-dev` for this test.
   Launch manually and create a disposable offline character on each PC.
4. Open **Multiplayer** beside Play. Use **LAN Only** if both PCs are on the
   same local network. Otherwise use **Unlisted** and share the Game ID.
   The guest uses Direct Join or the supplied ID. Let the loader report hosting
   reachability; the helper makes no router or firewall changes.
5. Set both PCs to the same Melee rules. This prototype also compares
   `[gameplay] contact_hits` and `enemies_always_hit`. Personal calibration,
   rendering, hand bindings and aim settings need not match.
6. Confirm both players can join, see each other, move, interact and leave.
   Save and Exit, reconnect and check the new character list persists. Do not
   use successful connection as evidence that physical attacks are synchronized.
7. Collect `mods/pcvr-coop/d2rloader/logs/d2r-vr-frame.log` from each PC, noting
   which PC hosted. The guest should log a matching host handshake; the host
   should log an authenticated peer received on its game thread. Try mismatched
   Melee rules, then restore matching rules and check recovery.

`Coop-Setup.cmd` enables the loader's Multiplayer button for an existing
installation. The Python helper's `--json` option produces a compatibility
report; `--mod pcvr-coop` selects the staged candidate. Compare the DLL hashes
on both PCs. Do not send saved characters or extracted asset caches.

The opt-in button setting is global to D2RLoader. The helper changes only
`[d2rloader] show_tcpip_button`, preserving other values/comments and keeping an
exact `.pcvr-coop-<timestamp>.bak` copy next to the TOML. It does not enable
public hosting, alter the port, disable security, change extension loading,
download another loader or launch the game. Turn the button off in D2RLoader
General settings if it is no longer wanted.

## Audit: work still required

- **Physical melee and breakables:** `vr/melee.cpp` queues contact resolution
  with `runOnGameThread` and resolves only `combat::LocalPlayerId`. On a guest,
  the SDK returns `Unavailable` because there is no local authoritative game.
  The host needs authenticated per-player gesture requests and independent
  cadence, hit-set, equipment and damage context for each player.
- **Bows, crossbows and hand spells:** `vr/bow.cpp` similarly requires
  `melee::LocalServer`. Missile ownership, mana/ammo costs, free-aim direction,
  homing and client visual binding must be routed and replicated by the host.
  Host-only custom missile transforms are insufficient for the guest's view.
- **Shield block/parry and incoming defense:** current guard, riposte and local
  identity state is process-wide. Remote shields must be evaluated against the
  correct attacker and defender, with per-player state and feedback returned
  only to the owner. Reusing the host's shield pose would be incorrect.
- **Potion throws, hirelings and quick items:** audit local-server assumptions
  and identify each player's hireling. The nearest friendly hireling is not
  necessarily the thrower's hireling in co-op.
- **UI and tracking:** verify lobby accessibility in the headset, remote
  character visibility, loading transitions, portals, death, reconnects and
  neutral interaction controls on both PCs.
- **Difficulty:** the existing Players 1-8 adapter explicitly excludes native
  TCP/IP modes 5 and 7. Keep native multiplayer scaling in charge.
- **Distribution:** first agree on a verified action protocol and build/rules
  compatibility before advertising multiplayer or adding it to normal setup.

The current channel only exchanges bounded, versioned diagnostic messages.
It has no damage, item, position-edit or arbitrary-command messages. The host
uses the SDK's `getPeerPlayer` on its game thread to authenticate a connected
peer; it never treats the peer handle or the guest's session nonce as a native
player pointer. No opaque player handle crosses the network. Delayed replies
from previous sessions expire, and queued peer callbacks are bounded.

References: [D2RLoader TCP/IP guide](https://d2rloader.net/tcp-ip.html),
[PluginSDK network API](https://github.com/D2RLoader/PluginSDK/blob/main/include/D2RLPlugin/network.h),
[thread ownership](https://github.com/D2RLoader/PluginSDK/blob/main/include/D2RLPlugin/threads.h).

## Automated verification

The initial candidate passed 112 native checks and 227 Python checks, with one
unavailable-symlink skip on Windows. The native adapter fixture runs the actual
service callbacks against a synthetic loader: unavailable/short service tables,
solo and non-TCP exclusions, session changes, delayed replies, rule/build
mismatch, authenticated host-thread peer lookup, rejected peers, discarded
callbacks, bounded queues and shutdown. Python fixtures cover isolated staging,
preservation of normal files, malformed settings, comments/BOM/newlines,
concurrent changes, repeat setup and rejection while the game runs.

Real two-PC, VR lobby and native gameplay checks are pending. Passing a protocol
fixture is not evidence of synchronized physical combat.
