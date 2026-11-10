# Mercenary and party health bars

## What this adds

Health bars for your mercenary and party members, with **Off / Wrist / Overhead / Both** options under VR Settings → Wrist.

The wrist list sits below the companion strip. Overhead text keeps the same apparent size as you move away. Party entries use character names; the mercenary is currently labelled “Mercenary”.

Party membership and mercenary ownership are checked before displaying a bar. The health updates use their own network channel, separate from VR animations. Missing or stale health data hides the bar.

## Testing

I've tested the mercenary display in our combined build with **D2R PCVR 0.3.32** and **D2RLoader 1.3.1beta**. It works, although the overhead health update is a little delayed.

**Party-member health and mercenary health on a joining client still need testing.** Single-player/host mercenary health uses a matching server-side sample when available; joining clients use client-side data.

The separate branch passes **188/188 native tests**, plus settings page and help-text fit checks.

Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.

## Still to do

Resolve the conflicts with current main and finish the multiplayer tests. Compatibility with game 3.4.95032 hasn't been tested.

The overhead bars can show through scenery, and placement on uneven terrain needs more testing. Portraits and individual mercenary names aren't included. Party entries currently stack vertically on the wrist.
