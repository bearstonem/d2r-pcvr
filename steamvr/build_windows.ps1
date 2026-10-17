param([switch]$Test, [switch]$Developer, [switch]$NoDevTools)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$cmake = Join-Path $repoRoot 'deps/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$ninja = Join-Path $repoRoot 'deps/ninja/ninja.exe'
$buildDirectory = Join-Path $repoRoot 'build-frame/windows-native'
if ($Developer) { $buildDirectory = Join-Path $repoRoot 'build-frame/windows-developer' }
$devToolsOption = if ($NoDevTools) { 'OFF' } else { 'ON' }
$developerOption = if ($Developer) { 'ON' } else { 'OFF' }
if (!(Test-Path -LiteralPath $cmake) -or !(Test-Path -LiteralPath $ninja)) {
    throw 'Run steamvr/setup_windows_tools.ps1 first'
}
& $cmake -S $PSScriptRoot -B $buildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$repoRoot/cmake/windows-llvm-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release "-DD2RVR_DEVELOPER=$developerOption" "-DD2RVR_DEV_TOOLS=$devToolsOption"
if ($LASTEXITCODE -ne 0) { throw 'Native Windows configure failed' }
& $cmake --build $buildDirectory --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native Windows build failed' }
if ($Test) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native Windows tests failed' }
}
Write-Host "Built $buildDirectory/d2rl-pcvr.dll (staged; installation is separate)."
