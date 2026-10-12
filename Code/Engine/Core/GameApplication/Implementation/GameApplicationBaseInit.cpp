#include <Core/CorePCH.h>

#include <Core/GameApplication/GameApplicationBase.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Core/World/WorldModuleConfig.h>
#include <Foundation/Application/Config/FileSystemConfig.h>
#include <Foundation/Application/Config/PluginConfig.h>
#include <Foundation/Communication/Telemetry.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/Archive/DataDirTypeArchive.h>
#include <Foundation/IO/FileSystem/DataDirTypeFolder.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OpenDdlReader.h>
#include <Foundation/Logging/ConsoleWriter.h>
#include <Foundation/Logging/TraceWriter.h>
#include <Foundation/Logging/VisualStudioWriter.h>
#include <Foundation/Platform/PlatformDesc.h>
#include <Foundation/Types/TagRegistry.h>
#include <Foundation/Utilities/CommandLineOptions.h>

ezCommandLineOptionBool opt_DisableConsoleOutput("app", "-disableConsoleOutput", "Disables logging to the standard console window.", false);
ezCommandLineOptionBool opt_Telemetry("app", "-telemetry", "Enables the development telemetry listener for ezInspector. Off by default; may require firewall permission.", false);
ezCommandLineOptionInt opt_TelemetryPort("app", "-TelemetryPort", "The network port over which telemetry is sent.", ezTelemetry::s_uiPort);
ezCommandLineOptionString opt_Profile("app", "-profile", "The platform profile to use.", "Default");

ezString ezGameApplicationBase::GetBaseDataDirectoryPath() const
{
  return ">sdk/Data/Base";
}

ezString ezGameApplicationBase::GetProjectDataDirectoryPath() const
{
  return ">project/";
}

void ezGameApplicationBase::ExecuteInitFunctions()
{
  // Preserve the virtual void hooks, but honor failure/quit between every stage, including plugins
  // that request quit from startup callbacks. No later device or game-state initialization follows.
  using InitFunction = void (ezGameApplicationBase::*)();
  const InitFunction functions[] = {
    &ezGameApplicationBase::Init_PlatformProfile_SetPreferred,
    &ezGameApplicationBase::Init_ConfigureTelemetry,
    &ezGameApplicationBase::Init_FileSystem_SetSpecialDirs,
    &ezGameApplicationBase::Init_LoadRequiredPlugins,
    &ezGameApplicationBase::Init_ConfigureAssetManagement,
    &ezGameApplicationBase::Init_FileSystem_ConfigureDataDirs,
    &ezGameApplicationBase::Init_LoadWorldModuleConfig,
    &ezGameApplicationBase::Init_LoadProjectPlugins,
    &ezGameApplicationBase::Init_PlatformProfile_LoadForRuntime,
    &ezGameApplicationBase::Init_ConfigureTags,
    &ezGameApplicationBase::Init_ConfigureCVars,
    &ezGameApplicationBase::Init_SetupGraphicsDevice,
    &ezGameApplicationBase::Init_SetupDefaultResources,
  };
  for (InitFunction function : functions)
  {
    if (ShouldApplicationQuit())
      return;
    (this->*function)();
  }
}

void ezGameApplicationBase::Init_PlatformProfile_SetPreferred()
{
  if (opt_Profile.IsOptionSpecified())
  {
    m_PlatformProfile.SetConfigName(opt_Profile.GetOptionValue(ezCommandLineOption::LogMode::AlwaysIfSpecified));
  }
  else
  {
    m_PlatformProfile.SetConfigName(ezPlatformDesc::GetThisPlatformDesc().GetName());

    const ezStringBuilder sRuntimeProfileFile(":project/RuntimeConfigs/", m_PlatformProfile.GetConfigName(), ".ezProfile");

    if (!ezFileSystem::ExistsFile(sRuntimeProfileFile))
    {
      ezLog::Info("Platform profile '{}' doesn't exist, switching to 'Default'", m_PlatformProfile.GetConfigName());

      m_PlatformProfile.SetConfigName("Default");
    }
  }

  m_PlatformProfile.AddMissingConfigs();
}

