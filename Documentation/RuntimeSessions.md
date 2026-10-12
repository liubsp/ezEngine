# Isolated Development runtime sessions

Windows desktop Debug/Dev DLL builds of applications based on `ezGameApplication` can opt into private
writable storage with `-runtimeSession <absolute-directory>`. The directory must already
exist, belong to the caller and be a fresh ordinary local-drive directory. UNC/device paths,
reparse-routed paths, ambiguous Win32 names and uninspectable trees are rejected. Existing
contents are inspected without following reparse points, up to 4096 entries and 64 levels.
Release, static builds, other platforms and Live++ enabled application startup do not support this option.

This is trusted-local isolation, not a sandbox for arbitrary plugins or hostile filesystem
mutation. Do not add links or change the directory tree underneath a running session.
OS and GPU-driver caches are not virtualized. Preparation/build/asset transformation must
finish before starting consumers of those outputs.

## Storage and startup

Selection happens before the application's text logger, base startup and plugin loading.
Player help with controlled or unattended arguments writes to inherited stdout instead of
opening a dialog. Player validates project/scene option cardinality and project discovery
before entering base startup. An invalid scene resource discovered later remains the game
state's responsibility; early validation cannot resolve asset GUIDs before project mounts.

After validation, the engine holds an exclusive `ezOSFile` handle to `runtime-owner.lock`.
It never deletes that ownership file. A conflicting owner fails before other store writes.
The handle survives shutdown, plugin unload, log closure and derived-member destruction.
Private directories are checked for write access before any logger starts. Failed selection
or initialization returns a nonzero application exit code; it never falls back to normal stores.

| Output | Session location |
| --- | --- |
| `:appdata` (CVars, window settings, console history, HTML log, captures) | `appdata/` |
| Writable `:bin` | `bin/` |
| `:shadercache` and Vulkan's engine-owned pipeline cache | `shadercache/` |
| Default startup text log | `logs/runtime.log` |
| Relative OS writes, including ImGui defaults | Session directory, installed as CWD |

Explicit `-logfile` and `-screenshot` paths must resolve inside the session. Relative values
are relative to that root, not the launcher's previous CWD. CWD is not restored at shutdown.
The executable directory, SDK detection and read-only resource discovery keep their ordinary
meaning. The project mount is read-only even when authored writable for the editor. Other
configured writable mounts must resolve inside the session; missing/malformed mount and
plugin configuration is fatal. Absolute and special-directory paths in filesystem configuration
are preserved, while legacy relative entries remain SDK-relative.

Telemetry and fileserve activation are incompatible with session startup. Loading FileservePlugin
also fails before it installs its mount factory or opens its shared caches. Controlled project
plugins load their original binaries, ignoring configured `LoadCopy`: there is no hot reload while
consuming prepared output. Declared dependency load failures propagate to the requesting plugin.
Unloading an original binary does not delete another process's `.loaded` sidecar.

Without `-runtimeSession`, normal store locations, application identity and project-plugin
copy settings are unchanged. MCP does not imply storage isolation, and storage isolation does
not imply MCP.

## Application and provider interfaces

`ezGameApplicationBase::ConfigureRuntimeSession(root)` is the status-returning programmatic
entry point. Call it before startup, check its result, and do not also pass the command-line
selection. `GetRuntimeSession()` exposes read-only selection and `ResolveOutputPath()` for
application-owned outputs. No global user/temp-folder override is installed. Applications
using custom device creators must honor the creation description's `m_sPipelineCacheDirectory`.

Void initialization hooks report fatal errors through `ReportStartupFailure()`. The base checks
for quit between initialization stages and after high-level startup, before game-state activation.
The graphics-device path reports creation failure rather than continuing with a missing device.

Control intent is parsed by Core's `ezRuntimeControlOptions`, even when McpPlugin is missing:

- No own/editor port: no listener.
- Explicit `-mcpport 0`: loopback ephemeral port.
- Explicit decimal own port in `[0, 65535]`: overrides the editor fallback; malformed,
  duplicate or out-of-range values fail startup.
- No own port and a valid editor port in `[1, 65534]`: editor port plus one.
- `-mcpRequired` requires an explicit own port and makes missing transport/bind failure fatal.
  Optional bind failure still allows an ordinary application to run without control.

After binding, McpEngineHost calls `ReportBoundControlPort()` with the server's actual port.
Providers depend on Core/Mcp, not McpPlugin, and read `GetBoundControlPort()` for publication.
The report is write-once per serving lifetime and is cleared only after the host joins its transport.
Server startup refreshes reflected providers after project plugin loading. High-level shutdown
destroys providers before project DLL unload, including the optional-bind-failure path.
Application-specific provider presence, identity and rendered readiness are not established by
the port report; the application must check those before publishing readiness.

See [optional debug services](DebugServices.md) for ordinary editor and telemetry usage.
