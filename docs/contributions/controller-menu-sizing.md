# Adjustable controller-menu size

## What this adds

A **Controller menu size** slider under VR Settings → Wrist. It adjusts both stick-click menus from **50% to 200%**, with **100%** keeping the original size.

The buttons, pointer hit areas and map tab scale together. The setting is saved between sessions. It doesn't resize the wrist map.

## Testing

I've used the menu sizing in our combined build based on **D2R PCVR 0.3.32**, with **D2RLoader 1.3.1beta**.

The separate branch passes **182/182 native tests**, including settings persistence and pointer hit-area checks. Settings page and help-text fit checks also pass.

Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.

## Still to do

Resolve the conflicts with current main and retest the separate branch in-game. This remains a draft; compatibility with game 3.4.95032 hasn't been tested.
