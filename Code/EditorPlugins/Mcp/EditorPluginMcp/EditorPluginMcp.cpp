#include <EditorPluginMcp/EditorPluginMcpPCH.h>

#include <Mcp/McpServer.h>
#include <Mcp/McpToolRegistry.h>

#include <Foundation/Utilities/CommandLineOptions.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <QTimer>

/// The port to listen on.
///
/// Exists so that several editors can run at the same time, each answering on its own port: with a
/// single fixed port the second editor just fails to bind. That matters for automated testing, where
/// one editor is launched per project and has to be addressable independently of any other.
///
/// Spelled '-editor-mcpport' rather than '-mcpport' because the editor forwards its whole command line
/// to the engine process it starts, which serves MCP as well. One name for both would have meant both
/// processes reading the same number and fighting over the port; two names mean the engine process can
/// derive its own from this one without anything being passed explicitly.
///
/// The default is what a client connects to when nothing else was agreed on, so changing it means
/// changing the documented URL as well (see the 'ez-mcp' skill). Ports below 1024 are excluded
/// because they need elevation on most systems, and 0 because 'any free port' cannot be put into a
/// client's URL.
static ezCommandLineOptionInt s_opt_McpPort("_Mcp", "-editor-mcpport",
  "The port that the editor's MCP server listens on, on 127.0.0.1.\n"
  "Pass a distinct port per editor to run several at the same time.\n"
  "The engine process that runs the game serves MCP on this port + 1, unless it is given '-mcpport'.",
  7391, 1024, 0xFFFF);

static ezMcpServer* s_pServer = nullptr;
static QTimer* s_pRequestTimer = nullptr;
static bool s_bProcessingRequest = false;
static bool s_bServerStatePending = false;
static ezUInt16 s_uiRequestedPort = 0;

static void ToolsProjectEventHandler(const ezToolsProjectEvent& e);
static void ApplyServerState();
static void ProcessRequests();

/// \brief Runs one tool call inside the editor's unattended mode, and turns a failed assert into an error.
///
/// This is the editor's half of what ezMcpToolRegistry used to do itself. It stays here, in the plugin,
/// because none of it means anything in a game process.
static void ExecuteWrapper(ezStringView sToolName, ezMcpToolResult& ref_result, ezDelegate<void()> execute)
{
  // Only for the duration of the call: the user typically *is* sitting in front of this editor, so their
  // own menu clicks must keep opening dialogs. It is the agent's call that must not block on one - see
  // ezQtDialog. What got suppressed is reported afterwards, otherwise a suppressed dialog is
  // indistinguishable from the operation having done nothing.
  ezQtScopedUnattended unattended;
  ezQtUiServices::ClearSuppressedDialogs();
  ezQtUiServices::ClearFailedAsserts();

  execute();

  // An assert that fires during a tool call no longer stops the editor - the handler installed by
  // ezQtUiServices records it and lets execution continue - but whatever the tool produced afterwards
  // was produced by code that had already declared its own assumptions broken. Reporting it as an error
  // is the only honest answer, and it replaces what used to be a hung editor and a lost session.
  const ezArrayPtr<const ezString> asserts = ezQtUiServices::GetFailedAsserts();

  if (!asserts.IsEmpty())
  {
    ezStringBuilder sError;
    sError.SetFormat("Tool '{}' triggered {} failed assert(s). Its result is not trustworthy and the editor may now be in a broken "
                     "state - restart it before relying on anything further. This is a bug in the editor or in the tool, not "
                     "something the arguments can be changed to avoid; report it with the text below.\n",
      sToolName, asserts.GetCount());

    for (const ezString& sAssert : asserts)
    {
      sError.AppendFormat("\n{}", sAssert);
    }

    ref_result.SetError(sError);
  }
}

