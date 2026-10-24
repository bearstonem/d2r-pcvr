# Independent VR render resolution

Open **VR Settings > Performance > Render resolution**, choose a size, and
press **Apply**. Applying rebuilds the VR session and closes the panel; allow
several seconds for the view to return. Reopen Performance to see the actual
**VR source** dimensions per eye.

| Choice | Source dimensions | INI value |
|---|---|---|
| Desktop | Physical game-window client size | 0 |
| Headset (default) | OpenXR runtime recommendation | 1 |
| 1536 × 1536 | Fixed square preset | 2 |
| 2048 × 2048 | Fixed square preset | 3 |
| 2560 × 2560 | Fixed square preset | 4 |
| 3072 × 3072 | Fixed square preset | 5 |
| 4096 × 4096 | Fixed square preset | 6 |

The preference is `[render] vr_resolution` in `mods/D2R-PCVR/d2rloader/plugins/d2r_pcvr.ini`.
Missing or invalid values use Headset. Start with a moderate preset and
increase only if frame timing permits. Independent sizes respect both eyes'
OpenXR limits and the system's swapchain limits, with an additional cap of
4096 pixels per side. Headset preserves the runtime's recommended aspect ratio.
The recommendation can change with runtime quality settings and is not
necessarily the headset panel's physical pixel count.

No custom monitor mode, EDID change, virtual monitor, or Settings.json edit is
needed. The physical window stays its existing size; the desktop mirror may
therefore appear stretched. Returning to Desktop restores the physical client
size. The override also releases on normal window close or XR session stop.
The saved preference does not change unmodded desktop play.

Applying the game's native graphics options now queues a fresh request for
the selected VR resolution, even when its dimensions have not changed. This
addresses the reported broken view after native Apply that previously needed
a manual VR resolution change. The request waits at least 500 ms after the
last `OptionsPanelMessage/AcceptChanges`, then posts to the window thread;
repeated Apply events coalesce. It retains pointer coordinates and physical
window size. Desktop mode and XR shutdown cancel pending recovery. The mod
does not listen to its own `ResolutionChanged` notifications for recovery,
avoiding a resize loop. Automated window/message fixtures pass; the actual
native graphics Apply sequence still needs an offline headset comparison.

## What changes

Previously, the native OpenXR path copied the game's desktop-sized backbuffer
into equally sized eye textures. Raising compositor resolution could resample
that image, but could not supply detail missing from the game's source.

This setting requests a logical size through the game's own `WM_SIZE` path,
dispatched on the window thread. D2R recreates its native render resources;
ReShade's resize notifications recreate our OpenXR eye textures at that size.
The source-to-eye transfer remains a same-size copy. ReShade shaders are not
used to synthesize a larger scene. Native `ScreenToClient` and `ClientToScreen`
calls originating from the game executable are remapped for the game window,
so the game's cursor coordinates follow the resized image. Other windows and
the mod's physical desktop laser positioning retain their original coordinates.

D2R's resolution scale, dynamic resolution and DLSS still affect internal scene
detail. The displayed source size describes the final game output per eye; it
does not imply that every intermediate pass renders at that resolution.
Streaming bitrate, compression, lens correction and headset optics still
affect perceived clarity. Higher source sizes cost GPU time and memory.

In native VR gameplay, the mod hides the game's `HUDPanel` on its UI thread
as well as suppressing the bottom toolbar draw. This covers controller HUD
layouts that place the bar away from the bottom at square render sizes.
The panel stays hidden throughout the XR session, including native menus,
loading and gaps between eye draws; tracked wrist/palm displays remain.
Native panel messages reapply the override immediately and queue a coalesced
UI-thread refresh after the native handler. This prevents menu transitions
from deliberately restoring the toolbar or waiting for the slow state poll.
Stopping XR releases the override; an intentional resolution rebuild retains it.

