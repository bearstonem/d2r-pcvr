# Quest 2 and Quest 3 PCVR testing

Work continues on `vr/quest-2-3-support`, created from merged `main` at
`7b69ce7`. Use native Windows Steam D2R 3.3.93787 and a local offline character.
This is PCVR streaming, not a standalone Quest port.

The first test uses the unchanged, owner-verified Steam Frame build:
`f559b3e57d73cb38e9091753782e7702a53b5185db5108b5a8c488011ea1006c`.
That initial plugin matched the staged Windows build. Existing checks passed
33 native fixtures, 21 Python tests and both 17-call NGX ABI targets before
the merge; those checks do not establish Quest headset compatibility.

## Transport and runtime coverage

| Headset | Streaming connection | OpenXR runtime | Mod validation |
| --- | --- | --- | --- |
| Steam Frame | SteamVR streaming | SteamVR | Owner-verified offline gameplay |
| Quest 3 | Steam Link | SteamVR | Owner-confirmed offline gameplay; calibration/melee/exit logged |
| Quest 3 | Virtual Desktop | SteamVR | Not tested |
| Quest 3 | Virtual Desktop | VDXR | Owner confirms corrected colors; tracked outdoor melee and normal exit logged |
| Quest 2 | Steam Link / Virtual Desktop | Matching runtime | No physical Quest 2 test yet |

