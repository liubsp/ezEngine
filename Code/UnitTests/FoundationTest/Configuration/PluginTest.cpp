#include <FoundationTest/FoundationTestPCH.h>

#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/OSFile.h>
#include <TestFramework/Utilities/TestLogInterface.h>

ezCVarInt CVar_TestMissingDependencyInitCount("TestMissingDependencyInitCount", 0, ezCVarFlags::None, "Callbacks run despite a missing dependency.");

ezCVarInt CVar_TestPlugin1InitializedCount("TestPlugin1InitCount", 0, ezCVarFlags::None, "How often Plugin1 has been initialized.");
ezCVarInt CVar_TestPlugin1CoreShutdownCount("TestPlugin1CoreShutdownCount", 0, ezCVarFlags::None, "How often Plugin1's core subsystem has shut down before unload.");
ezCVarInt CVar_TestPlugin1UninitializedCount("TestPlugin1UninitCount", 0, ezCVarFlags::None, "How often Plugin1 has been uninitialized.");
ezCVarInt CVar_TestPlugin1Reloaded("TestPlugin1Reloaded", 0, ezCVarFlags::None, "How often Plugin1 has been reloaded (counts init AND de-init).");

ezCVarInt CVar_TestPlugin2InitializedCount("TestPlugin2InitCount", 0, ezCVarFlags::None, "How often Plugin2 has been initialized.");
ezCVarInt CVar_TestPlugin2UninitializedCount("TestPlugin2UninitCount", 0, ezCVarFlags::None, "How often Plugin2 has been uninitialized.");
ezCVarInt CVar_TestPlugin2Reloaded("TestPlugin2Reloaded", 0, ezCVarFlags::None, "How often Plugin2 has been reloaded (counts init AND de-init).");
ezCVarBool CVar_TestPlugin2FoundDependencies("TestPlugin2FoundDependencies", false, ezCVarFlags::None, "Whether Plugin2 found all its dependencies (other plugins).");