The added sky/fog reads a private copy of the scene depth. Square render sizes
can match a shadow map's dimensions, so selecting depth by size alone can put
sky or fog across nearby scenery. The private channel requires a matching
color target, a current-eye binding and a known resource state. It clears
stale/destroyed resources and suppresses depth effects if no suitable source
is available. Desktop rendering retains ReShade's normal depth channel.
Update both `d2rl-pcvr.dll` and `reshade-shaders/Shaders/D2R_DepthFog.fx`
together when testing this fix.

Native inventory/character workspaces and the main menu fit inside their
original widescreen-sized VR surfaces. Square and portrait render targets
are uniformly reduced to that height limit instead of producing a much taller
panel. The complete native image and its aspect ratio are preserved, and the
laser uses the same fitted dimensions as the displayed surface. This changes
only menu presentation, not world rendering or the handheld quest journal.
Inventory workspaces sit 0.55 m away and the main menu 0.70 m away. They are
placed upright at the current eye height when opened, independent of head
pitch. The controller's tabbed inventory/character/options view occupies the
upper widescreen band of a tall canvas; that visible band is centered on the
eye-height anchor without cropping the remaining canvas. Rendering and laser
targeting share this offset. Other layouts retain their full-canvas center.
Automated panel tests cover square, portrait and wide sources at both menu
sizes, including resolution-independent sizing, seated/standing eye-height
placement and laser-to-pixel mapping. The owner found the first fitted-height
candidate too small/far away; closer placement and visible-band centering
await a fresh headset comparison.

NPC conversations retain the complete captured native canvas on Frame, Index
and Touch controllers. The widget-based crop and its separate four-times
magnification were removed after the owner saw only the right edge of the
choices at 3072 square. The SDK's parent-local widget bounds cannot safely be
treated as texture coordinates after a resize; a plausible in-bounds rectangle
can still sample the wrong pixels. The complete canvas uses the existing
uniformly fitted workspace and menu-distance setting. Both NPC choices and
scrolling speech use the same upper-band offset, and changing between them
keeps the room-space anchor while rearming pointer input.

The laser uses that same fitted surface, offset and full source region.
Regression coverage exercises both edges and interior choices at 1536, 3072
and 4096 square, widescreen and portrait sizes, including a 3840x2160 desktop
with a differently sized VR source. Sky/world visibility and merchant/journal
policies remain independently controlled. Headset visibility and readability
of the complete NPC surface still require owner verification.

The native lighting depth reduction also assumes square tiles when calculating
the row for a linear sample index. Its 32x20 grid has rectangular tiles at
square VR resolutions: the height divisor misses lower rows and can reject
lights in hard rectangular patches. An independently written replacement
uses the tile width for both coordinate calculations, preserving the native
depth/sampler/constant/output layout and 2x2 gather footprint. Only the exact
verified `light_bin_depth_bounds_c` shader in game build 3.3.93787 is matched.
The replacement binds only during native VR eye rendering; other shaders and
desktop rendering retain their original pipelines. No native shader bytecode
is distributed. Failure to compile or create the replacement retains native
lighting. Logs report both a matching pipeline and actual VR activation.

## Validation and remaining headset checks

The native UI's client-size query also needs the applied VR dimensions. In
3.3.93787, `0x6545d0` reads the physical client independently of `WM_SIZE`;
UI root/safe-area sizing, layout globals and cursor bounds consume that value.
With a 3840-wide desktop and a 3072-wide VR source, the character-selection
list and outer inventory tabs can therefore fall outside the rendered texture.
A hook guarded by the complete 48-byte query and seven UI call sites returns
the applied VR width and a fitting widescreen height only to UI root/layout
and cursor consumers. For example, a 3072-square world uses a 3072 x 1728 UI
band at the top of its full-size texture. Feeding the full square height to
the native UI triggered a text-rendering crash during startup in the first
candidate; the fitted band also keeps font sizing in the native layout's
aspect. The world source and laser-to-source coordinates remain full-size.
Movie and other engine consumers retain their original dimensions. Before
resize, after XR shutdown and in Desktop mode,
it returns the original result. No global `GetClientRect` override is used.
If the hook installs after the first resize, it queues one reflow on the window
thread. Unsupported code retains the original query. The real-window fixture
checks square/portrait sizes, pointer agreement, deferred reflow and restoration;
the guards match a local 3.3.93787 code snapshot. The final combined candidate
passed all 57 automated checks and launched with 3072-square SteamVR eye
textures. A desktop capture shows the complete character-selection list and
both outer menu edges; the process continued rendering normally. This probe
used the native skip-logo-video launch option and did not enter a character.
Headset edge visibility, inventory tabs, merchant/tooltips and laser selection
still require testing with this candidate.

