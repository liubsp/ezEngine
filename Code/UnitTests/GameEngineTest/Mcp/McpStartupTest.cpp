#include <TestFramework/Framework/TestFramework.h>

#ifdef BUILDSYSTEM_ENABLE_MCP_TESTS

#  include <Core/GameApplication/GameApplicationBase.h>
#  include <Foundation/Utilities/CommandLineUtils.h>
#  include <Mcp/McpServer.h>
#  include <Mcp/McpSocket.h>
#  include <Mcp/McpToolRegistry.h>
#  include <McpPlugin/McpEngineHost.h>
#  include <TestFramework/Utilities/TestLogInterface.h>

namespace
{
  class ControlStartup : public ezGameApplicationBase
  {
  public:
    ControlStartup(ezArrayPtr<ezString> arguments)
      : ezGameApplicationBase("ControlStartup")
    {
      m_OriginalArguments = ezCommandLineUtils::GetGlobalInstance()->GetCommandLineArray();
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(arguments);
    }

    ~ControlStartup()
    {
      ezMcpEngineHost::Shutdown();
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(m_OriginalArguments);
    }

  private:
    ezString FindProjectDirectory() const override { return {}; }
    void Init_SetupGraphicsDevice() override {}
    void Deinit_ShutdownGraphicsDevice() override {}
    void Run_WorldUpdateAndRender() override {}
    ezDynamicArray<ezString> m_OriginalArguments;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(McpStartup);

EZ_CREATE_SIMPLE_TEST(McpStartup, ControlEndpoint)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Absent means no listener")
  {
    ezString arguments[] = {"ControlStartup"};
    ControlStartup app(ezMakeArrayPtr(arguments));
    ezMcpEngineHost::Startup();
    EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
    EZ_TEST_INT(app.GetBoundControlPort(), 0);
    EZ_TEST_BOOL(!app.ShouldApplicationQuit());
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Explicit zero reports the actual port and shutdown clears it")
  {
    ezString arguments[] = {"ControlStartup", "-mcpport", "0", "-mcpRequired"};
    ControlStartup app(ezMakeArrayPtr(arguments));
    ezMcpEngineHost::Startup();
    const ezMcpServer* pServer = ezMcpServer::GetInstance();
    EZ_TEST_BOOL(pServer != nullptr);
    if (pServer != nullptr)
    {
      EZ_TEST_BOOL(pServer->IsRunning());
      EZ_TEST_BOOL(pServer->GetPort() != 0);
      EZ_TEST_INT(app.GetBoundControlPort(), pServer->GetPort());
      EZ_TEST_BOOL(!ezMcpToolRegistry::GetTools().IsEmpty());
      ezMcpEngineHost::Startup();
      EZ_TEST_INT(app.GetBoundControlPort(), pServer->GetPort());
    }
    ezMcpEngineHost::Shutdown();
    EZ_TEST_INT(app.GetBoundControlPort(), 0);
    EZ_TEST_BOOL(ezMcpToolRegistry::GetTools().IsEmpty());
    ezMcpEngineHost::Startup();
    EZ_TEST_BOOL(app.GetBoundControlPort() != 0);
    EZ_TEST_BOOL(!ezMcpToolRegistry::GetTools().IsEmpty());
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Fixed own port and editor fallback bind the requested endpoint")
  {
    // Reserve an OS-selected port for each case, then release immediately before the real bind.
    // This is test setup, not production free-port probing; an external conflict fails the test.
    for (bool bEditorFallback : {false, true})
    {
      ezUInt16 uiPort = 0;
      {
        ezMcpSocket reservation;
        if (!EZ_TEST_BOOL(reservation.Listen(0).Succeeded()))
          return;
        uiPort = reservation.GetPort();
      }
      ezStringBuilder port;
      port.SetFormat("{}", bEditorFallback ? uiPort - 1 : uiPort);
      ezString arguments[] = {"ControlStartup", bEditorFallback ? "-editor-mcpport" : "-mcpport", port};
      ControlStartup app(ezMakeArrayPtr(arguments));
      ezMcpEngineHost::Startup();
      EZ_TEST_INT(app.GetBoundControlPort(), uiPort);
      EZ_TEST_BOOL(!app.ShouldApplicationQuit());
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Required occupied port fails; optional occupied port remains optional")
  {
    ezMcpSocket occupied;
    const ezResult bound = occupied.Listen(0);
    EZ_TEST_BOOL(bound.Succeeded());
    if (bound.Failed())
      return;
    ezStringBuilder port;
    port.SetFormat("{}", occupied.GetPort());
    for (bool bRequired : {false, true})
    {
      ezTestLogInterface log;
      ezTestLogSystemScope scope(&log, true);
      log.ExpectMessage("Could not bind to port", ezLogMsgType::WarningMsg);
      log.ExpectMessage(bRequired ? "Required MCP transport could not listen" : "The game runs without an MCP server", ezLogMsgType::ErrorMsg);
      ezString arguments[] = {"ControlStartup", "-mcpport", port, "-mcpRequired", bRequired ? "true" : "false"};
      ControlStartup app(ezMakeArrayPtr(arguments));
      ezMcpEngineHost::Startup();
      EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
      EZ_TEST_INT(app.GetBoundControlPort(), 0);
      EZ_TEST_BOOL(app.ShouldApplicationQuit() == bRequired);
      EZ_TEST_BOOL((app.GetReturnCode() != 0) == bRequired);
    }
  }
}

#endif
