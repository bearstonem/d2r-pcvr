# TCP/IP co-op setup and experimental diagnostics

Work tracked in [#179](https://github.com/bearstonem/d2r-pcvr/issues/179).
Players have reported hosting, joining and playing together with released
Unleashed builds. Remote VR hand/body movements are not replicated. We have not
yet verified the full host/guest gameplay matrix ourselves, including physical
swings, custom projectiles, shield defense, portals and reconnects.

The **setup assistant works with the normal installed release**. It does not
require a diagnostic DLL or a separate mod. The optional isolated connection
test below remains available for investigating the remaining gameplay questions.
Use local offline characters through D2RLoader TCP/IP, not Battle.net online play.

## Easiest setup: both players

1. Install the same Unleashed release with the official Windows installer.
2. Open **Start > D2R VR Unleashed > Co-op Setup**, or double-click
   `Coop-Setup.cmd` in the extracted package. Until this branch is released,
   extract the small **co-op assistant ZIP** beside your existing installation
   and double-click its `Coop-Setup.cmd`. It reuses the official private Python;
   no system Python, new loader or replacement DLL is needed.
3. Choose **Host** or **Join**, then **LAN**, **Internet** or an already
   configured **private VPN**. Follow the steps for that role. If Multiplayer
   is hidden, the assistant offers to enable it after the game is closed.
4. Use **Save compatibility report** and exchange the small JSON files.
   **Compare a friend report** identifies differences in the game build,
   selected mod, actual DLL/loader hashes, port and shared combat policy.
   Differences show each player's value and where to fix it, such as
   **VR Settings > Combat > Melee rules** or D2RLoader's **game_port**.
   Rendering, calibration and personal controls are excluded. Reports contain
   no local paths, IPs, saved characters or passwords. A matching check code
   means these compared fields match; the loader still checks game data and
   other extensions when joining.
5. Launch the usual Unleashed shortcut, select an offline character, then
   **Multiplayer** beside Play. For the first test, use disposable characters.

The assistant does not launch the game, change port/router/firewall rules,
move saved characters or alter combat/rendering settings. Its only optional
configuration edit enables the loader's Multiplayer button, with an exact backup.

## Host and join

**Same home network:** the host creates a **LAN Only** game. The guest uses
**Direct Join** with the host's LAN IPv4, shown by the assistant. Router
forwarding is unnecessary for the same LAN. Guest Wi-Fi isolation can prevent
two PCs from reaching each other even when they use the same router.

**Across the internet:** the host creates an **Unlisted** game. Let D2RLoader
finish its **Hosting status** check and automatic router mapping first, then
share the Game ID. Use Direct Join with the host's public IPv4 as an alternative.
A LAN address such as `192.168.x.x` is not an internet address. Only the host
normally needs incoming connectivity; the joining player does not need their
own forwarding rule.

If automatic setup cannot make the host reachable:

- Allow `D2RLoader.exe` through Windows Firewall on the network being used.
  Keep the firewall enabled.
- Forward **TCP 4000** on the host's router to **TCP 4000** on the host PC's
  LAN IPv4. If using another `game_port`, use that port on both ends and set
  the same loader port on both PCs. Reserve the host LAN address in the router.
- Two routers can require two forwarding rules. A router WAN address in a
  private range or `100.64.0.0/10` can indicate double NAT or CGNAT; forwarding
  your own router alone may not be enough. Ask the ISP about public IPv4 or
  use a private VPN.

**Optional private VPN:** configure both PCs on the same private network first,
then use **LAN Only / Direct Join** with the host VPN IPv4. Tools such as
Tailscale can avoid manual router forwarding and work through many CGNAT
connections. They are optional, installed separately and not bundled. Their
relay fallback can add latency. This alternative has not been tested with our
assistant in a two-PC D2R session.

## Test the host connection

After the host creates the game, the guest can choose **Test the host address**
in the assistant. Enter an IPv4/name, optionally `address:port`; Game IDs belong
in D2RLoader, not the TCP test. The test opens and closes a TCP connection without
sending game commands. It distinguishes name lookup failure, refusal, timeout
and an unavailable network. A successful TCP test does not prove the loader
accepted the mod or that gameplay works. The loader's Hosting status checks
internet reachability from outside; testing your own public IP locally may fail
on routers without NAT loopback even when an outside guest can connect.

For terminal use:

```powershell
.\Coop-Setup.cmd --game "YOUR GAME FOLDER" --save-report "my-coop-check.json"
.\Coop-Setup.cmd --game "YOUR GAME FOLDER" --compare-report "friend-coop-check.json"
.\Coop-Setup.cmd --game "YOUR GAME FOLDER" --test-host "HOST IPV4"
```

## What still needs verifying

Record **host and guest separately**: physical melee, trigger attacks, bows,
crossbows, hand spells, shield blocks/parries, loot/trading, potions, portals,
death, Save & Exit and reconnect. Record which release/mod each player used.
Basic co-op reports are valuable, but do not establish every custom VR path.
Remote tracked head/hand/weapon replication is a separate feature; normal
network character animation does not transmit VR poses. Pose replication needs
authenticated peer ownership, timestamps/interpolation, per-character skeleton
binding, bandwidth limits and cleanup on disconnect/area changes.

## Optional isolated diagnostic connection test

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

Current build/check results are recorded in the draft PR. The native adapter fixture runs the actual
service callbacks against a synthetic loader: unavailable/short service tables,
solo and non-TCP exclusions, session changes, delayed replies, rule/build
mismatch, authenticated host-thread peer lookup, rejected peers, discarded
callbacks, bounded queues and shutdown. Python fixtures cover isolated staging,
preservation of normal files, malformed settings, comments/BOM/newlines,
concurrent changes, repeat setup and rejection while the game runs.
Assistant fixtures also cover actionable friend-report comparisons, malformed
settings recovery, source-only distribution and the real loopback TCP probe
without any game payload.

Real two-PC verification by the maintainer is pending. Community reports of
basic play do not identify every tested feature or substitute for this matrix.
Passing a protocol fixture is not evidence of synchronized physical combat.

Setup references: [D2RLoader host/join and port guide](https://d2rloader.net/tcp-ip.html),
[Tailscale connectivity and firewall documentation](https://tailscale.com/kb/1082/firewall-ports).
