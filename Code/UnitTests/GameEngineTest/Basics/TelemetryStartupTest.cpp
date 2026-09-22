#include <GameEngineTest/GameEngineTestPCH.h>

#include <Foundation/Communication/Telemetry.h>
#include <GameEngineTest/TestClass/TestClass.h>
#include <TestFramework/Utilities/TestLogInterface.h>

class ezTelemetryStartupTestApplication : public ezGameEngineTestApplication
{
public:
  ezTelemetryStartupTestApplication()
    : ezGameEngineTestApplication("Basics")
  {
  }
  using ezGameApplicationBase::Init_ConfigureTelemetry;
};

class ezTelemetryStartupTest : public ezGameEngineTest
{
public:
  const char* GetTestName() const override { return "Telemetry Startup"; }
  ezGameEngineTestApplication* CreateApplication() override { return EZ_DEFAULT_NEW(ezTelemetryStartupTestApplication); }
  void SetupSubTests() override { AddSubTest("Explicit opt-in before binding", 0); }

  ezTestAppRun RunSubTest(ezInt32, ezUInt32) override
  {
    // Actual application initialization, not just command-line parsing, must have remained offline.
    EZ_TEST_BOOL(ezTelemetry::GetConnectionMode() == ezTelemetry::None);
    auto* pCommandLine = ezCommandLineUtils::GetGlobalInstance();
    auto savedArguments = pCommandLine->GetCommandLineArray();
    const ezUInt16 uiSavedPort = ezTelemetry::s_uiPort;
    EZ_SCOPE_EXIT(pCommandLine->SetCommandLine(savedArguments); ezTelemetry::CloseConnection(); ezTelemetry::s_uiPort = uiSavedPort);
    auto* pApplication = static_cast<ezTelemetryStartupTestApplication*>(m_pApplication);
    for (const char* szOptIn : {"absent", "false", "true"})
    {
      ezTelemetry::CloseConnection();
      const char* arguments[] = {"GameEngineTest", "-TelemetryPort", "17643", "-telemetry", szOptIn};
      pCommandLine->SetCommandLine(ezStringUtils::IsEqual(szOptIn, "absent") ? 3 : 5, arguments);
      ezTestLogInterface log;
      ezTestLogSystemScope logScope(&log, false);
      const bool bEnabled = ezStringUtils::IsEqual(szOptIn, "true");
#if !defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
      if (bEnabled)
        log.ExpectMessage("Enet is not compiled", ezLogMsgType::SeriousWarningMsg, 1);
#endif
      pApplication->Init_ConfigureTelemetry();
#if defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
      EZ_TEST_BOOL(ezTelemetry::GetConnectionMode() == (bEnabled ? ezTelemetry::Server : ezTelemetry::None));
      if (bEnabled)
        EZ_TEST_INT(ezTelemetry::s_uiPort, 17643);
#else
      EZ_TEST_BOOL(ezTelemetry::GetConnectionMode() == ezTelemetry::None);
#endif
    }
    ezTelemetry::CloseConnection();
    const char* explicitArguments[] = {"GameEngineTest", "-telemetry"};
    pCommandLine->SetCommandLine(2, explicitArguments);
    ezTestLogInterface log;
    ezTestLogSystemScope logScope(&log, false);
#if !defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
    log.ExpectMessage("Enet is not compiled", ezLogMsgType::SeriousWarningMsg, 1);
#endif
    pApplication->Init_ConfigureTelemetry();
#if defined(BUILDSYSTEM_ENABLE_ENET_SUPPORT) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
    EZ_TEST_BOOL(ezTelemetry::GetConnectionMode() == ezTelemetry::Server);
    EZ_TEST_INT(ezTelemetry::s_uiPort, 1040);
#else
    EZ_TEST_BOOL(ezTelemetry::GetConnectionMode() == ezTelemetry::None);
#endif
    return ezTestAppRun::Quit;
  }
};
static ezTelemetryStartupTest s_TelemetryStartupTest;
