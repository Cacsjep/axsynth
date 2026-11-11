<#
.SYNOPSIS
  Builds AxSynth-<version>-win64.msi from a built AxSynth.vst3 bundle with WiX Toolset v4.

.DESCRIPTION
  Needs wix 4.x plus the matching UI extension, e.g. (locally, into the gitignored tools/ dir):
    dotnet tool install --tool-path tools/wix4 wix --version 4.0.6
    tools/wix4/wix.exe extension add -g WixToolset.UI.wixext/4.0.6

.EXAMPLE
  powershell -File scripts/build-msi.ps1 -Version 1.0.0 -Wix tools/wix4/wix.exe
#>
param(
    [Parameter(Mandatory)] [string] $Version,
    [string] $Bundle = 'build/AxSynth_artefacts/Release/VST3/AxSynth.vst3',
    [string] $OutDir = 'dist',
    [string] $Wix = 'wix',
    # Must match the installed wix version; pins the UI extension from the global cache.
    [string] $WixVersion = '4.0.6'
)

$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must be x.y.z, got '$Version'" }

$root = Split-Path -Parent $PSScriptRoot
$bundleDir = (Resolve-Path $Bundle).Path
if (Test-Path $Wix) { $Wix = (Resolve-Path $Wix).Path }  # a path, not a command on PATH

# The .wxs lists the bundle files explicitly. Refuse to build if the bundle has
# files the installer does not know about, so nothing is silently left out.
$expected = @('Contents\Resources\moduleinfo.json', 'Contents\x86_64-win\AxSynth.vst3')
$actual = @(Get-ChildItem -Recurse -File $bundleDir | ForEach-Object { $_.FullName.Substring($bundleDir.Length + 1) } | Sort-Object)
$diff = Compare-Object $expected $actual
if ($diff) {
    $diff | Format-Table | Out-String | Write-Host
    throw 'Bundle contents differ from the files listed in Installer/AxSynth.wxs; update the installer.'
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$msi = Join-Path (Resolve-Path $OutDir).Path "AxSynth-$Version-win64.msi"

Push-Location (Join-Path $root 'Installer')
try {
    & $Wix build AxSynth.wxs -arch x64 -ext "WixToolset.UI.wixext/$WixVersion" `
        -d "ProductVersion=$Version" -d "BundleDir=$bundleDir" -o $msi
    if ($LASTEXITCODE -ne 0) { throw "wix build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

Remove-Item -ErrorAction SilentlyContinue ([IO.Path]::ChangeExtension($msi, '.wixpdb'))
Write-Host "Built $msi"
