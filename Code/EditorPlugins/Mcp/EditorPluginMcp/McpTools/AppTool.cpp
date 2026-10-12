#include <EditorPluginMcp/EditorPluginMcpPCH.h>

#include <EditorPluginMcp/McpTools/AppTool.h>

#include <Mcp/McpJsonWriter.h>
#include <Mcp/McpServer.h>

#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/Utilities/CommandLineUtils.h>
#include <Foundation/Utilities/ConversionUtils.h>

#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Document/DocumentManager.h>

#include <QApplication>
#include <QTimer>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezMcpEditorAppTool, 1, ezRTTIDefaultAllocator<ezMcpEditorAppTool>)
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

void ezMcpEditorAppTool::GetSupportedTools(ezDynamicArray<ezMcpToolDesc>& out_tools) const
{
  ezMcpAppTool::GetSupportedTools(out_tools);
  auto& desc = out_tools.ExpandAndGetRef();
  desc.m_sName = "app_quit_if_instance";
  desc.m_sDescription = "Requests document-safe quit only if expectedInstance matches this provider's app_info.editorQuitInstance. "
                        "Never discards changes. A positive reply requests shutdown; it does not prove exit. "
                        "On refusal or an unknown/lost reply, use human close/recovery, not unguarded quit or process termination.";
  desc.m_sInputSchema = R"({"type":"object","properties":{"expectedInstance":{"type":"string","description":"UUID from this editor's validated app_info response."}},"required":["expectedInstance"],"additionalProperties":false})";
}

void ezMcpEditorAppTool::Execute(ezStringView sToolName, const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  if (sToolName != "app_quit_if_instance")
  {
    ezMcpAppTool::Execute(sToolName, arguments, out_result);
    return;
  }

  const auto instance = arguments.Find("expectedInstance");
  if (arguments.GetCount() != 1 || !instance.IsValid() || !instance.Value().IsA<ezString>())
  {
    out_result.SetError("app_quit_if_instance requires only a UUID-string expectedInstance argument.");
    return;
  }

  // Compare with the canonical UUID we publish. This also rejects malformed/empty input without
  // passing untrusted hexadecimal digits to the engine's permissive UUID conversion routine.
  ezStringBuilder sInstance;
  ezConversionUtils::ToString(m_QuitInstance, sInstance);
  if (!instance.Value().Get<ezString>().IsEqual_NoCase(sInstance))
  {
    out_result.SetError("expectedInstance is invalid or does not match this editor; shutdown was not requested.");
    return;
  }

  // Reuse the ordinary refusal/reporting/deferred-shutdown path, always without discard.
  ezMcpAppTool::Execute("app_quit", {}, out_result);
}

ezStringView ezMcpEditorAppTool::GetBuildTimestamp() const
{
  return __DATE__ " " __TIME__;
}

ezStringView ezMcpEditorAppTool::GetRelaunchHint() const
{
  return "To start an editor again afterwards, run the ezEditor executable with "
         "'-project <path-to-project-folder> -minimized -NoSplash -unattended -editor-mcpport <port>'. Pass '-unattended', or the editor may "
         "open a dialog while opening the project - before any tool call can suppress it - and hang there. "
         "It serves MCP at http://127.0.0.1:<port>/mcp "
         "once the project is open, which takes a few seconds - poll the port rather than assuming a delay. "
         "Without '-editor-mcpport' no editor MCP listener starts. Supply a distinct port for each editor. "
         "Call app_info for this editor's own port and executable path.";
}

