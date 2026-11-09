# Run with: pwsh -NoProfile -File installer/compiler_version_test.ps1
# Load only the production function, without invoking downloads or compilation.
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'build_native_installer.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Build script has PowerShell syntax errors' }
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-InnoCompilerVersion'
}, $true)
if (!$function) { throw 'Missing compiler version function' }
. ([scriptblock]::Create($function.Extent.Text))

# Mock the executable and its metadata; no installed compiler is required.
function TestCompiler {
    param([string]$Option)
    if ($Option -ne '--version') { throw 'Unexpected compiler argument' }
    if ($script:queryThrows) { throw 'Unsupported option' }
    $global:LASTEXITCODE = $script:queryExit
    $script:queryOutput
}
function Get-Item {
    param([string]$LiteralPath)
    $script:metadataReads++
    [pscustomobject]@{ VersionInfo = [pscustomobject]@{
        ProductVersion = $script:productVersion
        FileVersion = $script:fileVersion
    } }
}
$cases = @(
    @(0, '7.1.0', '0.0.0.0', '0.0.0.0', $false, '7.1.0'),
    @(1, 'Unknown option: --version', '7.1.0', '0.0.0.0', $false, '7.1.0'),
    @(1, '', '0.0.0.0', '7.1.0.0', $false, '7.1.0.0'),
    @(1, '', '7.1.0', '0.0.0.0', $true, '7.1.0'),
    @(0, 'invalid output', '', '7.1.0', $false, '7.1.0'),
    @(0, '0.0.0.0', '7.1.0', '0.0.0.0', $false, '7.1.0'),
    @(0, '0.0.0.0', '0.0.0.0', '0.0.0.0', $false, $null),
    @(1, '', '', '', $false, $null),
    @(1, '7.1.0', '', '', $false, $null)
)
foreach ($case in $cases) {
    $script:queryExit, $script:queryOutput, $script:productVersion,
        $script:fileVersion, $script:queryThrows, $expected = $case
    $script:metadataReads = 0
    $actual = $null
    $failed = $false
    try { $actual = Get-InnoCompilerVersion 'TestCompiler' }
    catch {
        if ($_.Exception.Message -ne 'Could not determine a valid Inno Setup compiler version') { throw }
        $failed = $true
    }
    if (($null -eq $expected) -ne $failed -or $actual -ne $expected) {
        throw "Version case failed: $($case -join ', ')"
    }
    if ($script:queryExit -eq 0 -and $script:queryOutput -eq '7.1.0' -and $script:metadataReads) {
        throw 'Successful version query must take precedence over metadata'
    }
}
Write-Host "$($cases.Count) compiler version cases passed."