EZ_CREATE_SIMPLE_TEST(Configuration, Plugin)
{
  CVar_TestPlugin1InitializedCount = 0;
  CVar_TestPlugin1CoreShutdownCount = 0;
  CVar_TestPlugin1UninitializedCount = 0;
  CVar_TestPlugin1Reloaded = 0;
  CVar_TestPlugin2InitializedCount = 0;
  CVar_TestPlugin2UninitializedCount = 0;
  CVar_TestPlugin2Reloaded = 0;
  CVar_TestPlugin2FoundDependencies = false;

#if EZ_ENABLED(EZ_SUPPORTS_DYNAMIC_PLUGINS) && EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "LoadPlugin")
  {
    EZ_TEST_BOOL(ezPlugin::LoadPlugin(ezFoundationTest_Plugin2) == EZ_SUCCESS);
    EZ_TEST_BOOL(ezPlugin::LoadPlugin(ezFoundationTest_Plugin2, ezPluginLoadFlags::PluginIsOptional) == EZ_SUCCESS); // loading already loaded plugin is always a success

    EZ_TEST_INT(CVar_TestPlugin1InitializedCount, 1);
    EZ_TEST_INT(CVar_TestPlugin2InitializedCount, 1);

    EZ_TEST_INT(CVar_TestPlugin1UninitializedCount, 0);
    EZ_TEST_INT(CVar_TestPlugin2UninitializedCount, 0);

    EZ_TEST_INT(CVar_TestPlugin1Reloaded, 0);
    EZ_TEST_INT(CVar_TestPlugin2Reloaded, 0);

    EZ_TEST_BOOL(CVar_TestPlugin2FoundDependencies);

    // this will fail the FoundationTests, as it logs an error
    // EZ_TEST_BOOL(ezPlugin::LoadPlugin("Test") == EZ_FAILURE); // plugin does not exist
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "UnloadPlugin")
  {
    CVar_TestPlugin2FoundDependencies = false;
    ezPlugin::UnloadAllPlugins();

    EZ_TEST_INT(CVar_TestPlugin1InitializedCount, 1);
    EZ_TEST_INT(CVar_TestPlugin2InitializedCount, 1);

    EZ_TEST_INT(CVar_TestPlugin1UninitializedCount, 1);
    EZ_TEST_INT(CVar_TestPlugin1CoreShutdownCount, 1);
    EZ_TEST_INT(CVar_TestPlugin2UninitializedCount, 1);

    EZ_TEST_INT(CVar_TestPlugin1Reloaded, 0);
    EZ_TEST_INT(CVar_TestPlugin2Reloaded, 0);

    EZ_TEST_BOOL(CVar_TestPlugin2FoundDependencies);
    EZ_TEST_BOOL(ezPlugin::LoadPlugin("Test", ezPluginLoadFlags::PluginIsOptional) == EZ_FAILURE); // plugin does not exist
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Original load preserves another owner's copy")
  {
    if (ezPlugin::PlatformNeedsPluginCopy())
    {
      ezStringBuilder original, copy;
      ezPlugin::GetPluginPaths(ezFoundationTest_Plugin1, original, copy, 0);
      // Never overwrite or clean up an existing sidecar from another running test process.
      const bool bExists = ezOSFile::ExistsFile(copy);
      EZ_TEST_BOOL_MSG(!bExists, "This test requires an unused test-plugin sidecar path.");
      if (bExists)
        return;
      {
        ezOSFile sentinel;
        const ezResult opened = sentinel.Open(copy, ezFileOpenMode::Write, ezFileShareMode::Exclusive);
        EZ_TEST_BOOL(opened.Succeeded());
        if (opened.Failed())
          return;
        const char marker[] = "another owner's plugin copy";
        EZ_TEST_BOOL(sentinel.Write(marker, sizeof(marker)).Succeeded());
      }
      EZ_SCOPE_EXIT(ezOSFile::DeleteFile(copy).IgnoreResult());
      EZ_TEST_BOOL(ezPlugin::LoadPlugin(ezFoundationTest_Plugin1).Succeeded());
      ezPlugin::UnloadAllPlugins();
      EZ_TEST_BOOL(ezOSFile::ExistsFile(copy));
      ezFileStats stats;
      EZ_TEST_BOOL(ezOSFile::GetFileStats(copy, stats).Succeeded());
      EZ_TEST_INT(stats.m_uiFileSize, sizeof("another owner's plugin copy"));
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Copy load removes its owned sidecar")
  {
    if (ezPlugin::PlatformNeedsPluginCopy())
    {
      ezStringBuilder original, copy;
      ezPlugin::GetPluginPaths(ezFoundationTest_Plugin1, original, copy, 0);
      const bool bExists = ezOSFile::ExistsFile(copy);
      EZ_TEST_BOOL(!bExists);
      if (bExists)
        return;
      EZ_TEST_BOOL(ezPlugin::LoadPlugin(ezFoundationTest_Plugin1, ezPluginLoadFlags::LoadCopy).Succeeded());
      EZ_TEST_BOOL(ezOSFile::ExistsFile(copy));
      ezPlugin::UnloadAllPlugins();
      EZ_TEST_BOOL(!ezOSFile::ExistsFile(copy));
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Missing declared dependency rejects the module before its load callback")
  {
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage("ezDeliberatelyMissingRuntimeDependency", ezLogMsgType::ErrorMsg);
    CVar_TestMissingDependencyInitCount = 0;
    bool bNotifiedBeforeUnload = false;
    bool bNotifiedAfterUnload = false;
    auto subscription = ezPlugin::Events().AddEventHandler([&](const ezPluginEvent& event)
      {
        if (event.m_sPluginBinary == "ezFoundationTest_PluginMissingDependency")
        {
          bNotifiedBeforeUnload |= event.m_EventType == ezPluginEvent::BeforeUnloading;
          bNotifiedAfterUnload |= event.m_EventType == ezPluginEvent::AfterUnloading;
        } });
    EZ_SCOPE_EXIT(ezPlugin::Events().RemoveEventHandler(subscription));
    EZ_TEST_BOOL(ezPlugin::LoadPlugin("ezFoundationTest_PluginMissingDependency").Failed());
    EZ_TEST_INT(CVar_TestMissingDependencyInitCount, 0);
    EZ_TEST_BOOL(bNotifiedBeforeUnload);
    EZ_TEST_BOOL(bNotifiedAfterUnload);
    ezDynamicArray<ezPlugin::PluginInfo> plugins;
    ezPlugin::GetAllPluginInfos(plugins);
    for (const auto& plugin : plugins)
      EZ_TEST_BOOL(plugin.m_sName != "ezFoundationTest_PluginMissingDependency");
    ezPlugin::UnloadAllPlugins();
  }

#endif
}
