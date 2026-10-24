param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [string]$Version = '0.3.24',
    [string]$ISCCPath,
    [string]$PythonArchive,
    [string]$OutputDirectory,
    [string]$LauncherSetup,
    [string]$LauncherSetupSha256
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = Split-Path $PSScriptRoot -Parent
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Use a numeric three- or four-part installer version.' }
$package = (Resolve-Path -LiteralPath $PackagePath).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build-frame/installers' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$PythonArchive) { $PythonArchive = Join-Path $repoRoot 'deps/downloads/python-3.12.10-embed-amd64.zip' }
$pythonDigest = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
if (!(Test-Path -LiteralPath $PythonArchive)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PythonArchive) | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip' -OutFile $PythonArchive
}
if ((Get-FileHash -LiteralPath $PythonArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pythonDigest) {
    throw 'Private Python download checksum mismatch'
}
$python = Join-Path $repoRoot 'deps/python/python.exe'
if (!(Test-Path -LiteralPath $python)) { $python = (Get-Command python -ErrorAction Stop).Source }
if (!$ISCCPath) {
    $compilerCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compilerCommand) { $ISCCPath = $compilerCommand.Source }
    else { $ISCCPath = Join-Path $repoRoot 'deps/inno-7.1.0/ISCC.exe' }
}
if (!(Test-Path -LiteralPath $ISCCPath)) {
    throw 'Install Inno Setup 7.1 or later and pass -ISCCPath to its ISCC.exe. See installer/README.md.'
}
function Get-InnoCompilerVersion([string]$CompilerPath) {
    try {
        $versionOutput = & $CompilerPath --version 2>&1
        $versionExitCode = $LASTEXITCODE
    }
    catch {
        $versionOutput = ''
        $versionExitCode = 1
    }
    if ($versionExitCode -eq 0) {
        $candidate = ($versionOutput | Out-String).Trim()
        if ($candidate -match '^\d+\.\d+\.\d+(?:\.\d+)?$' -and
            [version]$candidate -gt [version]'0.0.0.0') {
            return $candidate
        }
    }
    $info = (Get-Item -LiteralPath $CompilerPath).VersionInfo
    foreach ($candidate in @($info.ProductVersion, $info.FileVersion)) {
        $candidate = ([string]$candidate).Trim()
        if ($candidate -match '^\d+\.\d+\.\d+(?:\.\d+)?$' -and
            [version]$candidate -gt [version]'0.0.0.0') {
            return $candidate
        }
    }
    throw 'Could not determine a valid Inno Setup compiler version'
}
$compilerVersion = Get-InnoCompilerVersion $ISCCPath
$launcherArguments = @()
if ($LauncherSetup -or $LauncherSetupSha256) {
    # The D2R VR Launcher's own Setup, carried unmodified from https://github.com/natpoh/d2r-vr-launcher/releases and installed silently
    # when it is missing or older. Pin the file's SHA-256 here, from the release's .sha256 file; nothing is downloaded at install time.
    if (!$LauncherSetup -or $LauncherSetupSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Give both -LauncherSetup <D2R_VR_Launcher_Setup_version.exe> and its 64-digit -LauncherSetupSha256.'
    }
    $launcher = (Resolve-Path -LiteralPath $LauncherSetup).Path
    if ((Split-Path -Leaf $launcher) -notmatch '^D2R_VR_Launcher_Setup_\d+(\.\d+){1,3}\.exe$') {
        throw 'The launcher Setup must keep its published name, D2R_VR_Launcher_Setup_<version>.exe.'
    }
    if ((Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash.ToLowerInvariant() -ne $LauncherSetupSha256.ToLowerInvariant()) {
        throw 'Launcher Setup checksum mismatch'
    }
    $launcherArguments = @('--launcher-setup', $launcher, '--launcher-sha256', $LauncherSetupSha256.ToLowerInvariant())
}
$staged = & $python (Join-Path $PSScriptRoot 'stage_native_installer.py') --package $package --python-archive $PythonArchive --staging-parent (Join-Path $repoRoot 'build-frame/installer-staging') --version $Version @launcherArguments
if ($LASTEXITCODE -ne 0) { throw 'Native installer payload validation failed' }
$stage = $staged | ConvertFrom-Json
$runtimeExe = Join-Path $stage.payload '.runtime/python/python.exe'
$signature = Get-AuthenticodeSignature -LiteralPath $runtimeExe
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
    throw 'Bundled private Python publisher signature validation failed'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$launcherDefines = @()
if ($stage.carried_launcher) {
    $launcherDefines = @("/DLauncherSetupFile=$(Split-Path -Leaf $stage.carried_launcher.file)", "/DLauncherVersion=$($stage.carried_launcher.version)",
                         "/DLauncherSetupSha256=$($stage.carried_launcher.sha256)")
}
& $ISCCPath /Qp "/DPayloadDir=$($stage.payload)" "/DModVersion=$Version" "/DInstallerOutput=$output" @launcherDefines (Join-Path $PSScriptRoot 'D2R_PCVR_Setup.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed' }
$installer = Join-Path $output "D2R-PCVR-Setup-$Version.exe"
$digest = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$installer.sha256", "$digest  $([IO.Path]::GetFileName($installer))`n")
$report = [ordered]@{
    installer = $installer
    bytes = (Get-Item -LiteralPath $installer).Length
    sha256 = $digest
    package_sha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    source_commit = $stage.source_commit
    plugin_source_commit = $stage.plugin_source_commit
    plugin_sha256 = $stage.plugin_sha256
    compiler = ($compilerVersion | Out-String).Trim()
    private_python = '3.12.10 (official x64 embeddable package; SHA-256 and publisher checked)'
    carried_launcher = $stage.carried_launcher
    signed = $false
}
$report | ConvertTo-Json | Set-Content -LiteralPath "$installer.build.json" -Encoding UTF8
$report | ConvertTo-Json
