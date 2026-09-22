# Optional debug services

Ordinary local editor and development game startup does not enable telemetry, MCP or Tracy listeners.
Local editor/engine communication still uses named pipes. Explicit remote connections and direct
network API use are unaffected; this is not a blanket prohibition on application networking.

## Telemetry / ezInspector

Development applications based on `ezGameApplicationBase` (including ezPlayer and editor engine
processes) require **`-telemetry`** to start telemetry. `-telemetry false` leaves it disabled.
**`-TelemetryPort <port>` only selects a port; it never enables telemetry by itself.**

For example, launch `ezPlayer -project <project> -scene <scene> -telemetry -TelemetryPort 1040`,
then connect ezInspector to port 1040. For the editor's primary engine process, launch
`ezEditor -project <project> -telemetry -TelemetryPort 1050` before using its Inspector action.
The standalone default is 1040; the editor supplies 1050 for its engine process if not specified.
Use distinct ports for concurrent explicitly enabled hosts. Telemetry binds UDP on all interfaces.
Release builds do not start development telemetry; builds without ENet warn on explicit opt-in
and do not start a listener.

The editor forwards its arguments to its primary engine child. Background asset processors do not
inherit the editor's telemetry or MCP opt-in; their local engine children stay offline by default.
A directly launched processor given `-telemetry` forwards it to its own engine child.
Test hosts based on the game application inherit the same policy. Network-specific tests and
applications explicitly calling the network APIs still control their own connections.

## MCP automation

Launch the editor with **`-editor-mcpport <port>`** to enable its project-scoped loopback MCP server.
Pass a numeric port explicitly so clients and the engine child agree on the endpoint. The existing
editor fallback of 7391 applies only when that option is supplied without a value, not at normal startup.
Its engine child uses editor port + 1, or explicit **`-mcpport <port>`** to override that derivation.
A standalone game uses `-mcpport <port>`. MCP and telemetry opt-ins are independent.

These defaults prevent unnecessary listeners; they do not change firewall permissions, dismiss
existing security prompts, or provide stable executable identity across changing output paths.
Intentionally enabled services may still require network permission.

## Tracy and local profiling

Development hosts require **`-tracy`** to start the process-wide Tracy network profiler. This is
independent of telemetry and MCP. The editor forwards the option to its primary engine; background
asset workers do not inherit it. Directly launched players, processors and test hosts may opt in.
Tracy retains its supported port discovery (starting at TCP 8086) and `TRACY_PORT` environment setting.
An explicitly enabled host also broadcasts discovery over UDP. A port environment setting alone
does not start Tracy.

Built-in `EZ_PROFILE_*` CPU scopes, frame timing, GPU profiling and JSON capture remain available
without a network profiler. Tracy-only zones, plots, messages and optional allocation tracking are
inactive until startup opt-in, so a Tracy client cannot attach to an ordinary default launch later.
Restart with `-tracy` for that workflow. Early events before core-system startup are not sent to Tracy.
The integration uses Tracy's supported manual lifetime and guards inactive instrumentation; it does
not compile out all profiling. Like the previous delayed-initialization mode, an activated Tracy
instance lasts until process exit, including across core-system restarts.
Plugins using raw Tracy APIs must guard inactive calls with `TracyIsStarted`; prefer the guarded
`EZ_PROFILE_*` integration. Startup opt-in is not a runtime pause/resume switch.
