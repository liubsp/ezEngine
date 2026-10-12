#include <CoreTest/CoreTestPCH.h>

#include <Core/GameApplication/GameApplicationBase.h>
#include <Foundation/Application/Config/FileSystemConfig.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/Utilities/CommandLineUtils.h>
#include <TestFramework/Utilities/TestLogInterface.h>

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT) && EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)
#  include <Foundation/Platform/Win/Utils/IncludeWindows.h>
#  include <Foundation/Strings/StringConversion.h>

namespace
{
  class RuntimeMounts : public ezGameApplicationBase
  {
  public:
    RuntimeMounts(ezStringView sProject)
      : ezGameApplicationBase("RuntimeMounts")
      , m_sProject(sProject)
      , m_sOldCwd(ezOSFile::GetCurrentWorkingDirectory())
    {
      m_OriginalArguments = ezCommandLineUtils::GetGlobalInstance()->GetCommandLineArray();
      ezDynamicArray<ezString> empty;
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(empty);
    }

    ~RuntimeMounts()
    {
      Deinit_ShutdownLogging();
      ezApplicationFileSystemConfig::Clear();
      ezFileSystem::RemoveDataDirectoryGroup("GameApplicationBase");
      ezFileSystem::SetSpecialDirectory("project", {});
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(m_OriginalArguments);
      SetCurrentDirectoryW(ezStringWChar(m_sOldCwd).GetData());
    }

    void Mount()
    {
      Init_FileSystem_SetSpecialDirs();
      Init_FileSystem_ConfigureDataDirs();
    }

    void LoadProjectPlugins() { Init_LoadProjectPlugins(); }

  private:
    ezString FindProjectDirectory() const override { return m_sProject; }
    ezString GetBaseDataDirectoryPath() const override { return m_sProject; }
    void Init_SetupGraphicsDevice() override {}
    void Deinit_ShutdownGraphicsDevice() override {}
    void Run_WorldUpdateAndRender() override {}
    ezString m_sProject;
    ezString m_sOldCwd;
    ezDynamicArray<ezString> m_OriginalArguments;
  };

  bool WriteConfig(ezStringView sPath, ezStringView sContents)
  {
    ezOSFile file;
    if (file.Open(sPath, ezFileOpenMode::Write).Failed())
      return false;
    return file.Write(sContents.GetStartPointer(), sContents.GetElementCount()).Succeeded();
  }
} // namespace

EZ_CREATE_SIMPLE_TEST(GameApplication, RuntimeMounts)
{
  const ezStringBuilder output(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/RuntimeMounts");
  const ezStringBuilder project(output, "/project");
  const ezStringBuilder config(project, "/RuntimeConfigs/DataDirectories.ddl");
  const ezStringBuilder root(output, "/session");
  EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(root).Succeeded());
  EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(project).Succeeded());

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Reserved stores are private and the authored writable project becomes read-only")
  {
    if (!EZ_TEST_BOOL(WriteConfig(config,
          "DataDir { string %Path {\">project/\"} string %RootName {\":project/\"} bool %Writable {true} }\n"
          "DataDir { string %Path {\">appdir/\"} string %RootName {\":BIN/\"} bool %Writable {true} }\n")))
      return;
    RuntimeMounts app(project);
    const ezStatus selected = app.ConfigureRuntimeSession(root);
    EZ_TEST_BOOL(selected.Succeeded());
    if (selected.Failed())
      return;
    app.Mount();
    EZ_TEST_BOOL(!app.ShouldApplicationQuit());
    if (app.ShouldApplicationQuit())
      return;
    for (const char* szRoot : {"appdata", "bin", "shadercache"})
    {
      ezStringBuilder path(":", szRoot, "/owned.txt");
      ezFileWriter writer;
      EZ_TEST_BOOL(writer.Open(path).Succeeded());
      writer.Close();
      EZ_TEST_BOOL(ezOSFile::ExistsFile(ezStringBuilder(root, "/", szRoot, "/owned.txt")));
    }
    const auto* pProject = ezFileSystem::FindDataDirectoryWithRoot("project");
    EZ_TEST_BOOL(pProject != nullptr);
    if (pProject != nullptr)
      EZ_TEST_BOOL(pProject->m_Usage == ezDataDirUsage::ReadOnly);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "An absolute writable resource mount inside the session is preserved")
  {
    const ezStringBuilder privateMount(root, "/custom");
    if (!EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(privateMount).Succeeded()))
      return;
    const ezStringBuilder contents("DataDir { string %Path {\"", privateMount, "\"} string %RootName {\"custom\"} bool %Writable {true} }");
    if (!EZ_TEST_BOOL(WriteConfig(config, contents)))
      return;
    RuntimeMounts app(project);
    if (!EZ_TEST_BOOL(app.ConfigureRuntimeSession(root).Succeeded()))
      return;
    app.Mount();
    if (!EZ_TEST_BOOL(!app.ShouldApplicationQuit()))
      return;
    ezFileWriter writer;
    EZ_TEST_BOOL(writer.Open(":custom/owned.txt").Succeeded());
    writer.Close();
    EZ_TEST_BOOL(ezOSFile::ExistsFile(ezStringBuilder(privateMount, "/owned.txt")));
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "A writable resource mount cannot escape the session")
  {
    if (!EZ_TEST_BOOL(WriteConfig(config,
          "DataDir { string %Path {\">project/\"} string %RootName {\"project\"} bool %Writable {true} }\n"
          "DataDir { string %Path {\">project/\"} string %RootName {\"escape\"} bool %Writable {true} }\n")))
      return;
    RuntimeMounts app(project);
    if (!EZ_TEST_BOOL(app.ConfigureRuntimeSession(root).Succeeded()))
      return;
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage("Runtime output must be below", ezLogMsgType::ErrorMsg);
    app.Mount();
    EZ_TEST_BOOL(app.ShouldApplicationQuit());
    EZ_TEST_BOOL(app.GetReturnCode() != 0);
    EZ_TEST_BOOL(ezFileSystem::FindDataDirectoryWithRoot("escape") == nullptr);
    EZ_TEST_BOOL(ezFileSystem::FindDataDirectoryWithRoot("project") == nullptr);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Missing project plugin reports failure without activating a game state")
  {
    if (!EZ_TEST_BOOL(WriteConfig(config, "")))
      return;
    const ezStringBuilder plugins(project, "/RuntimeConfigs/Plugins.ddl");
    if (!EZ_TEST_BOOL(WriteConfig(plugins, "Plugin { string %Path {\"ezRuntimeIsolationMissingTestPlugin\"} bool %LoadCopy {true} }")))
      return;
    RuntimeMounts app(project);
    if (!EZ_TEST_BOOL(app.ConfigureRuntimeSession(root).Succeeded()))
      return;
    app.Mount();
    if (!EZ_TEST_BOOL(!app.ShouldApplicationQuit()))
      return;
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage("does not exist", ezLogMsgType::ErrorMsg);
    log.ExpectMessage("Cannot load required runtime plugin", ezLogMsgType::ErrorMsg);
    app.LoadProjectPlugins();
    EZ_TEST_BOOL(app.ShouldApplicationQuit());
    EZ_TEST_BOOL(app.GetReturnCode() != 0);
    EZ_TEST_BOOL(app.GetActiveGameState() == nullptr);
  }
  EZ_TEST_BOOL(!ezStartup::HasApplicationTag("runtime-session"));
}
#endif
