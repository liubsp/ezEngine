#include <Player/Player.h>

#include <Core/Input/DeviceTypes/MouseKeyboard.h>
#include <Core/Input/InputManager.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Logging/Log.h>
#include <Foundation/Utilities/CommandLineOptions.h>

#include <cstdio>

// this injects the main function
EZ_APPLICATION_ENTRY_POINT(ezPlayerApplication);

// these command line options may not all be directly used in ezPlayer, but the ezFallbackGameState reads those options to determine which scene to load
ezCommandLineOptionString opt_Project("_Player", "-project", "Path to the project folder.\nUsually an absolute path, though relative paths will work for projects that are located inside the EZ SDK directory.", "");
ezCommandLineOptionString opt_Scene("_Player", "-scene", "Path to a scene file.\nUsually given relative to the corresponding project data directory where it resides, but can also be given as an absolute path.", "");


ezPlayerApplication::ezPlayerApplication()
  : ezGameApplication("ezPlayer", nullptr) // we don't have a fixed project path in this app, so we need to pass that in a bit later
{
}

ezResult ezPlayerApplication::BeforeCoreSystemsStartup()
{
  // show the command line options, if help is requested
  {
    ezStringBuilder cmdHelp;
    if (ezCommandLineOption::LogAvailableOptionsToBuffer(cmdHelp, ezCommandLineOption::LogAvailableModes::IfHelpRequested))
    {
      if (ezRuntimeStartup::IsUnattended(*ezCommandLineUtils::GetGlobalInstance()))
      {
        // File-backed inherited handles also work for the GUI-subsystem Player; never create a dialog.
        fprintf(stdout, "%s\n", cmdHelp.GetData());
        fflush(stdout);
        SetReturnCode(0);
      }
      else
      {
        ezLog::OsMessageBox(cmdHelp);
        SetReturnCode(-1);
      }
      return EZ_FAILURE;
    }
  }

  // SDK/project discovery reads only. Reject invalid launch arguments before the base opens logs,
  // acquires a session directory, starts subsystems or loads plugins.
  const ezStatus projectStatus = DetermineProjectPath();
  if (projectStatus.Failed())
  {
    ReportStartupFailure(projectStatus.GetMessageString().GetView());
    return EZ_FAILURE;
  }
  ezStartup::AddApplicationTag("player");
  return SUPER::BeforeCoreSystemsStartup();
}

ezStatus ezPlayerApplication::DetermineProjectPath()
{
  const auto& commandLine = *ezCommandLineUtils::GetGlobalInstance();
  ezStringView project, scene;
  EZ_SUCCEED_OR_RETURN(ezRuntimeStartup::GetSingleOption(commandLine, "-project", project));
  EZ_SUCCEED_OR_RETURN(ezRuntimeStartup::GetSingleOption(commandLine, "-scene", scene));
  ezStringBuilder sProjectPath(project);
  if (ezFileSystem::DetectSdkRootDirectory().Failed())
    return ezStatus("Cannot locate the SDK root for Player.");

#if EZ_DISABLED(EZ_SUPPORTS_UNRESTRICTED_FILE_ACCESS)
  // We can't specify command line arguments on many platforms so the project must be defined by ezFileserve.
  // ezFileserve must be started with the project special dir set. For example:
  // -specialdirs project ".../ezEngine/Data/Samples/Testing Chambers

  if (sProjectPath.IsEmpty())
  {
    m_sAppProjectPath = ">project";
    return EZ_SUCCESS;
  }
#endif

  if (sProjectPath.IsEmpty())
  {
    const ezStringBuilder sScenePath(scene);

    // project path is empty, need to extract it from the scene path

    if (!sScenePath.IsAbsolutePath())
    {
      // scene path is not absolute -> can't extract project path
      return ezStatus("Provide -project or an absolute -scene path from which Player can locate ezProject.");
    }

    if (ezFileSystem::FindFolderWithSubPath(sProjectPath, sScenePath, "ezProject", "ezSdkRoot.txt").Failed())
    {
      // couldn't find the 'ezProject' file in any parent folder of the scene
      return ezStatus("Cannot locate ezProject above the supplied scene path.");
    }
  }
  else if (!ezPathUtils::IsAbsolutePath(sProjectPath))
  {
    // project path is not absolute, so must be relative to the SDK directory
    sProjectPath.Prepend(ezFileSystem::GetSdkRootDirectory(), "/");
  }

  sProjectPath.MakeCleanPath();
  sProjectPath.TrimWordEnd("/ezProject");

  if (sProjectPath.IsEmpty() || !ezOSFile::ExistsDirectory(sProjectPath))
  {
    return ezStatus("The supplied Player project directory does not exist.");
  }

  m_sAppProjectPath = sProjectPath;
  return EZ_SUCCESS;
}
