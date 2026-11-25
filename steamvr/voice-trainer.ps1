# Build only the desktop trainer, never the game DLL or installer.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$compiler = Join-Path $repo 'deps/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Run the repository tool setup first: the LLVM compiler is missing.' }
$outputDir = Join-Path $repo 'build-frame/voice-prototype'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir 'voice-trainer.exe'
$env:PATH = (Split-Path $compiler -Parent) + ';' + $env:PATH
& $compiler -O2 -std=c++20 -static (Join-Path $PSScriptRoot 'voice_trainer.cpp') -lwinmm -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Voice trainer compilation failed.' }
& $exe