void ezMcpEditorAppTool::AddHostInfo(ezMcpJsonWriter& ref_writer)
{
  ezStringBuilder sInstance;
  ezConversionUtils::ToString(m_QuitInstance, sInstance);
  ref_writer.AddVariableString("editorQuitInstance", sInstance);

  // Where the editor writes its log. Reported as an absolute path because the location is otherwise
  // undiscoverable: it lives in the user data directory under a name derived from '-appid', and
  // nothing in the tool list or in '-help' points at it. It matters most when the editor dies - a
  // crash takes the MCP server with it, so the file is all that is left to read afterwards.
  const ezInt32 iApplicationID = ezCommandLineUtils::GetGlobalInstance()->GetIntOption("-appid", 0);

  ezStringBuilder sLogFile;
  sLogFile.SetFormat(":appdata/Logs/LogEditor_{}.htm", iApplicationID);

  ezStringBuilder sAbsLogFile;
  if (ezFileSystem::ResolvePath(sLogFile, &sAbsLogFile, nullptr).Succeeded())
  {
    ref_writer.AddVariableString("logFile", sAbsLogFile);

    // The engine runs as a separate process and logs separately, which is why log_read cannot see
    // it. Naming the pattern is the only way an agent can find those files at all.
    ezStringBuilder sEngineLogPattern = sAbsLogFile;
    sEngineLogPattern.PathParentDirectory();
    sEngineLogPattern.AppendFormat("LogEditor_{}_Engine_<pid>.htm", iApplicationID);
    sEngineLogPattern.MakeCleanPath();
    ref_writer.AddVariableString("engineLogFilePattern", sEngineLogPattern);
  }

  // The engine process serves its own MCP endpoint, and that is where anything about the running game
  // is answered - screenshots, input, frames. An agent should not be expected to know the '+1' rule, so
  // the port is reported rather than implied.
  {
    const ezEditorEngineProcessConnection* pCon = ezEditorEngineProcessConnection::GetSingleton();
    const bool bEngineRunning = pCon != nullptr && pCon->IsEngineSetup();

    // The engine process' port cannot be queried from here - that would need an IPC message this
    // feature does not have - so it is resolved exactly the way the engine plugin resolves it. That
    // works because the editor forwards its whole command line, so both processes see the same options:
    // an explicit '-mcpport' wins, otherwise the engine takes our port + 1.
    const ezInt32 iExplicitPort = ezCommandLineUtils::GetGlobalInstance()->GetIntOption("-mcpport", 0);

    const ezMcpServer* pServer = ezMcpServer::GetInstance();
    const ezUInt16 uiOwnPort = pServer != nullptr ? pServer->GetPort() : 0;

    ezUInt16 uiEnginePort = 0;

    if (iExplicitPort > 0 && iExplicitPort <= 0xFFFF)
    {
      uiEnginePort = static_cast<ezUInt16>(iExplicitPort);
    }
    else if (uiOwnPort > 0 && uiOwnPort < 0xFFFF)
    {
      uiEnginePort = uiOwnPort + 1;
    }

    if (uiEnginePort > 0 && bEngineRunning)
    {
      ref_writer.AddVariableUInt32("engineMcpPort", uiEnginePort);

      ezStringBuilder sEngineUrl;
      sEngineUrl.SetFormat("http://127.0.0.1:{}/mcp", uiEnginePort);
      ref_writer.AddVariableString("engineMcpUrl", sEngineUrl);
    }
    else
    {
      // Reported as unknown with the reason, rather than omitted: a missing field is indistinguishable
      // from a build that never had one.
      ref_writer.AddVariableString("engineMcpPort", "unknown");
      ref_writer.AddVariableString("engineMcpPortReason",
        bEngineRunning
          ? "This editor has no MCP port of its own, so the engine process had nothing to derive one from. "
            "Restart the editor with '-editor-mcpport <port>'."
          : "The engine process is not running yet. It starts with the first opened scene document.");
    }

    ref_writer.AddVariableString("engineMcpHint",
      "The engine process renders and runs the game, and answers for itself on its own port: screenshots, input, "
      "frame stepping and its own log_read are all there, not here. It only serves MCP if the build includes the "
      "engine side plugin - call app_info against that port to find out.");
  }

  ref_writer.AddVariableString("launchHint",
    "Run 'executable' with '-project <project-folder> -minimized -NoSplash -unattended -editor-mcpport <port>' to start another editor. "
    "Minimized affects main-window startup only; dialogs and later interaction retain ordinary behavior. Poll the port until it "
    "accepts connections - startup takes a few seconds. Use a port other than 'mcpPort' to run one alongside this editor. "
    "'logFile' is written as HTML and is the only record left if the editor crashes, since that also kills this server; "
    "note that a crash may lose the last messages, as the log is not flushed on the way down.");
}

