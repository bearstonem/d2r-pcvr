// Who this plugin is: D2R PCVR, not D2R VR. Shared by vr/vrcam.cpp (the
// D2RLoader PluginInfo) and vr/vrcam.rc (the DLL's VERSIONINFO), so the plugin,
// its file properties and a support log all say the same thing.
//
// D2R PCVR started as a fork of D2R VR 0.152 by BodyWalkVR (MIT), and until
// 0.3.16 it kept D2R VR's plugin id, name, version, author and file names. The
// two mods now have their own: D2R PCVR is d2rl-pcvr.dll with d2r_pcvr.ini;
// D2R VR keeps d2rl-vrcam.dll with d2r_vr.ini.
//
// Keep D2RPCVR_VERSION in step with installer/D2R_PCVR_Setup.iss (ModVersion)
// and installer/build_native_installer.ps1 (-Version); steamvr/mod_identity_test.py
// checks that they agree.
#pragma once

#define D2RPCVR_MOD_ID "d2r-pcvr"
#define D2RPCVR_PLUGIN_ID "d2r-pcvr-vrcam"
#define D2RPCVR_NAME "D2R PCVR"
#define D2RPCVR_AUTHOR "bearstonem"
#define D2RPCVR_VERSION "0.3.19"
#define D2RPCVR_VERSION_RC 0, 3, 19, 0
#define D2RPCVR_PLUGIN_FILE "d2rl-pcvr.dll"
#define D2RPCVR_SETTINGS_FILE_W L"d2r_pcvr.ini"
// D2R VR's plugin. When it sits beside ours D2RLoader would load both VR layers
// into one game; ours then stays out (see D2RLoaderLoadPlugin).
#define D2RPCVR_OTHER_PLUGIN_FILE_W L"d2rl-vrcam.dll"
#define D2RPCVR_CREDITS "Based on D2R VR by BodyWalkVR (MIT)."
// One literal for vrcam.rc (resource strings are not concatenated reliably).
#define D2RPCVR_RC_COMMENTS "mod-id=d2r-pcvr; plugin-id=d2r-pcvr-vrcam; Based on D2R VR by BodyWalkVR (MIT)."
