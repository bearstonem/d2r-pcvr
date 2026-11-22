# D2RLoader multiplayer on PCVR 0.3.35

## What is fixed

**Joining VR clients now use physical swing detection with instant, consistent hit registration instead of simulated attack-button presses.** Contacts are forwarded to the host and resolved through the existing PCVR combat path for the joining character. The host remains responsible for damage.

I installed and tested the complete **PCVR 0.3.35 multiplayer package with a flatscreen host and a VR client**. I landed hits with both hands: each controller gave the correct feedback and the hits registered immediately.

## Included features

- D2RLoader TCP/IP multiplayer, with desktop/VR and VR/VR support.
- Remote VR head, arm, hand and weapon poses, including solved two-handed grips, while retaining native lower-body animation.
- Desktop launcher mode and remote-player rendering without requiring a headset on the desktop host.
- Host-assisted client targeting and outlines, using the requesting player's enemy relation and line of sight.
- Physical joining-client swing forwarding through the existing PCVR detector/resolver, with per-player combat state rather than a replacement combat system.
- Wrist/controller menus and adjustable controller-menu sizing.
- Ally/mercenary health bars, with wrist and overhead display options.
- Client loot beams through host-provided, bounded loot reports.
- Immediate local contact feedback with host-confirmed feedback routed to the correct hand.
- Laser selection and VR keyboard input for the multiplayer panel, building on #588.

This update brings the previously separated menu-sizing and health features into the combined multiplayer PR. Physical swing forwarding is automatic; no additional multiplayer settings were introduced for this update. This remains separate from the co-op setup tools in #180.

## Upstream and verification

- Integrated with **PCVR 0.3.35**, upstream commit `1217d771a3d3eebf7fd693c89c6f63c6af1c4e57` (`v0.3.35-windows-preview`).
- Clean upstream build: **238/238 automated tests passed**.
- Combined multiplayer build: **268/268 automated tests passed**.
- In the headset, I tested Host Game, Browse search, Direct Join and joining a listed password-protected game. Backspace and re-entry worked in the password prompt. The keyboard dismissed with B/Close and when selecting Unlisted or LAN Only. I also confirmed typing worked after opening Multiplayer from both controller and keyboard/mouse input modes.
- The complete package passed the upstream setup's check-only verification before the gameplay test.
- **Live-tested configuration for this update: flatscreen host + VR client, using the complete 0.3.35 installation.** Earlier versions were also tested in VR/VR, but that configuration has not yet been revalidated on this exact integration.
- The working swing code was preserved during the upstream port. Source and tests in this update match the tested integration; contribution documentation has been refreshed separately.

Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

**Both host and client need matching multiplayer builds.** Keep the matching 0.3.35 base files; a DLL-only update does not upgrade older external assets.

## Remaining limitations

- VR-host testing and exhaustive class/form, dual-hand, shield and motion-skill coverage remain to be completed on this exact build.
- Earlier reports involving close-player overlap, movement/target approach, two-hand timing, and native hit sound/VFX have not been individually revalidated in this latest test; this update does not claim separate fixes for them.
- Allow the initial connection handshake to complete before testing swings. Rapid reconnect state remains a validation gap; restart both games between test sessions.

## Implementation boundaries

Authoritative combat resolution and server reads run on the host game thread; network messages carry copied values. SDK peer handles remain opaque and are never cast to native pointers. Peer-to-character association still uses cooperative claimed character IDs checked against live server state, not independently authenticated character ownership; this is not a hardened protocol for untrusted clients.

Target, health and loot reports remain separate from combat requests. Desktop native behavior is preserved outside the scoped remote combat operation. Local contact prediction drives feedback only, not damage authorization. Existing upstream licensing and maintainer contributions are retained.
