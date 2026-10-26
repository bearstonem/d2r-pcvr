param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = Split-Path $PSScriptRoot -Parent
$depsRoot = Join-Path $repoRoot 'deps'
$downloads = Join-Path $depsRoot 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

function Get-VerifiedArchive($Url, $Name, $Digest, $Algorithm = 'SHA256') {
    $destination = Join-Path $downloads $Name
    if (!(Test-Path -LiteralPath $destination) -or
        (Get-FileHash -LiteralPath $destination -Algorithm $Algorithm).Hash -ne $Digest) {
        Write-Host "Downloading $Name"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $destination
    }
    if ((Get-FileHash -LiteralPath $destination -Algorithm $Algorithm).Hash -ne $Digest) {
        throw "Checksum mismatch: $Name"
    }
    return $destination
}

$toolArchives = @(
    @{
        name = 'llvm-mingw-20260616-ucrt-x86_64.zip'
        url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip'
        hash = 'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35'
        directory = $depsRoot
        executable = 'llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe'
    },
    @{
        name = 'cmake-4.4.4-windows-x86_64.zip'
        url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'
        hash = 'bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536'
        directory = $depsRoot
        executable = 'cmake-4.4.4-windows-x86_64/bin/cmake.exe'
    },
    @{
        name = 'ninja-win.zip'
        url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
        hash = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
        directory = (Join-Path $depsRoot 'ninja')
        executable = 'ninja/ninja.exe'
    }
)
foreach ($toolArchive in $toolArchives) {
    $archive = Get-VerifiedArchive $toolArchive.url $toolArchive.name $toolArchive.hash
    if (!(Test-Path -LiteralPath (Join-Path $depsRoot $toolArchive.executable))) {
        Write-Host "Extracting $($toolArchive.name)"
        Expand-Archive -LiteralPath $archive -DestinationPath $toolArchive.directory -Force
    }
}

# Python's official release page publishes this MD5. HTTPS provides the download
# origin; verify the extracted interpreter's publisher signature as well.
$pythonArchive = Get-VerifiedArchive 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip' 'python-3.12.10-embed-amd64.zip' 'fe8ef205f2e9c3ba44d0cf9954e1abd3' 'MD5'
$pythonDirectory = Join-Path $depsRoot 'python'
if (!(Test-Path -LiteralPath (Join-Path $pythonDirectory 'python.exe'))) {
    Expand-Archive -LiteralPath $pythonArchive -DestinationPath $pythonDirectory
}
$pythonSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $pythonDirectory 'python.exe')
if ($pythonSignature.Status -ne 'Valid' -or $pythonSignature.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
    throw 'Portable Python publisher signature validation failed'
}
# The scripts use only the standard library. Add this checkout explicitly to
# embedded Python's isolated import path, without changing the system Python.
Set-Content -LiteralPath (Join-Path $pythonDirectory 'python312._pth') -Encoding ASCII -Value @('python312.zip', '.', '../../steamvr', '../..')

$sdkRepos = @(
    @{name = 'D2RLoader-PluginSDK'; url = 'https://github.com/D2RLoader/PluginSDK.git'; commit = '717f727a0ec52912d1558764345f8fa3453a2bd6'},
    @{name = 'OpenXR-SDK'; url = 'https://github.com/KhronosGroup/OpenXR-SDK.git'; commit = '57af7fc61f9f2d492580cb28aab6d0ea59d8d417'},
    # Box3D v0.1.0 (MIT): rigid-body physics for dropped empty bottles.
    @{name = 'box3d'; url = 'https://github.com/erincatto/box3d.git'; commit = '8441b4a06d6d09dcfb0b0f704df4d847d1437b92'}
)
foreach ($sdkRepo in $sdkRepos) {
    $checkout = Join-Path $depsRoot $sdkRepo.name
    if (!(Test-Path -LiteralPath (Join-Path $checkout '.git'))) {
        git init $checkout
        if ($LASTEXITCODE -ne 0) { throw "Cannot initialize $($sdkRepo.name)" }
        git -C $checkout remote add origin $sdkRepo.url
        if ($LASTEXITCODE -ne 0) { throw 'Cannot configure SDK remote' }
        git -C $checkout fetch --depth 1 origin $sdkRepo.commit
        if ($LASTEXITCODE -ne 0) { throw 'SDK fetch failed' }
        git -C $checkout checkout --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw 'SDK checkout failed' }
    }
    if ((git -C $checkout rev-parse HEAD) -ne $sdkRepo.commit) {
        throw "Wrong SDK revision: $checkout"
    }
}
Write-Host 'Portable native Windows tools ready under deps/.'
