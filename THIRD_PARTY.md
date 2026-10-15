# Provenance and third-party notices

D2R PCVR is an independently developed mod that adds VR support and motion
interactions on top of the original Diablo II: Resurrected. It is not a
standalone game. You must purchase and install your own legitimate copy of
the game; this Windows preview supports the Steam edition and experimental
Battle.net installations on the documented game builds.

No Blizzard game executables, models, textures, audio or other game assets
are distributed in this repository, graphical installer or release ZIP. Required native artwork
and potion models are read from the user's own installation into local
caches, which must not be redistributed. The mod includes licensed
open-source components and retains their notices below.

This adaptation starts from [natpoh/diablo2-vr-port](https://github.com/natpoh/diablo2-vr-port)
at `10cfb0a2a9080184101794d3ac8c238c35717967`, under the retained [MIT license](LICENSE),
copyright 2026 BodyWalkVR. Existing source, original generated sky art, and
attribution remain. BodyWalk and FlatVR are separate upstream products; this
native OpenXR preview does not distribute them or use them as runtime dependencies.
The original user reference, `emmericp/D2R-3D`, is a separate checkout.

The private scene-depth channel and D3D12 event filter in
`steamvr/scene_depth.*`, `vr/game_device_only.h` and `reshade/D2R_DepthFog.fx`
adapt further work from `natpoh/diablo2-vr-port` at
`4b6284c0766c089139693a504333b9a19201cdd2` (MIT, copyright 2026 BodyWalkVR).
The native OpenXR adaptation uses current-eye color/depth bindings, tracked
resource states and device lifetimes to keep square shadow maps out of fog/sky
sampling. This does not include upstream's experimental command replay renderer.

| Component | Source / version | Notice |
|---|---|---|
| MinHook (compiled into plugin) | Vendored `third_party/minhook` | `third_party/minhook/LICENSE.txt`, BSD 2-clause |
| ReShade SDK headers | Vendored `external/reshade` | `external/reshade/LICENSE.md`, BSD 3-clause |
| D2RLoader Plugin SDK headers | [D2RLoader/PluginSDK](https://github.com/D2RLoader/PluginSDK), `717f727a0ec52912d1558764345f8fa3453a2bd6` | `steamvr/licenses/D2RLoader-PluginSDK-MIT.txt` |
| OpenXR headers and Windows loader | [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK), 1.1.62, `57af7fc61f9f2d492580cb28aab6d0ea59d8d417` | `steamvr/licenses/OpenXR-Apache-2.0.txt`, `OpenXR-MIT.txt` |
| JsonCpp (compiled into OpenXR loader) | OpenXR SDK's vendored JsonCpp | `steamvr/licenses/JsonCpp.txt`, public domain / MIT |
| MinGW runtime and GCC runtime libraries | MinGW-w64 / GCC 13, static linkage | `steamvr/licenses/MinGW-copyright.txt`, `GCC-runtime-copyright.txt`, `GPL-3.0.txt`; GCC Runtime Library Exception applies |
| Native Windows C++/compiler runtime | LLVM-MinGW 20260616, Clang 22.1.8, static libc++/compiler-rt and winpthreads | `steamvr/licenses/LLVM-Apache-2.0-with-exceptions.txt`, `LLVM-MinGW-runtime.txt`, `winpthreads-MIT.txt` |

OpenXR loader copyright holders include The Khronos Group Inc. (2017–2026),
Valve Corporation (2017–2019) and LunarG, Inc. (2017–2019), with additional
per-file notices in the linked SDK. The Windows build wrapper adjusts build
flags for MinGW; the loader's source is otherwise unchanged. D3D12 VRS enum and
interface compatibility declarations follow Microsoft's
[DirectX-Headers](https://github.com/microsoft/DirectX-Headers) API definitions.

The installer downloads **D2RLoader 1.3.1-beta** from [d2rloader.net](https://d2rloader.net/)
and **ReShade 6.8.0 with full add-on support** from [reshade.me](https://reshade.me/).
These binaries are not bundled in the release ZIP. The original dependency
licenses and terms govern them. Pinned archive checksums are in `steamvr/install.py`.

The portable ZIP setup downloads the official **CPython 3.12.10 x64 embeddable
package** from [python.org](https://www.python.org/downloads/release/python-31210/)
into the extracted mod folder. It checks the archive's SHA-256 and the interpreter's
Python Software Foundation signature. Python is not bundled in the release ZIP;
its license is retained in the downloaded package. The graphical Setup EXE
bundles that same verified runtime and its license in `.runtime/python/LICENSE.txt`.
This private runtime does not replace the user's system Python. Its checksum is
in `steamvr/setup_windows.ps1` and `installer/stage_native_installer.py`.

The graphical installer is built with [Inno Setup](https://jrsoftware.org/),
Copyright (C) 1997–2026 Jordan Russell; portions Copyright (C) 2000–2026 Martijn
Laan. Its unmodified installer engine retains its copyright and About-box notices.
The build uses Inno Setup 7.1.0; its license is retained in
[`steamvr/licenses/Inno-Setup.txt`](steamvr/licenses/Inno-Setup.txt).

On Linux the installer also downloads Microsoft's
[Windows SDK for C++ 10.0.26100.9169](https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP/10.0.26100.9169)
and extracts `c/Redist/D3D/x64/d3dcompiler_47.dll` for mod-local shader compilation.
Neither the SDK package nor its compiler is bundled in this repository or release.
Microsoft's [Windows SDK license](https://aka.ms/WinSDKLicenseURL) applies.

The visor blood splatters in `steamvr/assets/blood-splatters.png` are eight
transparent images supplied by contributor ChuckRibbits, who states they are free,
openly licensed vector splatters; their original download source was not
recorded. They are packed by `steamvr/prepare_blood.py` and embedded in the
plugin. See [`steamvr/assets/README.md`](steamvr/assets/README.md#visor-blood).

Diablo II: Resurrected, its executable, archives and save data belong to Blizzard
Entertainment / their owners and are not distributed in this preview. This
project is not affiliated with or endorsed by Blizzard Entertainment, Valve,
Meta, Virtual Desktop, WiVRn, or the upstream authors.

The read-only Steam static-container/TVFS adapter in `steamvr/steam_assets.py`
is adapted from [levinium/D2RExtractor](https://github.com/levinium/D2RExtractor)
(MIT; its Steam reader credits rustydemon). The retained notice is
[`steamvr/licenses/D2RExtractor-MIT.txt`](steamvr/licenses/D2RExtractor-MIT.txt).
The local SpA1 v31 RGBA decoder and PNG writer use Python's standard library.
Extracted spell/item artwork belongs to the game; it is generated only in the
user's local icon cache, never included in this repository or release ZIP.

The experimental Battle.net asset reader uses
[CascLib 3.0](https://github.com/ladislav-zezula/CascLib/tree/4971d363e665551ac4142f541e5f2d71f1cda653),
MIT, copyright 2014 Ladislav Zezula. The pinned source revision is
`4971d363e665551ac4142f541e5f2d71f1cda653`; `steamvr/build_casclib.ps1` builds a
Unicode x64 DLL with the repository's native Windows toolchain. Its license is
[`steamvr/licenses/CascLib-MIT.txt`](steamvr/licenses/CascLib-MIT.txt).
The DLL is used only during installation to read the owner's local archives;
online storage and downloads are not enabled. Packages made with `--casclib`
include the library and its license, with Battle.net marked experimental.
The wheel/wrist framing is separately generated artwork. Its attribution is
recorded in the [asset notices](https://github.com/bearstonem/d2r-pcvr/blob/vr/quest-2-3-support/steamvr/assets/README.md).

The installation-only BitKnit decoder in `steamvr/vendor/bitknit.py` is vendored
from [Rasetsuu/blendergranny](https://github.com/Rasetsuu/blendergranny) commit
`aec91bbd8e244d82277d1f5435dc20feff3086f9`, MIT, copyright 2026 ciupix and
contributors. Only its imports were adapted to run standalone; the notice is
[`steamvr/licenses/blendergranny-MIT.txt`](steamvr/licenses/blendergranny-MIT.txt).
The local bounded Granny mesh adapter exports static potion geometry only.
The texture header/format mapping was checked against
[D2R-Reimagined's TextureReader](https://github.com/D2R-Reimagined/reimagined-level-editor/blob/master/src/D2RLevel.Assets/TextureReader.cs).
Original potion models and textures belong to the game and are extracted into
the local `d2r_vr_potions` cache; neither that cache nor raw game assets are
distributed in the repository or release. No Granny runtime is needed.
