# D2RLoader multiplayer: remote VR animations and desktop/VR support

## Features

- VR-to-VR and desktop/VR multiplayer through D2RLoader TCP/IP.
- Remote VR head, arm, hand and weapon poses, including solved two-handed grips. Native lower-body animation is retained with a facing correction.
- Desktop launcher mode and remote-player rendering without starting a headset or enabling VR combat controls for the desktop host.
- Joining-client gestures routed through native skill inputs; the host remains responsible for damage and native validation.
- Host-assisted enemy snapshots for client target selection and outlines. Enemy relation and line of sight use the requesting character, not the host hero.
- Confirmed client melee feedback: the host observes actual native life loss and sends a bounded notification; the client uses its recent physical melee hand for vibration and the existing body-strike response.
- Session/disconnect resets, expiry, bounded chunks, request matching and duplicate protection.

This is separate from the co-op setup tools in #180. **Controller-menu sizing, ally/mercenary health bars and experimental map zoom are excluded.**

## Changes since the original PR

The multiplayer branch is now ported to official PCVR 0.3.33, with joining-client target/outline reporting and confirmed melee feedback from our latest candidate. Hit confirmation checks the actual native life write, not a potentially recomputed damage-record total.

**Both host and client need matching builds:** the target/feedback channel version changed.

## Working and tested

A friend and I have tested VR-to-VR and desktop/VR gameplay, including remote VR hand/arm movements. In our latest combined candidate, **targeting and outlines work for both host and client**. Client hit vibration now triggers correctly, but is noticeably delayed.

Latest gameplay setup: **D2R 3.4.95032 / D2RLoader 1.3.1beta / PCVR 0.3.33**, using the multiplayer launcher. Those gameplay observations came from the combined candidate; the newly separated multiplayer-only branch still needs another headset test.

The standalone multiplayer-only build passes **243/243 tests**. Its diff against official 0.3.33 was checked to exclude the separate health, menu-sizing and experimental-zoom features.

Build: `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.

## Known issues

1. **Movement pauses on client swings**, including swings that hit nothing. Walking briefly stops for the native attack; we do not see this when hosting.
2. **Client hit vibration is delayed.** Confirmation works, but native attack timing and the polling/response delay need investigation.
3. **Apparent automatic movement toward the next target.** The joining character seems to approach another enemy automatically; the cause is not yet established.
4. **Simultaneous two-hand attacks/casts differ from the host**, with a delay before some subsequent actions. Native input is not full host physical-combat parity.
5. **Very-close player overlap remains unresolved.** Desktop/VR testing showed what looked like brief control/pose crossover when players stood very close together. Whether this is visual identity association or actual control is not established; it was not seen in VR/VR testing.
6. **Native hit sounds and visual effects are not confirmed fixed.** The new code drives vibration and body-strike response, not a native sound repair. It uses the generic flesh pattern and can coalesce rapid hits.

This remains a draft based on official 0.3.33. Remaining conflicts with newer main need integration and testing; this is not a claim of current-main compatibility.

## Implementation boundaries

Target/feedback reports use cooperative claimed character IDs checked against live server units. SDK peer handles remain opaque, never native pointers; the claimed character association is not independently authenticated. These reports are read-only, not authorization for damage or other native mutations. Server reads stay on the authoritative game thread and transport sends copied values. Desktop damage/life-write hooks are pass-through observers; full VR combat rules are separately gated.
