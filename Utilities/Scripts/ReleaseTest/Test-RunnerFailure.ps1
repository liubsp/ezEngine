# Exercises the public runner as a subprocess, including failures outside Invoke-TestCheck.
param([Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ReleaseTestCommon.ps1"
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$shell = (Get-Process -Id $PID).Path
$sdk = Join-Path $OutputDir 'Sdk'
$bin = Join-Path $sdk 'Bin'
New-Item -ItemType Directory -Force $bin | Out-Null
# The real Tools group records the missing first tool, then aborts launching this invalid image.
Set-Content (Join-Path $bin 'ezShaderCompiler.exe') 'Not an executable image.'
Set-Content (Join-Path $bin 'ezPlayer.exe') 'Not an executable image.'

function Check-Runner($runner, $name, $expectedFailure, $expectedMessage, $expectedPartial) {
    $resultsDir = Join-Path $OutputDir $name
    $result = Invoke-EzProcess -Exe $shell -Arguments @('-NoProfile', '-File', $runner,
        '-SdkDir', $sdk, '-BinDir', $bin, '-OutputDir', $resultsDir, '-Tools') -TimeoutSeconds 30
    $result.StdOut + $result.StdErr | Set-Content (Join-Path $OutputDir "$name.log")
    if ($result.TimedOut -or (($result.ExitCode -ne 0) -ne $expectedFailure)) { throw "$name returned incorrect final status: $($result.ExitCode)" }
    $parsed = Get-Content (Join-Path $resultsDir 'Results.json') -Raw | ConvertFrom-Json
    $rows = @($parsed)
    $failed = @($rows | Where-Object Status -eq 'FAIL')
    $summary = Get-Content (Join-Path $resultsDir 'Summary.md') -Raw
    if ($expectedFailure) {
        if (!$failed.Count -or !($failed.Message -match $expectedMessage) -or $summary -notmatch '[1-9][0-9]* failed') {
            throw "$name omitted the failure from JSON/summary."
        }
    } elseif ($failed.Count -or @($rows | Where-Object Status -eq 'SKIP').Count -ne 1) { throw 'Genuine skip changed semantics.' }
    if ($expectedPartial -and !($rows.Name -contains $expectedPartial)) { throw "$name lost partial results." }
    "$name : exit=$($result.ExitCode), JSON failures=$($failed.Count), summary verified"
}

Check-Runner (Join-Path $PSScriptRoot 'Run-ReleaseTests.ps1') 'InvalidLaunch' $true 'CreateProcess' 'ezTexConv.exe -help'

# Use an unchanged copy of the runner with disposable group scripts. This injects group-boundary
# failures without adding fault switches to the production runner or launching real SDK tools.
$fixture = Join-Path $OutputDir 'RunnerFixture'
New-Item -ItemType Directory -Force $fixture | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'Run-ReleaseTests.ps1'), (Join-Path $PSScriptRoot 'ReleaseTestCommon.ps1') $fixture
$prefix = @'
param($SdkDir, $OutputDir, $BinDir)
. "$PSScriptRoot/ReleaseTestCommon.ps1"
Initialize-TestGroup -Group Tools -OutputDir $OutputDir
'@
$cases = @(
    @{Name='CleanupAbort'; Body="Add-TestResult -Name 'Completed check' -Status PASS`nAdd-TestResult -Name 'Legitimate skip' -Status SKIP`nStop-EzProcessTree -Process (Get-Process -Id `$PID)"; Fails=$true; Message='unowned'; Partial='Completed check'},
    @{Name='NonzeroExit'; Body="Add-TestResult -Name 'Completed check' -Status PASS`nSave-TestResults | Out-Null`nexit 7"; Fails=$true; Message='7'; Partial='Completed check'},
    @{Name='MissingResults'; Body='exit 0'; Fails=$true; Message='no results'; Partial=''},
    @{Name='GenuineSkip'; Body="Add-TestResult -Name 'Legitimate skip' -Status SKIP`nexit (Save-TestResults)"; Fails=$false; Message=''; Partial='Legitimate skip'}
)
foreach ($case in $cases) {
    Set-Content (Join-Path $fixture 'Test-Tools.ps1') ($prefix + "`n" + $case.Body)
    Check-Runner (Join-Path $fixture 'Run-ReleaseTests.ps1') $case.Name $case.Fails $case.Message $case.Partial
}