Automated Windows fixtures cover runtime limits, invalid choices, asynchronous
window resize/coalescing, physical-window preservation, desktop restoration,
setting persistence, and real user32 pointer conversion hooks including inverse
cursor warps and unrelated windows.

The October 13 Steam/SteamVR title-screen probe recreated the native source,
a depth target and OpenXR eye textures at 2560 × 2560 while the physical client
remained 3840 × 2160. Selecting Headset live used the runtime's 2160 × 2160
recommendation. The 4096 × 4096 preset also recreated matching depth and eye
textures with a 1280 × 1024 physical client, and Desktop restored 1280 × 1024
eye textures. No character was entered. This verifies resource allocation
and session rebuilding, not in-headset clarity or gameplay performance.

Pending offline headset checks: compare clarity and GPU frame time at a fixed
scene, test both eye images and aspect, click native menu edges/corners and
inventory slots, check controller laser alignment, apply and cancel choices,
relaunch, resize/minimize the desktop window, and reconnect the headset.
Validate VDXR separately. Keep the old DLL available for comparison.

A subsequent offline level-3 Sorceress test in Rogue Encampment used the Steam
3.3.93787 build, SteamVR and 4096 square rendering at native resolution scale
100%, DLSS off. The owner confirmed the HUD was gone and the fog result was
better. The recording still shows reported roof shading flicker during head
movement, even with added sky/fog off, ambient occlusion off, and shadows low.
The owner confirmed the rectangular-tile correction worked in the same cart
head-turn test. The first candidate did not activate because the observer
registered after the engine created its lighting pipelines. Registration now
runs immediately after loading ReShade, before renderer startup; runtime logs
confirmed the exact shader match and its use during VR eye rendering before
the successful comparison. The owner then re-enabled Added Fog and Added Sky
and confirmed both work with the correction. Other acts, runtimes and graphics configurations
still need broader visual testing.
Its compiled compute shader passes a software GPU test against a full-pixel
reference scan, including exact 4096 square, wide views, empty tiles, extrema
near lower tile edges, atlas scale/offset and padded output stride.
The agent inspected recordings
and desktop output, and observed HUDPanel release when inventory opened;
this is not agent headset validation. Automated coverage additionally checks
panel visibility/expiry and scene-depth selection, scaling and lifetimes.

Diagnostics in `d2rloader/logs/d2r-vr-frame.log` report the requested, source,
physical and recommended dimensions; matching depth allocations; and the
OpenXR session's actual eye size. A missing pointer hook or unavailable runtime
limits retains the desktop path rather than enabling misaligned input.

Native mouse messages also need conversion: `WM_MOUSEMOVE` and client-coordinate
button events bypass `ScreenToClient`. They now use the same applied render
dimensions as polled cursor positions. This prevents the laser's hover/click
coordinates from remaining at desktop resolution after a VR resize. The mapping
updates on the window thread when the resize is applied, including live preset
changes; queued sizes are not used prematurely. Wheel messages retain their
screen coordinates, captured drags retain signed coordinates outside the window,
and other windows/Desktop mode remain native.

The real-window pointer fixture checks event/poll agreement at center and edge
targets and during an outside drag at 1536, 3072, 4096 and portrait dimensions,
then checks Desktop restoration. All 56 focused native checks passed. Quest 3
with Virtual Desktop/VDXR still needs a headset retest of Play/Create Character
at 3K and after changing resolution. These checks do not establish headset
validation.