void ezGameApplicationBase::BaseInit_ConfigureLogging()
{
#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  ezGlobalLog::RemoveLogWriter(m_LogToConsoleID);
  ezGlobalLog::RemoveLogWriter(m_LogToVsID);
  ezGlobalLog::RemoveLogWriter(m_LogToTracingID);

  if (!opt_DisableConsoleOutput.GetOptionValue(ezCommandLineOption::LogMode::AlwaysIfSpecified))
  {
    m_LogToConsoleID = ezGlobalLog::AddLogWriter(ezLogWriter::Console::LogMessageHandler);
  }

  m_LogToVsID = ezGlobalLog::AddLogWriter(ezLogWriter::VisualStudio::LogMessageHandler);
  m_LogToTracingID = ezGlobalLog::AddLogWriter(ezLogWriter::Tracing::LogMessageHandler);
#endif
}

void ezGameApplicationBase::Init_ConfigureTelemetry()
{
#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  // Decide before any bind, not from plugins or CVars loaded later. A port setting is not consent:
  // the editor also supplies -TelemetryPort for ordinary local engine processes.
  if (!opt_Telemetry.GetOptionValue(ezCommandLineOption::LogMode::AlwaysIfSpecified))
    return;
  ezTelemetry::s_uiPort = static_cast<ezUInt16>(opt_TelemetryPort.GetOptionValue(ezCommandLineOption::LogMode::AlwaysIfSpecified));
  ezTelemetry::SetServerName(GetApplicationName());
  ezTelemetry::CreateServer();
#endif
}

void ezGameApplicationBase::Init_FileSystem_SetSpecialDirs()
{
  ezFileSystem::SetSpecialDirectory("project", FindProjectDirectory());
}

void ezGameApplicationBase::Init_ConfigureAssetManagement() {}

void ezGameApplicationBase::Init_LoadRequiredPlugins()
{
  ezPlugin::InitializeStaticallyLinkedPlugins();

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS)
  ezPlugin::LoadPlugin("XBoxControllerPlugin", ezPluginLoadFlags::PluginIsOptional).IgnoreResult();
#endif
}

