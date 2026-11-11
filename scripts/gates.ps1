<#
.SYNOPSIS
  Runs the AxSynth release gates against a Release build.

.DESCRIPTION
  Gates: unit (unit + smoke tests), realtime, pluginval (strictness 10), validator (Steinberg VST3 validator).
  Any failing gate exits non-zero. Used by CI (one gate per step) and locally (all gates).

.EXAMPLE
  powershell -File scripts/gates.ps1 -Pluginval tools/pluginval/pluginval.exe -Validator tools/vst3sdk-build/bin/validator.exe
  powershell -File scripts/gates.ps1 -Gate unit
#>
param(
    [ValidateSet('all', 'unit', 'realtime', 'pluginval', 'validator')]
    [string] $Gate = 'all',
    [string] $BuildDir = 'build',
    [string] $Pluginval = 'tools/pluginval/pluginval.exe',
    [string] $Validator = 'tools/vst3-validator/validator.exe',
    [string] $LogDir = 'gate-logs'
)

# Continue: native tools write to stderr; failures are detected via exit codes.
$ErrorActionPreference = 'Continue'
$failed = @()

$bundle = Join-Path $BuildDir 'AxSynth_artefacts/Release/VST3/AxSynth.vst3'
New-Item -ItemType Directory -Force $LogDir | Out-Null

function Invoke-Gate([string] $name, [scriptblock] $body) {
    Write-Host "=== Gate: $name ==="
    $global:LASTEXITCODE = 0
    & $body
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host "::error::Gate '$name' failed (exit code $code)"
        $script:failed += $name
    } else {
        Write-Host "Gate '$name' passed"
    }
}

function Assert-File([string] $path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing: $path" }
}

if ($Gate -in 'all', 'unit') {
    $exe = Join-Path $BuildDir 'Tests/AxSynthTests_artefacts/Release/AxSynthTests.exe'
    Assert-File $exe
    Invoke-Gate 'unit' { & $exe 2>&1 | Tee-Object -FilePath (Join-Path $LogDir 'unit.log') }
}

if ($Gate -in 'all', 'realtime') {
    $exe = Join-Path $BuildDir 'Tests/AxSynthRealtimeTest_artefacts/Release/AxSynthRealtimeTest.exe'
    Assert-File $exe
    Invoke-Gate 'realtime' { & $exe 2>&1 | Tee-Object -FilePath (Join-Path $LogDir 'realtime.log') }
}

if ($Gate -in 'all', 'pluginval') {
    Assert-File $Pluginval
    Assert-File $bundle
    # Full strictness 10 run including editor tests (Windows runners have a desktop session).
    Invoke-Gate 'pluginval' {
        & $Pluginval --strictness-level 10 --validate-in-process --timeout-ms 600000 `
            --output-dir $LogDir --validate $bundle 2>&1 |
            Tee-Object -FilePath (Join-Path $LogDir 'pluginval-console.log')
    }
}

if ($Gate -in 'all', 'validator') {
    Assert-File $Validator
    Assert-File $bundle
    $log = Join-Path $LogDir 'vst3-validator.log'
    Invoke-Gate 'validator' {
        & $Validator $bundle 2>&1 | Tee-Object -FilePath $log
        $code = $LASTEXITCODE
        # Belt and braces: also fail on any reported failed test.
        $text = Get-Content -Raw $log
        if ($text -notmatch 'Result: \d+ tests passed, 0 tests failed' -or $text -match 'tests passed, [1-9]\d* tests failed') {
            Get-Content $log | Select-String -Pattern 'ERROR|Failed' | ForEach-Object { Write-Host "::error::$_" }
            if ($code -eq 0) { $global:LASTEXITCODE = 1 }
        }
    }
}

if ($failed.Count -gt 0) {
    Write-Host "Failed gates: $($failed -join ', ')"
    exit 1
}
Write-Host 'All requested gates passed.'
exit 0
