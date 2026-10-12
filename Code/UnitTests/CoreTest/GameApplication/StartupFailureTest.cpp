#include <CoreTest/CoreTestPCH.h>

#include <Core/GameApplication/GameApplicationBase.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <TestFramework/Utilities/TestLogInterface.h>

namespace
{
  class StartupStages : public ezGameApplicationBase
  {
  public:
    StartupStages(ezUInt32 uiFailAt)
      : ezGameApplicationBase("StartupStages")
      , m_uiFailAt(uiFailAt)
    {
    }

    void Initialize() { ExecuteInitFunctions(); }
    ezUInt32 GetStageCount() const { return m_uiStageCount; }

  private:
    void VisitStage()
    {
      if (++m_uiStageCount == m_uiFailAt)
        ReportStartupFailure("Injected startup stage failure.");
    }
    ezString FindProjectDirectory() const override { return {}; }
    void Init_PlatformProfile_SetPreferred() override { VisitStage(); }
    void Init_ConfigureTelemetry() override { VisitStage(); }
    void Init_FileSystem_SetSpecialDirs() override { VisitStage(); }
    void Init_LoadRequiredPlugins() override { VisitStage(); }
    void Init_ConfigureAssetManagement() override { VisitStage(); }
    void Init_FileSystem_ConfigureDataDirs() override { VisitStage(); }
    void Init_LoadWorldModuleConfig() override { VisitStage(); }
    void Init_LoadProjectPlugins() override { VisitStage(); }
    void Init_PlatformProfile_LoadForRuntime() override { VisitStage(); }
    void Init_ConfigureTags() override { VisitStage(); }
    void Init_ConfigureCVars() override { VisitStage(); }
    void Init_SetupGraphicsDevice() override { VisitStage(); }
    void Init_SetupDefaultResources() override { VisitStage(); }
    void Deinit_ShutdownGraphicsDevice() override {}
    void Run_WorldUpdateAndRender() override {}

    ezUInt32 m_uiFailAt;
    ezUInt32 m_uiStageCount = 0;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST(GameApplication, StartupFailure)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Required plugin, mount and project plugin failures stop subsequent stages")
  {
    for (ezUInt32 uiFailAt : {4u, 6u, 8u, 12u})
    {
      ezTestLogInterface log;
      ezTestLogSystemScope scope(&log, true);
      log.ExpectMessage("Injected startup stage failure", ezLogMsgType::ErrorMsg);
      StartupStages app(uiFailAt);
      app.Initialize();
      EZ_TEST_INT(app.GetStageCount(), uiFailAt);
      EZ_TEST_BOOL(app.ShouldApplicationQuit());
      EZ_TEST_BOOL(app.GetReturnCode() != 0);
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Normal initialization and write-once control result")
  {
    StartupStages app(ezInvalidIndex);
    app.Initialize();
    EZ_TEST_INT(app.GetStageCount(), 13);
    EZ_TEST_BOOL(!app.ShouldApplicationQuit());
    EZ_TEST_BOOL(!app.GetRuntimeSession().IsSelected());
    EZ_TEST_INT(app.GetBoundControlPort(), 0);
    EZ_TEST_BOOL(app.ReportBoundControlPort(0).Failed());
    EZ_TEST_BOOL(app.ReportBoundControlPort(12345).Succeeded());
    EZ_TEST_INT(app.GetBoundControlPort(), 12345);
    EZ_TEST_BOOL(app.ReportBoundControlPort(12345).Failed());
    app.ClearBoundControlPort();
    EZ_TEST_INT(app.GetBoundControlPort(), 0);
    EZ_TEST_BOOL(app.ReportBoundControlPort(12346).Succeeded());
  }
}
