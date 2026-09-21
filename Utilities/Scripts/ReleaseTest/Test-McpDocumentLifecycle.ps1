# Exercises real editor request dispatch without requiring a sample scene or external assets.
# Example: ./Test-McpDocumentLifecycle.ps1 -SdkDir <checkout> -BinDir <Debug-binaries> -OutputDir <ignored-results>
# Creates a fresh project below OutputDir; does not modify an existing project. Requires editor,
# editor-engine and MCP binaries. Results use the same JSON report as the release smoke tests.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SdkDir,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$BinDir = '',
    [ValidateRange(1024, 65534)][int]$EditorPort = 7499
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ReleaseTestCommon.ps1"
$SdkDir = (Resolve-Path $SdkDir).Path
$BinDir = Get-EzBinDir -SdkDir $SdkDir -BinDir $BinDir
$editor = Get-EzExe -BinDir $BinDir -ExeName 'ezEditor.exe'
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null
Initialize-TestGroup -Group 'McpDocumentLifecycle' -OutputDir $OutputDir
$project = Join-Path $OutputDir ('Project-' + [Guid]::NewGuid().ToString('N'))
$process = $null

function Call-Editor {
    param([string]$Tool, [hashtable]$Arguments = @{})
    @{tool=$Tool; arguments=$Arguments} | ConvertTo-Json -Depth 10 -Compress |
        Add-Content (Join-Path $OutputDir 'requests.jsonl')
    Get-McpToolJson -Port $EditorPort -Tool $Tool -Arguments $Arguments -TimeoutSeconds 30
}