The Quest/VDXR retest showed that scaling mouse messages alone did not restore
the bottom row. Native `SetCursorPosition` subsequently clamps X/Y using
desktop width/height. At a 1440-high desktop, a button at source Y=1650 is
unreachable regardless of the correct incoming event. Two audited size-query
return sites now receive the full applied VR source for that clamp. Height
uses a direct call; width tail-calls the query, so both the caller and tail
jump are signature-checked. UI layout/font callers still receive only the
widescreen band. Desktop mode retains the native bounds.

Ground-item labels are placed over the whole eye, but the game's label layout
drops a label whose bottom passes the UI safe area's height, which is the
widescreen band. Items lower in the view lost their labels. At a 2048 square or
an Index eye that line is about the middle of the view, so the item a player
looked at had no label, with Show Items on as well. The label layout's single
safe-area size query (`0x8462E0`, called at `0x1FAC2A`, both signature-checked
once the label code is decrypted) now receives the full source height. Its
width, every other caller and Desktop mode are unchanged.

The real-window fixture reproduces the desktop-height truncation and verifies
bottom-row coordinates survive the corrected clamp after each live size
change, with desktop restoration. The three added branch guards match the
local 3.3.93787 code snapshot. All 56 focused checks pass; a new headset retest
of the entire bottom row is required. The prior event-only build failed that
headset scenario.

Merchant trade/repair and gambling screens bypass UISwitcher. They now receive
its upper widescreen-band height correction too, keeping the approved 0.55 m
distance and existing scale. At 4096 square this lowers the full canvas by
about 23.4 cm, centering the visible shop and inventory panes at eye height.
The laser uses the same pose; tooltips and confirmations remain on the complete
canvas. Waypoint/journal placement is unchanged. The
October 13 20:22 recording confirms the previously high merchant placement;
the correction still needs an offline headset check of trading, repairs,
confirmation buttons and item dragging.

## Release validation update (October 14)

The owner confirmed the complete bottom row is now clickable on Quest 3 through
Virtual Desktop/VDXR with the combined cursor-bounds build. The earlier failed
mouse-event-only test above is retained as diagnostic history. This does not
establish every resolution/runtime combination.

## A game window narrower than the VR source (October 20)

With the game windowed at 1920 x 1080 and a 3072-square VR source, the laser
could not reach the right side of any panel (the main menu's options, the
inventory, the stash), and the wrist map stayed empty under its title and
compass. A player reported the same on a Vive Pro at 3000 and 4000 sources:
part of the inventory at 3000, nearly all of it at 4000. A full-screen 3840 x
2160 desktop showed neither.

- **Cursor.** The game converts the cursor between the window's pixels and its
  own space (`0x7f350` in; `0x7f210` and `0x7f2d0` out; their content size
  `0x7f540`). With modern graphics it fits a letterbox and clamps X to a
  centred band 2.11 times the window's height. Those size queries returned
  the physical window, while the cursor arrives in VR-source pixels. The band
  ended at 2100 of 3072 in the 1920 x 1080 window, at 2800 for a 2560 x 1440
  one and at 4200 for 3840 x 2160, which is past the source. The seven
  return sites now receive the full applied VR source, as SetCursorPosition's
  clamp already did, and so does the controller cursor's own clamp, which
  calls `0x7f4d0` (it tail-jumps to the query). Each CALL and JMP is
  signature-checked before the hook goes in. Desktop mode and the time before
  a resize are unchanged.
- **Wrist map.** The game sizes the corner map's box from its own window, a
  third of it: 1280 x 720 under the 3840 desktop and 640 x 360 in the 1920
  window, both at the same corner of the 3072 layer. The map's scissor test
  required a box taller than an eighth of the layer, 384 at 3072, so the
  windowed box was refused and nothing was copied. The box now passes when
  its size fits the layer or the game window's own client area; the corner
  test is unchanged.

A private diagnostic build logged the game's cursor stopping at X = 2100
while the converted pointer reached 2797, and the map's 640 x 360 box being
refused. The owner then confirmed in the headset that the options panel and
the inventory's right side can be pointed at and clicked, and that the
wrist map shows, in the windowed 1920 x 1080 game.
