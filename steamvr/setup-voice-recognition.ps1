param([switch]$NoLaunch)
# Set up only the local speech UI. Does not build/install the game DLL.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$python = Join-Path $repo 'deps/python/python.exe'
& $python (Join-Path $PSScriptRoot 'setup_voice_recognition.py')
if ($LASTEXITCODE -ne 0) { throw 'Speech runtime/model setup failed.' }
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$exe = Join-Path $repo 'build-frame/voice-recognition/voice-recognition.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll "/out:$exe" (Join-Path $PSScriptRoot 'voice_recognition.cs') (Join-Path $PSScriptRoot 'sound_commands.cs')
if ($LASTEXITCODE -ne 0) { throw 'Speech UI compilation failed.' }
if (!$NoLaunch) { & $exe }
