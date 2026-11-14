# The campfire scene as tuned with its owner (2026-11-08/09): run from PowerShell to (re)build it in a mod folder.
# The installer builds the same scene: its options are prepare_campfire_sky.py's TUNED_SCENE (--tuned here).
param([string]$Game = "F:\Blizzard\Diablo II Resurrected", [ValidateSet("D2R-PCVR", "pcvr-dev")][string]$Mod = "D2R-PCVR")
& "$env:LOCALAPPDATA\Programs\D2R PCVR\.runtime\python\python.exe" "$PSScriptRoot\prepare_campfire_sky.py" $Game --mod $Mod --casclib "$env:LOCALAPPDATA\Programs\D2R PCVR\bin\CascLib.dll" --write --replace --tuned
