# Run with both Windows PowerShell and PowerShell: exercises only synthetic, job-owned processes.
param([Parameter(Mandatory=$true)][string]$OutputDir, [string]$Child = '')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ReleaseTestCommon.ps1"
$shell = (Get-Process -Id $PID).Path
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null
if ($Child) {
    # Deliberately not a second owned launch: this descendant must inherit its parent's job.
    $descendant = Start-Process $shell -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep -Seconds 60') -PassThru
    $descendant.Id | Set-Content (Join-Path $OutputDir 'child.pid')
    [Console]::WriteLine('parent output')
    [Console]::Error.WriteLine('parent error')
    if ($Child -eq 'Exit') { exit 7 }
    Start-Sleep -Seconds 60
    exit 0
}
$owned = @()
$childHandles = @()
try {
    foreach ($code in @(0, 7)) {
        $result = Invoke-EzProcess -Exe $shell -Arguments @('-NoProfile', '-Command', "[Console]::WriteLine('out'); [Console]::Error.WriteLine('err'); exit $code") -TimeoutSeconds 10
        if ($result.ExitCode -ne $code -or $result.TimedOut -or $result.StdOut.Trim() -ne 'out' -or $result.StdErr.Trim() -ne 'err') {
            throw 'Native status/output was not preserved.'
        }
    }
    $sentinel = Start-EzProcessDetached -Exe $shell -Arguments @('-NoProfile', '-Command', 'Start-Sleep -Seconds 60')
    $owned += $sentinel
    if (-not ('EzReleaseTests.OwnedProcessTestProbe' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'OwnedProcessTestProbe.cs') }
    [EzReleaseTests.OwnedProcessTestProbe]::VerifyDeniedTermination($sentinel.EzOwner)
    if ($sentinel.HasExited) { throw 'Denied cleanup unexpectedly terminated its process.' }
    foreach ($mode in @('Exit', 'Wait')) {
        $dir = Join-Path $OutputDir $mode
        $parent = Start-EzProcessDetached -Exe $shell -Arguments @('-NoProfile', '-File', $PSCommandPath, '-OutputDir', $dir, '-Child', $mode)
        $owned += $parent
        if (!(Wait-ForCondition -TimeoutSeconds 10 -Condition { Test-Path (Join-Path $dir 'child.pid') })) { throw 'Child did not start.' }
        $childProcess = Get-Process -Id ([int](Get-Content (Join-Path $dir 'child.pid')))
        $null = $childProcess.Handle # Retain identity rather than rediscovering a possibly reused PID.
        $childHandles += $childProcess
        if ($mode -eq 'Exit' -and !$parent.WaitForExit(10000)) { throw 'Parent did not exit.' }
        if ($childProcess.HasExited) { throw 'Probe child did not survive until cleanup.' }
        Stop-EzProcessTree -Process $parent
        if (!$childProcess.WaitForExit(10000)) { throw 'Descendant survived root cleanup.' }
        if ($mode -eq 'Exit' -and $parent.ExitCode -ne 7) { throw 'Root exit status changed during cleanup.' }
        Save-DetachedProcessOutput -Process $parent -LogFile (Join-Path $dir 'output.log')
        $text = Get-Content (Join-Path $dir 'output.log') -Raw
        if ($text -notmatch 'parent output' -or $text -notmatch 'parent error') { throw 'Cleanup lost stream output.' }
        if ($sentinel.HasExited) { throw 'Cleanup terminated an unrelated owned job.' }
    }
    $timeout = Invoke-EzProcess -Exe $shell -Arguments @('-NoProfile', '-File', $PSCommandPath, '-OutputDir', (Join-Path $OutputDir 'Timeout'), '-Child', 'Wait') -TimeoutSeconds 5
    if (!$timeout.TimedOut -or $timeout.ExitCode -ne -999 -or $timeout.StdOut -notmatch 'parent output') { throw 'Timeout result/output incorrect.' }
    $early = Invoke-EzProcess -Exe $shell -Arguments @('-NoProfile', '-File', $PSCommandPath, '-OutputDir', (Join-Path $OutputDir 'EarlyExit'), '-Child', 'Exit') -TimeoutSeconds 10
    if ($early.TimedOut -or $early.ExitCode -ne 7 -or $early.StdErr -notmatch 'parent error') { throw 'Early exit result/output incorrect.' }
    $failedClosed = $false
    $marker = Join-Path $OutputDir 'must-not-run.txt'
    try {
        $unexpected = Start-EzProcessDetached -Exe $shell -WorkingDirectory (Join-Path $OutputDir ([Guid]::NewGuid().ToString())) -Arguments @('-NoProfile', '-Command', "Set-Content '$marker' ran")
        $owned += $unexpected
    } catch {
        if ($_.Exception.ToString() -notmatch 'CreateProcess.*Win32') { throw }
        $failedClosed = $true
    }
    if (!$failedClosed -or (Test-Path $marker)) { throw 'Invalid setup did not fail closed.' }
    $refused = $false
    try { Stop-EzProcessTree -Process (Get-Process -Id $PID) } catch {
        if ($_.Exception.Message -notmatch 'unowned') { throw }
        $refused = $true
    }
    if (!$refused) { throw 'Unowned cleanup incorrectly reported success.' }
    'PASS: success/nonzero output, early-root exit, detached tree, timeout tree, separate-job safety, failed setup, denied termination and ownership diagnostics.'
}
finally {
    $errors = @()
    foreach ($process in $owned) {
        try { Stop-EzProcessTree -Process $process } catch { $errors += $_ }
        try { $process.EzOwner.Dispose() } catch { $errors += $_ }
    }
    foreach ($childProcess in $childHandles) { $childProcess.Dispose() }
    if ($errors.Count) { throw ($errors -join "`n") }
}