void ezGameApplicationBase::Init_FileSystem_ConfigureDataDirs()
{
  if (GetRuntimeSession().IsSelected())
  {
    Init_FileSystem_ConfigureSessionDataDirs();
    if (ShouldApplicationQuit())
      return;
  }
  else
  {
    // ">appdir/" and ">user/" are built-in special directories
    // see ezFileSystem::ResolveSpecialDirectory

    const ezStringBuilder sUserDataPath(">user/", GetApplicationName());

    ezFileSystem::CreateDirectoryStructure(sUserDataPath).AssertSuccess();

    ezString writableBinRoot = ">appdir/";
    ezString shaderCacheRoot = ">sdk/Output/";

#if EZ_DISABLED(EZ_SUPPORTS_UNRESTRICTED_FILE_ACCESS)
    // On platforms where this is disabled, one can usually only write to the user directory
    // e.g. on mobile platforms
    writableBinRoot = sUserDataPath;
#endif

    ezFileSystem::CreateDirectoryStructure(shaderCacheRoot).IgnoreResult();

    // for absolute paths, read-only
    ezFileSystem::AddDataDirectory("", "GameApplicationBase", ":", ezDataDirUsage::ReadOnly).AssertSuccess();

    // ":bin/" : writing to the binary directory
    ezFileSystem::AddDataDirectory(writableBinRoot, "GameApplicationBase", "bin", ezDataDirUsage::AllowWrites).AssertSuccess();

    // ":shadercache/" for reading and writing shader files
#if EZ_DISABLED(EZ_SUPPORTS_UNRESTRICTED_FILE_ACCESS)
    ezFileSystem::AddDataDirectory(shaderCacheRoot, "GameApplicationBase", "shadercache", ezDataDirUsage::ReadOnly).AssertSuccess();
#else
    ezFileSystem::AddDataDirectory(shaderCacheRoot, "GameApplicationBase", "shadercache", ezDataDirUsage::AllowWrites).AssertSuccess();
#endif

    // ":appdata/" for reading and writing app user data
    ezFileSystem::AddDataDirectory(sUserDataPath, "GameApplicationBase", "appdata", ezDataDirUsage::AllowWrites).AssertSuccess();
  }

  // ":base/" for reading the core engine files
  const ezResult baseMounted = ezFileSystem::AddDataDirectory(GetBaseDataDirectoryPath(), "GameApplicationBase", "base", ezDataDirUsage::ReadOnly);
  if (GetRuntimeSession().IsSelected() && baseMounted.Failed())
  {
    ReportStartupFailure("Cannot mount the runtime base resources.");
    return;
  }

  {
    // the default config path is ":project/...", which would require the project data directory to be mounted already,
    // but that only happens further below, so the path has to be resolved to an absolute one instead
    // (the ":" data directory above is what makes absolute paths readable)
    ezStringBuilder sConfigFile;
    const ezResult resolvedProject = ezFileSystem::ResolveSpecialDirectory(GetProjectDataDirectoryPath(), sConfigFile);
    if (GetRuntimeSession().IsSelected() && resolvedProject.Failed())
    {
      ReportStartupFailure("Cannot resolve the runtime project directory.");
      return;
    }
    sConfigFile.AppendPath("RuntimeConfigs/DataDirectories.ddl");

    ezApplicationFileSystemConfig appFileSystemConfig;
    const ezResult loadedConfig = appFileSystemConfig.LoadChecked(sConfigFile);
    if (GetRuntimeSession().IsSelected() && loadedConfig.Failed())
    {
      ReportStartupFailure("Cannot load runtime data-directory configuration.");
      return;
    }

    // get rid of duplicates that we already hard-coded above
    for (ezUInt32 i = appFileSystemConfig.m_DataDirs.GetCount(); i > 0; --i)
    {
      ezStringBuilder name = appFileSystemConfig.m_DataDirs[i - 1].m_sRootName;
      bool bAbsoluteRoot = name.IsEqual_NoCase(":");
      if (GetRuntimeSession().IsSelected())
      {
        const bool bNamedRoot = !name.IsEmpty();
        ezFileSystem::CleanUpRootName(name);
        bAbsoluteRoot = bNamedRoot && name.IsEmpty();
        appFileSystemConfig.m_DataDirs[i - 1].m_sRootName = name;
      }
      if (bAbsoluteRoot || name.IsEqual_NoCase("bin") || name.IsEqual_NoCase("shadercache") || name.IsEqual_NoCase("appdata") || name.IsEqual_NoCase("base"))
      {
        appFileSystemConfig.m_DataDirs.RemoveAtAndCopy(i - 1);
      }
    }

    // ":project/" is deliberately NOT mounted before the config is applied, because its position *within* the
    // config matters. Data directories are searched back to front, and the editor lists the data directories of
    // the active plugin bundles before the project, so that a project can override plugin provided files
    // (e.g. ship its own version of a particle shader). Mounting the project up front would put every
    // directory from the config, including those, above it again.
    // The config entry is only patched up, so that the (virtual) GetProjectDataDirectoryPath() still decides
    // where the project is, and so that it stays read-only, which is what a shipped game wants.
    ezUInt32 uiProjectIdx = ezInvalidIndex;
    for (ezUInt32 i = 0; i < appFileSystemConfig.m_DataDirs.GetCount(); ++i)
    {
      const ezString name = appFileSystemConfig.m_DataDirs[i].m_sRootName;
      if (name.IsEqual_NoCase("project"))
      {
        uiProjectIdx = i;
        appFileSystemConfig.m_DataDirs[i].m_sDataDirSpecialPath = GetProjectDataDirectoryPath();
        appFileSystemConfig.m_DataDirs[i].m_bWritable = false;
      }
    }

    if (uiProjectIdx == ezInvalidIndex)
    {
      // no config file at all, or one that doesn't mention the project directory
      ezApplicationFileSystemConfig::DataDirConfig cfg;
      cfg.m_sRootName = "project";
      cfg.m_bHardCodedDependency = true;

      uiProjectIdx = appFileSystemConfig.m_DataDirs.GetCount();
      appFileSystemConfig.m_DataDirs.PushBack(cfg);
    }

    appFileSystemConfig.m_DataDirs[uiProjectIdx].m_sDataDirSpecialPath = GetProjectDataDirectoryPath();
    appFileSystemConfig.m_DataDirs[uiProjectIdx].m_bWritable = false;

    if (GetRuntimeSession().IsSelected())
    {
      // Validate the complete normalized configuration before mounting any configured resources.
      for (const auto& config : appFileSystemConfig.m_DataDirs)
      {
        if (!config.m_bWritable)
          continue;
        ezStringBuilder absolutePath;
        if (ezFileSystem::ResolveSpecialDirectory(config.m_sDataDirSpecialPath, absolutePath).Failed())
        {
          ReportStartupFailure("Cannot resolve a configured writable runtime mount.");
          return;
        }
        ezStringBuilder checkedPath;
        const ezStatus pathStatus = GetRuntimeSession().ResolveOutputPath(absolutePath, checkedPath);
        if (pathStatus.Failed())
        {
          ReportStartupFailure(pathStatus.GetMessageString().GetView());
          return;
        }
      }
      ezApplicationFileSystemConfig::Clear();
      for (const auto& config : appFileSystemConfig.m_DataDirs)
      {
        const auto usage = config.m_bWritable && !config.m_sRootName.IsEmpty() ? ezDataDirUsage::AllowWrites : ezDataDirUsage::ReadOnly;
        if (ezFileSystem::AddDataDirectory(config.m_sDataDirSpecialPath, "AppFileSystemConfig", config.m_sRootName, usage).Failed())
        {
          ReportStartupFailure(ezFmt("Cannot mount runtime resource directory '{}'.", config.m_sDataDirSpecialPath));
          return;
        }
      }
    }
    else
    {
      appFileSystemConfig.Apply();
    }
  }

  {
    // We need the file system before we can start the html logger.

    ezGlobalLog::RemoveLogWriter(m_LogToHTML);
    ezStringBuilder sLogFile;
    sLogFile.SetFormat(":appdata/Log.htm");
    m_LogHTML.BeginLog(sLogFile, GetApplicationName());
    if (GetRuntimeSession().IsSelected() && !m_LogHTML.GetOpenedLogFile().IsOpen())
    {
      ReportStartupFailure("Cannot open the private runtime HTML log.");
      return;
    }

    m_LogToHTML = ezGlobalLog::AddLogWriter(ezMakeDelegate(&ezLogWriter::HTML::LogMessageHandler, &m_LogHTML));
  }
}

