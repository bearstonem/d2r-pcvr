# D2RLoader multiplayer: remote VR animations and desktop/VR support

## What this adds

Multiplayer support for D2RLoader, including remote VR hand, arm and weapon animations, VR-to-VR play, and VR-to-flatscreen play. It also adds a native-input combat path for joining clients and launcher support for desktop players.

This is separate from the co-op setup tools in #180.

## Testing

A friend and I have been playing with these changes for about two weeks. I hosted over the internet with port 4000 open, and my friend joined through TCP/IP. We've used both VR-to-VR and VR-to-flatscreen gameplay, including seeing the other VR player's hand and arm movements.

We tested with **D2RLoader 1.3.1beta** and **D2R PCVR 0.3.32** before the latest game update.

The changes have since been split into separate PRs. This multiplayer branch passes:
- Native build and tests: **191/191**.
- Native input/motion checks: **94**.
- Launcher tests: **17**; Battle.net launcher tests: **9**; installer tests: **41**.

Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.

## Still to do

This is a draft based on 0.3.32. It needs updating to current main, resolving the merge conflicts, and another in-game test of the separated branch. Compatibility with game 3.4.95032 hasn't been tested.

The client combat path uses the game's normal inputs, so some custom VR actions may behave differently from the host's direct combat path.
