# Experimental: extended wrist-map zoom

## What this adds

Extends wrist-map image zoom out to **0.5×**, while keeping the original panel and **1×** default. The maximum zoom remains **4×** for the corner minimap and **16×** for full-map mode.

This scales the map image already captured by the mod. It doesn't capture a larger area, and zooming out can expose empty margins.

## Testing

I've tried this in our combined build with **D2R PCVR 0.3.32** and **D2RLoader 1.3.1beta**, but the results have been mixed. I'm keeping it separate and marking it **experimental**.

The separate branch passes **184/184 native tests**, including landscape and portrait zoom-out rendering checks.

Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test`.

## Still to do

Resolve the conflict with current main and do more in-game testing before considering this ready. Compatibility with game 3.4.95032 hasn't been tested.