void ezGameApplicationBase::Init_FileSystem_ConfigureSessionDataDirs()
{
  if (ezFileSystem::AddDataDirectory("", "GameApplicationBase", ":", ezDataDirUsage::ReadOnly).Failed())
  {
    ReportStartupFailure("Cannot mount absolute read-only runtime resources.");
    return;
  }
  for (const char* szRoot : {"bin", "shadercache", "appdata"})
  {
    const ezStringBuilder path(GetRuntimeSession().GetRoot(), "/", szRoot);
    if (ezFileSystem::AddDataDirectory(path, "GameApplicationBase", szRoot, ezDataDirUsage::AllowWrites).Failed())
    {
      ReportStartupFailure(ezFmt("Cannot mount private runtime directory '{}'.", szRoot));
      return;
    }
  }
}

void ezGameApplicationBase::Init_LoadWorldModuleConfig()
{
  ezWorldModuleConfig worldModuleConfig;
  worldModuleConfig.Load();
  worldModuleConfig.Apply();
}

void ezGameApplicationBase::Init_LoadProjectPlugins()
{
  ezApplicationPluginConfig appPluginConfig;
  if (GetRuntimeSession().IsSelected())
  {
    if (appPluginConfig.LoadChecked().Failed())
    {
      ReportStartupFailure("Cannot load runtime plugin configuration.");
      return;
    }
    for (const auto& plugin : appPluginConfig.m_Plugins)
    {
      // Consuming prepared output forbids hot reload. Dependencies use the default original load too.
      if (ezPlugin::LoadPlugin(plugin.m_sAppDirRelativePath).Failed())
      {
        ReportStartupFailure(ezFmt("Cannot load required runtime plugin '{}'.", plugin.m_sAppDirRelativePath));
        return;
      }
      if (ShouldApplicationQuit())
        return;
    }
    return;
  }
  appPluginConfig.Load();
  appPluginConfig.Apply();
}