void ezMcpEditorAppTool::CollectModifiedDocuments(ezDynamicArray<ezString>& out_documents)
{
  for (const ezDocumentManager* pMan : ezDocumentManager::GetAllDocumentManagers())
  {
    for (const ezDocument* pDoc : pMan->GetAllOpenDocuments())
    {
      if (pDoc->IsModified())
      {
        out_documents.PushBack(pDoc->GetDocumentPath());
      }
    }
  }
}

ezResult ezMcpEditorAppTool::CanQuit(bool bDiscardChanges)
{
  // Checked here rather than through ezToolsProject::CanCloseProject(), which asks the *user* about
  // unsaved documents through a modal dialog. With no one at the keyboard - which is the entire point
  // of this tool - that dialog never returns and the editor hangs, holding its port, unreachable and
  // unkillable through MCP. So the decision is made without any dialog at all.
  m_ModifiedDocuments.Clear();
  CollectModifiedDocuments(m_ModifiedDocuments);

  if (!m_ModifiedDocuments.IsEmpty() && !bDiscardChanges)
    return EZ_FAILURE;

  return EZ_SUCCESS;
}

void ezMcpEditorAppTool::AddQuitRefusalInfo(ezMcpJsonWriter& ref_writer)
{
  ref_writer.AddVariableString("reason", "Documents have unsaved changes. Save them, or call again with discardChanges=true to lose them.");

  ref_writer.BeginArray("modifiedDocuments");
  for (const ezString& sDoc : m_ModifiedDocuments)
  {
    ref_writer.WriteString(sDoc);
  }
  ref_writer.EndArray();
}

void ezMcpEditorAppTool::AddQuitInfo(ezMcpJsonWriter& ref_writer)
{
  ref_writer.AddVariableUInt32("discardedDocuments", m_ModifiedDocuments.GetCount());
}

void ezMcpEditorAppTool::RequestQuit(bool bDiscardChanges)
{
  // Deferred to the next event loop iteration, because this runs inside the request handler: the
  // response has not been written to the socket yet, and quitting here would drop it, leaving the
  // caller waiting on a connection that closes with no answer.
  QTimer::singleShot(0, qApp, [bDiscardChanges]()
    {
      auto* pEditor = ezQtEditorApp::GetSingleton();
      if (pEditor == nullptr || !pEditor->IsRunning())
      {
        return;
      }

      // The reply is deferred; another GUI event may have modified a document in the meantime.
      ezDynamicArray<ezString> modifiedDocuments;
      CollectModifiedDocuments(modifiedDocuments);
      if (!bDiscardChanges && !modifiedDocuments.IsEmpty())
      {
        ezLog::Warning("Editor quit refused: documents were modified after the quit request.");
        return;
      }

      // Closes the documents first, discarding unsaved changes without asking. Otherwise the shutdown
      // path finds modified documents and raises the modal save dialog that this tool exists to avoid.
      // Reaching this point means that was either unnecessary or explicitly requested.
      ezQtScopedUnattended unattended;
      if (!ezToolsProject::CanCloseProject() && !bDiscardChanges)
      {
        ezLog::Warning("Editor quit refused: project closure was declined.");
        return;
      }
      ezDocumentManager::CloseAllDocuments();

      // Check the ordinary container close path rather than assuming closeAllWindows succeeded.
      // ShutdownEditor remains the final cleanup owner after the event loop returns.
      auto* pContainer = ezQtContainerWindow::GetContainerWindow();
      if (pContainer != nullptr && pContainer->close())
        QCoreApplication::quit(); });
}