try {
    if ((Test-McpPortOpen -Port $EditorPort) -or (Test-McpPortOpen -Port ($EditorPort + 1))) {
        throw 'Requested editor/engine port is already occupied.'
    }
    $process = Start-EzProcessDetached -Exe $editor -WorkingDirectory $SdkDir -Arguments @(
        '-createProject', $project, '-unattended', '-editor-mcpport', "$EditorPort", '-appid', "$EditorPort"
    )
    if (-not (Wait-ForCondition -TimeoutSeconds 120 -Condition { Test-McpPortOpen -Port $EditorPort })) {
        throw 'Editor did not start its MCP endpoint.'
    }
    $info = Call-Editor 'app_info'
    if ($info.processId -ne $process.Id) { throw 'Endpoint belongs to another process.' }
    # Discover the actual schemas before the calls and keep them with the local evidence.
    Invoke-McpRequest -Port $EditorPort -Method 'tools/list' -TimeoutSeconds 30 |
        ConvertTo-Json -Depth 40 | Set-Content (Join-Path $OutputDir 'tools.json')

    Invoke-TestCheck -Name 'Focused document lifecycle and idle dispatch' -Check {
        for ($iteration = 0; $iteration -lt 3; ++$iteration) {
            $path = Join-Path $project "Lifecycle-$iteration.ezPrefab"
            $created = Call-Editor 'document_create' @{path=$path; type='Prefab'; empty=$true; focus=$true}
            if (-not (Test-Path $path)) { throw 'Creation did not save the document.' }
            if ($iteration -eq 0) {
                $engineTools = Invoke-McpRequest -Port $info.engineMcpPort -Method 'tools/list' -TimeoutSeconds 30
                if ('app_info' -notin @($engineTools.tools.name)) { throw 'Engine MCP app_info is absent.' }
                $engineInfo = Get-McpToolJson -Port $info.engineMcpPort -Tool 'app_info' -TimeoutSeconds 30
                if ($engineInfo.processId -eq $process.Id) { throw 'Engine endpoint is not a separate process.' }
            }
            Call-Editor 'document_focus' @{document=$created.guid} | Out-Null
            Call-Editor 'document_close' @{document=$created.guid} | Out-Null
            Call-Editor 'app_ping' | Out-Null
            $opened = Call-Editor 'document_open' @{path=$path; focus=$true}
            Call-Editor 'document_close' @{document=$opened.guid} | Out-Null
            # No document window remains: servicing must not depend on redraw requests.
            Call-Editor 'app_ping' | Out-Null
        }
        return 'Three create/focus/close/open/close cycles, with idle pings.'
    }

    Invoke-TestCheck -Name 'Exposed property UUID authoring is transactional and persistent' -Check {
        $path = Join-Path $project 'Exposed.ezPrefab'
        $doc = (Call-Editor 'document_create' @{path=$path; type='Prefab'; empty=$true; focus=$true}).guid
        Call-Editor 'selection_set' @{document=$doc; objects=@()} | Out-Null
        Call-Editor 'action_execute' @{document=$doc; name='Selection.CreateEmptyChildObject'} | Out-Null
        function Flatten-Objects($objects) {
            foreach ($node in $objects) {
                $node
                if ($node.PSObject.Properties['children']) { Flatten-Objects $node.children }
            }
        }
        $nodes = @(Flatten-Objects (Call-Editor 'object_tree' @{document=$doc; depth=5}).objects)
        $nodes | ConvertTo-Json -Depth 30 | Set-Content (Join-Path $OutputDir 'exposed-tree.json')
        $root = @($nodes | Where-Object type -eq 'ezGameObject')[0].guid
        $settings = @($nodes | Where-Object type -eq 'ezPrefabDocumentSettings')[0].guid
        $component = (Call-Editor 'object_modify' @{document=$doc; object=$root; property='Components'; operation='addObject'; type='ezBoxReflectionProbeComponent'}).addedObject
        $nodes = @(Flatten-Objects (Call-Editor 'object_tree' @{document=$doc; depth=5}).objects)
        $component = @($nodes | Where-Object guid -eq $component)[0].guid
        Call-Editor 'object_modify' @{document=$doc; object=$root; property='LocalPosition'; value=@{x=1; y=2; z=3}} | Out-Null
        $position = (Call-Editor 'object_properties' @{document=$doc; object=$root; property='LocalPosition'}).properties[0].value
        if ($position.x -ne 1 -or $position.y -ne 2 -or $position.z -ne 3) { throw 'Composite property regression.' }
        Call-Editor 'object_modify' @{document=$doc; object=$component; property='Active'; value=$false} | Out-Null
        $active = (Call-Editor 'object_properties' @{document=$doc; object=$component; property='Active'}).properties[0].value
        if ($active -ne $false) { throw 'Scalar property regression.' }
        $exposed = (Call-Editor 'object_modify' @{document=$doc; object=$settings; property='ExposedProperties'; operation='addObject'; type='ezExposedSceneProperty'}).addedObject
        function Set-Exposed($property, $value) {
            Call-Editor 'object_modify' @{document=$doc; object=$exposed; property=$property; value=$value}
        }
        function Read-Exposed {
            (Call-Editor 'object_properties' @{document=$doc; object=$exposed; property='Object'}).properties[0].value
        }
        Set-Exposed 'Name' 'ProbeEnabled' | Out-Null
        Set-Exposed 'PropertyPath' 'Active' | Out-Null
        $initial = Read-Exposed
        Set-Exposed 'Object' $component.ToUpperInvariant() | Out-Null
        if ((Read-Exposed) -ne $component) { throw 'Uppercase UUID did not round trip.' }
        Set-Exposed 'Object' $initial | Out-Null
        Set-Exposed 'Object' $component | Out-Null
        if ((Read-Exposed) -ne $component) { throw 'UUID assignment did not round trip.' }
        $history = Call-Editor 'object_undo' @{document=$doc} | ConvertTo-Json -Depth 10 -Compress
        foreach ($bad in @('', 'not-a-guid', '{ 00000000-0000-0000-0000-00000000000Z }', '{ Z0000000-0000-0000-0000-000000000000 }', $component.Replace('-', '_'), 42, @{x=1})) {
            $response = Invoke-McpRequest -Port $EditorPort -Method 'tools/call' -TimeoutSeconds 30 -Params @{name='object_modify'; arguments=@{document=$doc; object=$exposed; property='Object'; value=$bad}}
            if (!$response.PSObject.Properties['isError'] -or !$response.isError) { throw "Malformed UUID was accepted: $($bad | ConvertTo-Json -Compress)" }
            if (($response.content.text -join ' ') -match 'failed assert') { throw 'Malformed UUID triggered an assertion rather than validation.' }
            if ((Read-Exposed) -ne $component) { throw 'Rejected UUID changed the property.' }
            $after = Call-Editor 'object_undo' @{document=$doc} | ConvertTo-Json -Depth 10 -Compress
            if ($history -ne $after) { throw 'Rejected UUID changed command history.' }
        }
        Call-Editor 'object_undo' @{document=$doc; action='undo'} | Out-Null
        if ((Read-Exposed) -ne $initial) { throw 'UUID undo did not restore original value.' }
        Call-Editor 'object_undo' @{document=$doc; action='redo'} | Out-Null
        if ((Read-Exposed) -ne $component) { throw 'UUID redo did not restore assigned value.' }
        Call-Editor 'document_save' @{document=$doc} | Out-Null
        Call-Editor 'document_close' @{document=$doc} | Out-Null
        $doc = (Call-Editor 'document_open' @{path=$path; focus=$true}).guid
        if ((Read-Exposed) -ne $component) { throw 'Saved UUID did not survive reload.' }
        $properties = (Call-Editor 'object_properties' @{document=$doc; object=$exposed}).properties
        if (($properties | Where-Object name -eq 'Name').value -ne 'ProbeEnabled' -or
            ($properties | Where-Object name -eq 'PropertyPath').value -ne 'Active') { throw 'Exposed property metadata did not survive reload.' }
        $nodes = @(Flatten-Objects (Call-Editor 'object_tree' @{document=$doc; depth=5}).objects)
        if (@($nodes | Where-Object guid -eq $component).Count -ne 1) { throw 'Saved reference target is missing.' }
        Call-Editor 'document_close' @{document=$doc} | Out-Null
        return 'Component GUID authored through reflected exposed property; malformed input rejected without value/history mutation; undo/redo/save/reload passed.'
    }

    Invoke-TestCheck -Name 'Editor shutdown acknowledges request and exits' -Check {
        Call-Editor 'app_quit' | Out-Null
        if (-not (Wait-ForCondition -TimeoutSeconds 60 -Condition { $process.HasExited })) {
            throw 'Editor did not exit after app_quit.'
        }
        if ($process.ExitCode -ne 0) { throw "Editor exit code: $($process.ExitCode)" }
        return 'Shutdown response received; editor exited normally.'
    }
}
catch {
    Add-TestResult -Name 'Host setup' -Status 'FAIL' -Message $_.Exception.Message
}
finally {
    try { Stop-EzProcessTree -Process $process }
    finally {
        try { Save-DetachedProcessOutput -Process $process -LogFile (Join-Path $OutputDir 'editor.log') }
        finally { if ($null -ne $process) { $process.EzOwner.Dispose() } }
    }
}
exit (Save-TestResults)
