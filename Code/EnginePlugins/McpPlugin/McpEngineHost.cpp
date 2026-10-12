#include <McpPlugin/McpPluginPCH.h>

#include <McpPlugin/McpEngineHost.h>

#include <Mcp/McpServer.h>
#include <Mcp/McpToolRegistry.h>

#include <Foundation/Application/Application.h>
#include <Foundation/Utilities/CommandLineUtils.h>

/// The editor's port, read here only to derive one from it.
///
/// Declared so that it shows up in app_command_line_options, and so that the +1 rule is stated in one
/// place that an agent can actually read. The editor is what interprets the value itself.
static ezCommandLineOptionInt s_opt_EditorMcpPort("_Mcp", "-editor-mcpport",
  "The editor's own MCP port. This process is started by the editor with the editor's whole command line,\n"
  "so when '-mcpport' is absent this is what it derives its port from: editor port + 1.",
  0, 0, 0xFFFF);

ezMcpServer* ezMcpEngineHost::s_pServer = nullptr;
ezUInt64 ezMcpEngineHost::s_uiFrameCount = 0;
bool ezMcpEngineHost::s_bQuitRequested = false;

void ezMcpEngineHost::Startup()
{
  if (s_pServer != nullptr)
    return;
  ezGameApplicationBase* pApp = ezGameApplicationBase::GetGameApplicationBaseInstance();

  if (pApp == nullptr)
  {
    ezLog::Warning("MCP: no ezGameApplicationBase, so there is no frame loop to answer requests from. Not starting the server.");
    return;
  }

  ezRuntimeControlOptions options;
  const ezStatus parsed = ezRuntimeControlOptions::Parse(*ezCommandLineUtils::GetGlobalInstance(), options);
  if (parsed.Failed())
  {
    pApp->ReportStartupFailure(parsed.GetMessageString().GetView());
    return;
  }
  if (!options.m_bEnabled || pApp->ShouldApplicationQuit())
    return;

  s_uiFrameCount = 0;
  s_bQuitRequested = false;
  s_pServer = EZ_DEFAULT_NEW(ezMcpServer, "ezEngine");

  if (s_pServer->Start(options.m_uiPort).Failed())
  {
    EZ_DEFAULT_DELETE(s_pServer);
    if (options.m_bRequired)
      pApp->ReportStartupFailure(ezFmt("Required MCP transport could not listen on port {}.", options.m_uiPort));
    else
      ezLog::Error("MCP: could not listen on port {}. The game runs without an MCP server.", options.m_uiPort);
    return;
  }

  const ezStatus reported = pApp->ReportBoundControlPort(s_pServer->GetPort());
  if (reported.Failed())
  {
    EZ_DEFAULT_DELETE(s_pServer);
    pApp->ReportStartupFailure(reported.GetMessageString().GetView());
    return;
  }
  ezLog::Info("MCP: listening on 127.0.0.1:{}.", s_pServer->GetPort());
  pApp->m_ExecutionEvents.AddEventHandler(&ezMcpEngineHost::ExecutionEventHandler);

  // The editor's engine process sleeps in its IPC wait until the editor sends it something, and an
  // arriving MCP request is not something the editor knows about. Without this, a call to an idle
  // engine process would simply never be answered. ezPlayer free-runs and ignores this.
  pApp->m_HasPendingExternalWork = []() -> bool
  {
    return s_pServer != nullptr && s_pServer->HasPendingRequest();
  };
}

void ezMcpEngineHost::Shutdown()
{
  if (s_pServer != nullptr)
  {
    ezGameApplicationBase* pApp = ezGameApplicationBase::GetGameApplicationBaseInstance();
    if (pApp != nullptr)
    {
      pApp->m_ExecutionEvents.RemoveEventHandler(&ezMcpEngineHost::ExecutionEventHandler);
      pApp->m_HasPendingExternalWork = {};
    }

    // Stop() joins the reply-writing transport. Do not clear the report or destroy providers first.
    EZ_DEFAULT_DELETE(s_pServer);
    if (pApp != nullptr)
      pApp->ClearBoundControlPort();
  }
  // Project providers must die while their DLLs are still loaded, including optional bind failure.
  // Plugin unload occurs later, in reverse load order (project plugins before McpPlugin).
  ezMcpToolRegistry::Clear();
}

void ezMcpEngineHost::RequestQuit()
{
  s_bQuitRequested = true;
}

void ezMcpEngineHost::ExecutionEventHandler(const ezGameApplicationExecutionEvent& e)
{
  if (e.m_Type == ezGameApplicationExecutionEvent::Type::BeforeWorldUpdates)
  {
    // Before the world ticks, but after this tick's input update. Values written here can contribute
    // to the next input-manager update; answering a call does not establish input consumption.
    s_pServer->ProcessPendingRequests();
    return;
  }

  // Counted on present rather than at the end of the tick, because 'a frame happened' has to mean 'a
  // frame was rendered'. The editor's engine process runs its loop continuously but only renders when
  // the editor asks it to, so an app tick is not evidence of anything - and a screenshot that waits for
  // a frame would then be told frames were passing while none were.
  if (e.m_Type == ezGameApplicationExecutionEvent::Type::BeforePresent)
  {
    ++s_uiFrameCount;
    return;
  }

  if (e.m_Type == ezGameApplicationExecutionEvent::Type::EndAppTick)
  {
    if (s_bQuitRequested)
    {
      // One full frame after app_quit was answered. The response left the socket at the point above,
      // and Shutdown() joins the transport thread anyway, so nothing is lost either way.
      ezApplication::GetApplicationInstance()->QuitApplication();
    }
  }
}