void ezGameApplicationBase::Init_PlatformProfile_LoadForRuntime()
{
  const ezStringBuilder sRuntimeProfileFile(":project/RuntimeConfigs/", m_PlatformProfile.GetConfigName(), ".ezProfile");
  m_PlatformProfile.AddMissingConfigs();

  const ezResult loadedProfile = m_PlatformProfile.LoadForRuntime(sRuntimeProfileFile);
  if (GetRuntimeSession().IsSelected() && loadedProfile.Failed())
    ReportStartupFailure("Cannot load the runtime platform profile.");
}

void ezGameApplicationBase::Init_ConfigureTags()
{
  EZ_LOG_BLOCK("Reading Tags", "Tags.ddl");

  ezStringView sFile = ":project/RuntimeConfigs/Tags.ddl";

  ezFileReader file;
  if (file.Open(sFile).Failed())
  {
    ezLog::Dev("'{}' does not exist", sFile);
    return;
  }

  ezStringBuilder tmp;

  ezOpenDdlReader reader;
  if (reader.ParseDocument(file).Failed())
  {
    ezLog::Error("Failed to parse DDL data in tags file");
    return;
  }

  const ezOpenDdlReaderElement* pRoot = reader.GetRootElement();

  for (const ezOpenDdlReaderElement* pTags = pRoot->GetFirstChild(); pTags != nullptr; pTags = pTags->GetSibling())
  {
    if (!pTags->IsCustomType("Tag"))
      continue;

    const ezOpenDdlReaderElement* pName = pTags->FindChildOfType(ezOpenDdlPrimitiveType::String, "Name");

    if (!pName)
    {
      ezLog::Error("Incomplete tag declaration!");
      continue;
    }

    tmp = pName->GetPrimitivesString()[0];
    ezTagRegistry::GetGlobalRegistry().RegisterTag(tmp);
  }
}

void ezGameApplicationBase::Init_ConfigureCVars()
{
  ezCVar::SetStorageFolder(":appdata/CVars");
  ezCVar::LoadCVars();
}

void ezGameApplicationBase::Init_SetupDefaultResources()
{
  // continuously unload resources that are not in use anymore
  ezResourceManager::SetAutoFreeUnused(ezTime::MakeFromMicroseconds(100), ezTime::MakeFromSeconds(10.0f));
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

void ezGameApplicationBase::Deinit_UnloadPlugins()
{
  ezPlugin::UnloadAllPlugins();
}

void ezGameApplicationBase::Deinit_ShutdownLogging()
{
#if EZ_DISABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  // during development, keep these loggers active
  ezGlobalLog::RemoveLogWriter(m_LogToConsoleID);
  ezGlobalLog::RemoveLogWriter(m_LogToVsID);
  ezGlobalLog::RemoveLogWriter(m_LogToTracingID);
#endif

  ezGlobalLog::RemoveLogWriter(m_LogToHTML);
  m_LogHTML.EndLog();
}