void OnLoadPlugin()
{
  // A separate Qt event, not a TickEvent handler: tools can create or destroy document windows,
  // which subscribe to that event. A single-shot timer also stays disarmed while a tool runs a
  // nested event loop, so the still-pending transport request cannot be executed recursively.
  s_pRequestTimer = new QTimer();
  s_pRequestTimer->setSingleShot(true);
  s_pRequestTimer->setInterval(16);
  QObject::connect(s_pRequestTimer, &QTimer::timeout, s_pRequestTimer, &ProcessRequests);

  // s_Events is static, so this works before any project has been opened
  ezToolsProject::s_Events.AddEventHandler(ToolsProjectEventHandler);

  ezMcpToolRegistry::SetExecuteWrapper(&ExecuteWrapper);

  // create the tool providers already now, rather than when the server starts, so that the log tool
  // sees everything that happens during editor startup
  ezMcpToolRegistry::UpdateProviders();
}

void OnUnloadPlugin()
{
  ezToolsProject::s_Events.RemoveEventHandler(ToolsProjectEventHandler);

  // Editor plugins unload after the editor event loop has returned, not from inside their own
  // tool calls. Destroy the timer/context first to remove any pending Qt callback before unload.
  EZ_ASSERT_DEV(!s_bProcessingRequest, "Cannot unload the editor MCP plugin during a tool call.");
  delete s_pRequestTimer;
  s_pRequestTimer = nullptr;
  s_bServerStatePending = false;
  s_uiRequestedPort = 0;

  EZ_DEFAULT_DELETE(s_pServer);

  ezMcpToolRegistry::SetExecuteWrapper({});
  ezMcpToolRegistry::Clear();
}

EZ_PLUGIN_ON_LOADED()
{
  OnLoadPlugin();
}

EZ_PLUGIN_ON_UNLOADED()
{
  OnUnloadPlugin();
}

static void ProcessRequests()
{
  // Asset transforms pump Qt events while their document is still on the call stack. A tool
  // must not close or mutate that document from the progress dialog's nested event processing.
  if (ezQtEditorApp::GetSingleton()->IsProgressBarProcessingEvents())
  {
    s_pRequestTimer->start();
    return;
  }

  EZ_ASSERT_DEV(!s_bProcessingRequest, "MCP requests must not be dispatched recursively.");
  s_bProcessingRequest = true;
  if (s_pServer != nullptr)
  {
    s_pServer->ProcessPendingRequests();
  }
  s_bProcessingRequest = false;

  ApplyServerState();
  if (s_pServer != nullptr && s_uiRequestedPort != 0)
    s_pRequestTimer->start();
}

static void ApplyServerState()
{
  if (!s_bServerStatePending || s_bProcessingRequest)
    return;

  s_bServerStatePending = false;
  s_pRequestTimer->stop();
  if (s_uiRequestedPort == 0)
  {
    if (s_pServer != nullptr)
      s_pServer->Stop();
  }
  else
  {
    if (s_pServer == nullptr)
    {
      s_pServer = EZ_DEFAULT_NEW(ezMcpServer, "ezEditor");
    }

    if (s_pServer->Start(s_uiRequestedPort).Succeeded())
    {
      s_pRequestTimer->start();
    }
    else
    {
      // Said out loud because there is nothing else to notice it by: without a server there is no
      // log_read either, so an agent that cannot connect has no way to find out why. The usual cause is
      // another editor already holding the port.
      ezLog::Warning("MCP: The server could not listen on port {}. This editor is not reachable through MCP. Another editor may already "
                     "be using that port - pass a different '-editor-mcpport'.",
        s_uiRequestedPort);
      s_uiRequestedPort = 0;
    }
  }
}

static void ToolsProjectEventHandler(const ezToolsProjectEvent& e)
{
  if (e.m_Type == ezToolsProjectEvent::Type::ProjectOpened)
  {
    s_uiRequestedPort = static_cast<ezUInt16>(s_opt_McpPort.GetOptionValue(ezCommandLineOption::LogMode::FirstTimeIfSpecified));
  }
  else if (e.m_Type == ezToolsProjectEvent::Type::ProjectClosing)
  {
    s_uiRequestedPort = 0;
  }
  else
    return;

  // Stop joins the transport thread, whose stack owns the current request/response. A tool can
  // close or switch projects; postpone Stop/Start until its handler has returned to the transport.
  s_bServerStatePending = true;
  ApplyServerState();
}
