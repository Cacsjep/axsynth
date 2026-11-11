<#
.SYNOPSIS
  Prints the CHANGELOG.md section for one version (the text under "## [x.y.z]").
  Fails if the section is missing or empty, so a release cannot ship without notes.

.EXAMPLE
  powershell -File scripts/changelog-notes.ps1 -Version 1.0.0 -OutFile notes.md
#>
param(
    [Parameter(Mandatory)] [string] $Version,
    [string] $Changelog,
    [string] $OutFile
)

$ErrorActionPreference = 'Stop'
if (-not $Changelog) { $Changelog = Join-Path (Split-Path -Parent $PSScriptRoot) 'CHANGELOG.md' }

$lines = Get-Content -Encoding UTF8 $Changelog
$header = '^##\s+\[' + [regex]::Escape($Version) + '\]'
$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match $header) { $start = $i + 1; break }
}
if ($start -lt 0) { throw "No '## [$Version]' section in $Changelog" }

$body = New-Object System.Collections.Generic.List[string]
for ($i = $start; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^##\s+\[' -or $lines[$i] -match '^\[[^\]]+\]:\s') { break }
    $body.Add($lines[$i])
}
$text = ($body -join "`n").Trim()
if (-not $text) { throw "Section '## [$Version]' in $Changelog is empty" }

if ($OutFile) {
    [IO.File]::WriteAllText($OutFile, $text + "`n", (New-Object Text.UTF8Encoding $false))
} else {
    $text
}
