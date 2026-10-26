# D2RLoader mod layout and migration

D2R VR Unleashed uses D2RLoader's `D2R-PCVR` mod selection. D2RLoader and
its configuration remain shared; the plugin, OpenXR loader, settings, artwork
and ReShade files live under `mods/D2R-PCVR`. Launch through the supplied
SteamVR or VDXR shortcut so the wrapper selects `-mod D2R-PCVR` and retains
the Explorer handoff and per-launch runtime. The ordinary desktop game path
does not select this mod. This change does not require the shared launcher.

## Paths relative to the game directory

- Plugin: `mods/D2R-PCVR/d2rloader/plugins/d2rl-pcvr.dll`
- Settings and calibration: the same folder's `d2r_pcvr.ini`
- OpenXR loader: the same folder's `openxr_loader.dll`
- Local game artwork/model caches: the same folder's `d2r_vr_icons`,
  `d2r_vr_potions` and `d2r_vr_spell_sprites` (generated locally; never
  commit or distribute them)
- ReShade: `mods/D2R-PCVR/reshade`, with its own `ReShade.ini` and preset
- Runtime diagnostics: `mods/D2R-PCVR/d2rloader/logs/d2r-vr-frame.log`
- Launch diagnostics: `d2rloader/logs/pcvr-launch.log`
- Mod metadata: `mods/D2R-PCVR/D2R-PCVR.mpq/modinfo.json`

The metadata specifies `savepath: "../"` to use normal characters. Confirm
character visibility in the headset before release; automated fixtures do not
prove the game's runtime interpretation of this setting.

## Updating an existing installation

Use Update / repair (`install.py --preserve-settings` in the underlying
installer). It identifies PCVR through installation records, backups and DLL
markers, moves its global plugin/settings into the mod folder, and preserves
settings/calibration. An unrelated global VR plugin blocks installation because
it would also load into the selected mod. Other mod folders remain untouched.

Only root ReShade files recorded in PCVR's historical snapshots are migrated.
ReShade search paths and the default preset path are rebased while retaining
custom values and external search paths. Existing mod-folder settings win on
repair. Owned root effect files are removed only when no other loader user is
present; unrelated files remain in place. Backups journal each file's original
state once, even when migration and the packaged shader both write that file.

Repair retains existing shared loader configuration. If another mod uses a
different loader binary, it stops before game files change. Uninstall and
snapshot restore also preserve shared loader/runtime/root-effect files when
another mod is present, including data-only mods with modinfo metadata.

Update the installed launch helpers together with the game files. An older
wrapper does not select this mod and cannot launch the new layout correctly.
The desktop settings editor resolves the mod-folder INI and ReShade paths;
its mod-scoped launch/update actions direct players to the supported shortcuts
and installer. That legacy editor is compile-checked but is not shipped in the
native Windows package.

## Validation and remaining gameplay checks

Fixture tests cover settings/calibration migration, custom ReShade values and
paths, repeated repair, rollback, shared dependency preservation, conflicting
loader versions, and unrelated-file preservation. Native checks cover path
selection, ReShade isolation and creating the runtime log folder.

The owner verified Full install and then Update / repair on the combined
Windows preview, including retained VR settings, calibration and characters.
Both installer attempts completed successfully. The configured connection was
Steam Frame / SteamVR; the confirmation did not separately specify the runtime.
The first migration used Full install, which intentionally resets configuration.

Remaining release checks include VDXR, explicit ReShade and ordinary desktop
play checks, switching between independently installed D2R-PCVR and D2R-VR,
real uninstall, and an old-layout upgrade that preserves settings/calibration.
Automated fixtures cover the latter migration and uninstall behavior.
The `pcvr-dev` developer scenario mod requires its own plugin staging under
this layout; the production migration does not provision that developer setup.
