param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [string]$Version = '0.3.15',
    [string]$ISCCPath,
    [string]$PythonArchive,
    [string]$OutputDirectory
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
$compilerVersion = & $ISCCPath --version
if ($LASTEXITCODE -ne 0) { throw 'Could not determine the Inno Setup compiler version' }
$staged = & $python (Join-Path $PSScriptRoot 'stage_native_installer.py') --package $package --python-archive $PythonArchive --staging-parent (Join-Path $repoRoot 'build-frame/installer-staging')
if ($LASTEXITCODE -ne 0) { throw 'Native installer payload validation failed' }
$stage = $staged | ConvertFrom-Json
$runtimeExe = Join-Path $stage.payload '.runtime/python/python.exe'
$signature = Get-AuthenticodeSignature -LiteralPath $runtimeExe
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
    throw 'Bundled private Python publisher signature validation failed'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $ISCCPath /Qp "/DPayloadDir=$($stage.payload)" "/DModVersion=$Version" "/DInstallerOutput=$output" (Join-Path $PSScriptRoot 'D2R_PCVR_Setup.iss')
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
    signed = $false
}
$report | ConvertTo-Json | Set-Content -LiteralPath "$installer.build.json" -Encoding UTF8
$report | ConvertTo-Json
