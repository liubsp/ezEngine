#include <TestFramework/Framework/TestFramework.h>

#ifdef BUILDSYSTEM_ENABLE_MCP_TESTS

#  include <Core/GameApplication/GameApplicationBase.h>
#  include <Foundation/Communication/Telemetry.h>
#  include <Foundation/Utilities/CommandLineUtils.h>
#  include <Mcp/McpServer.h>
#  include <Mcp/McpSocket.h>
#  include <Mcp/McpToolRegistry.h>
#  include <McpPlugin/McpEngineHost.h>
#  include <TestFramework/Utilities/TestLogInterface.h>

namespace
{
  // Exercise the supported initialization methods without starting a window, renderer or app loop.
  class HeadlessStartupApplication : public ezGameApplicationBase
  {
  public:
    HeadlessStartupApplication()
      : ezGameApplicationBase("Debug services test")
    {
      m_OriginalArguments = ezCommandLineUtils::GetGlobalInstance()->GetCommandLineArray();
      m_uiOriginalTelemetryPort = ezTelemetry::s_uiPort;
    }

    ~HeadlessStartupApplication()
    {
      ezMcpEngineHost::Shutdown();
      ezMcpToolRegistry::Clear();
      ezTelemetry::CloseConnection();
      ezTelemetry::s_uiPort = m_uiOriginalTelemetryPort;
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(m_OriginalArguments);
      EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
      EZ_TEST_INT(ezTelemetry::GetConnectionMode(), ezTelemetry::None);
    }

    using ezGameApplicationBase::Init_ConfigureTelemetry;

  private:
    ezString FindProjectDirectory() const override { return {}; }
    void Init_SetupGraphicsDevice() override {}
    void Deinit_ShutdownGraphicsDevice() override {}
    void Run_WorldUpdateAndRender() override {}
    ezDynamicArray<ezString> m_OriginalArguments;
    ezUInt16 m_uiOriginalTelemetryPort;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(DebugServices);

EZ_CREATE_SIMPLE_TEST(DebugServices, TelemetryOptIn)
{
  HeadlessStartupApplication application;
  EZ_TEST_INT(ezTelemetry::GetConnectionMode(), ezTelemetry::None);
  EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
  const ezUInt16 uiOriginalPort = ezTelemetry::s_uiPort;
  for (const char* szOptIn : {"absent", "false", "true", "bare"})
  {
    ezTelemetry::CloseConnection();
    // ENet port 0 requests an ephemeral UDP port instead of taking another host's conventional port.
    const char* arguments[] = {"GameEngineTest", "-TelemetryPort", "0", "-telemetry", szOptIn};
    const bool bBare = ezStringUtils::IsEqual(szOptIn, "bare");
    ezUInt32 uiArgumentCount = 5;
    if (ezStringUtils::IsEqual(szOptIn, "absent"))
      uiArgumentCount = 3;
    else if (bBare)
      uiArgumentCount = 4;
    ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(uiArgumentCount, arguments);
    const bool bEnabled = bBare || ezStringUtils::IsEqual(szOptIn, "true");
    ezTestLogInterface log;
    ezTestLogSystemScope logScope(&log, false);
#  if !defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
    if (bEnabled)
      log.ExpectMessage("Enet is not compiled", ezLogMsgType::SeriousWarningMsg, 1);
#  endif
    application.Init_ConfigureTelemetry();
#  if defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
    EZ_TEST_INT(ezTelemetry::GetConnectionMode(), bEnabled ? ezTelemetry::Server : ezTelemetry::None);
#  else
    EZ_TEST_INT(ezTelemetry::GetConnectionMode(), ezTelemetry::None);
#  endif
    EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
    if (!bEnabled)
      EZ_TEST_INT(ezTelemetry::s_uiPort, uiOriginalPort);
  }
}

EZ_CREATE_SIMPLE_TEST(DebugServices, GameMcpOptIn)
{
  HeadlessStartupApplication application;
  const char* defaultArguments[] = {"GameEngineTest"};
  ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(1, defaultArguments);
  ezMcpEngineHost::Startup();
  EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
  EZ_TEST_INT(ezTelemetry::GetConnectionMode(), ezTelemetry::None);

  ezMcpSocket probe;
  EZ_TEST_BOOL(probe.Listen(0).Succeeded());
  const ezUInt16 uiPort = probe.GetPort();
  probe.Close();
  ezStringBuilder sPort;
  sPort.SetFormat("{}", uiPort);
  const char* arguments[] = {"GameEngineTest", "-mcpport", sPort.GetData()};
  ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(3, arguments);
  ezMcpEngineHost::Startup();
  const auto* pServer = ezMcpServer::GetInstance();
  EZ_TEST_BOOL(pServer != nullptr && pServer->IsRunning());
  if (pServer != nullptr)
    EZ_TEST_INT(pServer->GetPort(), uiPort);
  EZ_TEST_INT(ezTelemetry::GetConnectionMode(), ezTelemetry::None);
}

#endif
