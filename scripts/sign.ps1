<#
.SYNOPSIS
  Authenticode-signs files with a PFX certificate (SHA-256, RFC 3161 timestamp).

.DESCRIPTION
  Reads the certificate from environment variables so it never touches the repo:
    WINDOWS_CERT_PFX_BASE64   base64 of the .pfx file
    WINDOWS_CERT_PASSWORD     its password
  In CI these come from the repository secrets of the same names.

.EXAMPLE
  powershell -File scripts/sign.ps1 dist/AxSynth.vst3/Contents/x86_64-win/AxSynth.vst3
#>
param(
    [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]] $Files,
    [string] $TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

if (-not $env:WINDOWS_CERT_PFX_BASE64) { throw 'WINDOWS_CERT_PFX_BASE64 is not set' }

# Newest signtool from the Windows SDK.
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object { [version]($_.Directory.Parent.Name) } -Descending | Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK)' }

$pfx = Join-Path ([IO.Path]::GetTempPath()) ("axsynth-" + [guid]::NewGuid() + '.pfx')
[IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERT_PFX_BASE64))
try {
    foreach ($f in $Files) {
        & $signtool.FullName sign /f $pfx /p $env:WINDOWS_CERT_PASSWORD /fd SHA256 /tr $TimestampUrl /td SHA256 /d AxSynth $f
        if ($LASTEXITCODE -ne 0) { throw "signtool sign failed for $f" }
        & $signtool.FullName verify /pa $f
        if ($LASTEXITCODE -ne 0) { throw "signtool verify failed for $f" }
    }
} finally {
    Remove-Item -Force $pfx -ErrorAction SilentlyContinue
}