[Valve supports Quest 2 and Quest 3 with Steam Link](https://help.steampowered.com/en/faqs/view/0E2C-406B-9135-38A4).
The mod already suggests the Oculus Touch OpenXR interaction profile, including
both grip/aim poses, sticks, stick clicks, triggers, grips, A/B/X/Y, left Menu
and haptics. Rendering and input still require a real headset test.

[Virtual Desktop](https://www.vrdesktop.net/) supports these Quest headsets.
[VDXR](https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki) can run OpenXR
applications without SteamVR. Streaming connection and OpenXR runtime must be
recorded separately; connecting Virtual Desktop does not by itself prove VDXR
is serving the mod. Neither Quest 2 nor Quest 3 requires eye tracking for this
build; the current configuration uses fixed foveation and disables eye tracking.

## First Quest 3 run: Steam Link and SteamVR

1. Fully exit D2R. Disconnect Steam Frame, then connect Quest 3 to this PC using
   its Steam Link app. Confirm the dashboard and both tracked controllers work.
   Keep only the headset being tested connected.
2. Retain the working Steam launch wrapper and its per-game SteamVR manifest:
   `--frame-openxr-runtime=C:\steam\steamapps\common\SteamVR\steamxr_win64.json`.
   Normal Steam Play remains stock; `--frame-vr` is consumed by the wrapper.
3. Run the standalone OpenXR probe while D2R is closed. Record runtime, reported
   headset name, recommended eye sizes, tracking support, reference spaces,
   graphics adapter and swapchain formats. The full probe creates a temporary
   graphics session but does not begin it or submit frames; `--capabilities`
   avoids creating a graphics session and cannot check its reference spaces.
4. Launch through `steam://run/2536520//--frame-vr/`, accept Steam's extra argument
   prompt if present, and select the offline character. Verify stereo rendering
   and both tracked hands before testing combat.
5. Open settings with Left Grip + Y and recalibrate upright in a T cross. Saved
   Steam Frame calibration is shared, so it will not automatically prompt again.
   Check floor/height, controller reach, settings laser and A/B/X/Y input, then
   torso/shield visibility, belt grabbing/drinking and wrist HP/mana.
6. Test movement/turning, manual and free-swing melee outdoors and in the cave,
   wrist values across that transition, and a dominant-grip weapon-set swap
   behind either shoulder. Open/close Akara dialogue, then Save & Exit normally.
   Record what was actually tested; no overall report establishes every class,
   weapon combination, hand preference or controller gesture.

Preserve the plugin, camera/graphics settings and profiling switches during the
first transport comparison. Back up the installed INI before recalibration;
record changed calibration separately. A gameplay smoke test is not a controlled
performance comparison. Raw logs/captures belong under ignored `build-frame/`.

## Virtual Desktop follow-up

For Virtual Desktop + SteamVR, retain the same per-game SteamVR manifest.
For the separate VDXR test, connect through Virtual Desktop, leave SteamVR
closed, and use `steam://run/2536520//--frame-vr%20--frame-vdxr/`.
The Windows wrapper consumes `--frame-vdxr` and selects the installed VDXR
manifest instead of the normal SteamVR launch-option manifest. It does not
forward either VR flag to the game. The current PC has
`C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`.
The manifest is resolved relative to Windows Program Files. A missing manifest
aborts the VR launch rather than silently falling back to another runtime.
`--frame-vdxr` alone does not enable the mod: stock Play still requires no VR
launch flag and keeps the original executable/environment. Only `--frame-vr`
selects D2RLoader. The existing custom runtime option remains available, with
at most one configured manifest. The launcher routing and installer suite now
passes 24 Python tests; no new game DLL was required.
Verify the actual runtime in the probe and game log. Do not change the system
OpenXR registration to switch a single game's test; keep normal Play stock.
Fully exit D2R between transport/runtime changes and record each route separately.

The local Streamer update to 1.34.22 encountered a running service already
marked for deletion. Windows refused normal service control, so the verified
stuck Virtual Desktop service process was ended and the vendor-signed service
MSI installed successfully. The replacement service is running/automatic,
the deletion marker is cleared, and the installer requires no reboot. Repair
logs remain local. The initial probe had no headset connection
(`XR_ERROR_FORM_FACTOR_UNAVAILABLE`). After the owner connected Quest 3 and
confirmed the PC desktop was visible, the full VDXR probe passed. The following
gameplay sessions and color correction are recorded below.

## Quest 3 Virtual Desktop preflight, October 10

With SteamVR and D2R closed, the full probe identifies runtime
`VirtualDesktopXR` and headset `Meta Quest 3`. D3D12 device/session creation on
the RTX 5090 passes. VIEW, LOCAL and STAGE reference spaces are advertised, with
orientation and position tracking supported. Recommended eye size is
3072 x 3264 for this Streamer configuration, separate from the game's eye
image size. Eye-gaze interaction is not supported by the headset. Max layers
is 16; requested formats 28, 29 and 10 all succeed, producing typeless resource
formats 27, 27 and 9 respectively.

The explicit `--frame-vr --frame-vdxr` Steam launch uses the unchanged verified
plugin. D2RLoader started at 18:30:58; the game log confirms
`VirtualDesktopXR` at 18:31:07.485 and a D3D12 session with actual eye images
2560 x 1440, format 28. SteamVR remains closed. Gameplay becomes active outdoors
with the head, both grip poses and aim tracked at 18:31:39.435. The different
eye image sizes between transport runs prevent treating them as a controlled
performance comparison. The owner reports **"everything looks washed out"**;
the game then exits normally at 18:32:44.680. Raw probes,
logs, settings backups and service-repair records remain local under
`build-frame/windows/quest3-vdxr-20261010-182014/`.

## VDXR color correction

VDXR accepts both linear UNORM (28) and sRGB (29) swapchains. The old selector
retained the game's format 28 whenever supported, although the copied game
pixels are already sRGB-encoded. SteamVR rejected 28, so the fallback selected
29 and concealed the bug. OpenXR interprets non-sRGB formats as linear;
[the specification's rendering rules](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc)
require sRGB-encoded content to use an sRGB swapchain format.

The correction selects the sRGB sibling for copied RGBA/BGRA SDR buffers even
when the runtime also exposes UNORM. Byte copies remain unchanged; a missing
color-correct format fails initialization instead of silently changing gamma.
Float source formats remain unchanged. Session diagnostics now print the
selected eye format separately from the game's backbuffer format.

All 34 native tests pass. A D3D12 GPU fixture copies an encoded grayscale ramp
into a typeless runtime image, samples it through the selected format and reads
back linear values. It checks RGBA/BGRA, a runtime exposing both formats,
sRGB-only support and unchanged alpha. The isolated old selector fails this
fixture (encoded 0.502 reads as 0.502 instead of linear 0.216); the correction
passes. Existing native UI tests and the typeless sRGB potion stereo/readback
check also pass. Launcher validation remains 24 Python tests.

Color build `109dfa0e3c1d1a97d2b274180f56f8fb25f9680f8853e378ccf6245a85621962`
is installed at 18:35 PDT, with the previous DLL backed up and INI/graphics
hashes preserved. Its relaunch logs `VirtualDesktopXR` and eye format 29 with
game format 28 at 18:35:50.117; eye images remain 2560 x 1440 and SteamVR is
closed. The owner confirms **"Yes — colors look normal"**. This run logs 45
reports with accepted native melee contacts, zero position mismatches, outdoor
gameplay and normal unload at 18:46:00.553. No OpenXR failure is logged. Cave
entry, calibration and a set-I/II change are not separately recorded on VDXR.
Retest artifacts are local under
`build-frame/windows/quest3-vdxr-color-20261010-183522/`. The curated record is
[evidence/2026-10-10-quest3-vdxr.json](evidence/2026-10-10-quest3-vdxr.json).

## Windows release and Journal cleanup

`v0.3.0-windows-quest-preview` packages the native Windows build and
[installation/run instructions](../INSTALL-WINDOWS.md). The final DLL is
`e12a5e958d31300dbe52bf93a88017844c650b0f532bae08ca6606ca877c1167`.
After the confirmed color build, the owner requested removal of the duplicate
Journal wheel entry. Its label/icon, hover and command dispatch are removed;
Journal remains accessible through the native tabbed game menu (Frame Left A /
Quest Left X). Other wheel actions retain their locations.

All 34 native fixtures, 24 Python checks and the generated wheel preview check
pass. The final Journal cleanup has not had another physical headset session;
the recorded sessions above refer to their exact predecessor DLL hashes. See
[evidence/2026-10-10-windows-release.json](evidence/2026-10-10-windows-release.json).

## Quest 3 Steam Link preflight, October 10

SteamVR identifies `Oculus Quest3` through `vrlink`. The full standalone probe
passes D3D12 instance/session creation on the RTX 5090 and reports:

- Runtime `SteamVR/OpenXR`, headset `SteamVR/OpenXR : oculus`, max 16 layers.
- Orientation and position tracking supported; VIEW, LOCAL and STAGE spaces
  advertised. This does not by itself validate the physical floor height.
- Recommended eye size 2064 x 2208, distinct from the game's actual eye images.
- Eye-gaze extension advertised by SteamVR, but headset gaze support is false.
- sRGB format 29 and float format 10 supported, with typeless resource formats
  27 and 9. Linear format 28 is rejected; the existing mod selects supported
  sRGB and creates explicit typed views for these resources.

Raw probe and pre-test settings/calibration copies are retained locally in
`build-frame/windows/quest3-preflight-20261010-181143/`. A VR launch request has
been sent. D2RLoader started at 18:14:05; its log confirms SteamVR session
creation at 18:14:17, actual eye images 3840 x 2160, and active gameplay with
head, both grip poses and aim tracked. The supported sRGB eye format is selected
from the game's linear backbuffer. No OpenXR failure appears in the observed
startup window. The owner subsequently reports **"ok it all works"**.
The Quest session independently records calibration saving/closing (eye height
1.70706 m, controller span 1.49549 m), 17 reports with accepted melee contacts,
zero position mismatch reports, and normal D2RCore unload at 18:16:57.729.
The biome log covers outdoors; cave entry and a weapon-set change are not
separately recorded in this run. The overall owner report is not exhaustive
per-feature/class/weapon coverage, a Quest 2 test or a performance benchmark.
Earlier Steam Frame melee log entries are excluded from this evidence.
