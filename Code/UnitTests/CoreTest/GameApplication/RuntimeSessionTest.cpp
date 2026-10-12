#include <CoreTest/CoreTestPCH.h>

#include <Core/GameApplication/RuntimeSession.h>
#include <Foundation/Types/ScopeExit.h>
#include <Foundation/Utilities/CommandLineUtils.h>
#include <TestFramework/Framework/TestFramework.h>

EZ_CREATE_SIMPLE_TEST_GROUP(GameApplication);

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
#  include <Foundation/Platform/Win/Utils/IncludeWindows.h>
#  include <Foundation/Strings/StringConversion.h>
#  include <winioctl.h>

namespace
{
  // A directory junction needs no symlink privilege. Use named fields rather than an opaque captured
  // reparse payload so this native-path fixture remains inspectable.
  bool CreateJunction(ezStringView sLink, ezStringView sTarget)
  {
    struct MountPointData
    {
      DWORD m_uiTag = IO_REPARSE_TAG_MOUNT_POINT;
      WORD m_uiDataLength = 0;
      WORD m_uiReserved = 0;
      WORD m_uiSubstituteOffset = 0;
      WORD m_uiSubstituteLength = 0;
      WORD m_uiPrintOffset = 0;
      WORD m_uiPrintLength = 0;
      wchar_t m_Path[1024] = {};
    } data;
    ezStringBuilder target("\\??\\", sTarget);
    target.ReplaceAll("/", "\\");
    const ezStringWChar wideTarget(target);
    const size_t uiCharacters = wcslen(wideTarget.GetData());
    if (uiCharacters + 2 > EZ_ARRAY_SIZE(data.m_Path))
      return false;
    memcpy(data.m_Path, wideTarget.GetData(), (uiCharacters + 1) * sizeof(wchar_t));
    data.m_uiSubstituteLength = static_cast<WORD>(uiCharacters * sizeof(wchar_t));
    data.m_uiPrintOffset = data.m_uiSubstituteLength + sizeof(wchar_t);
    data.m_uiDataLength = 8 + data.m_uiPrintOffset + sizeof(wchar_t);
    if (!CreateDirectoryW(ezStringWChar(sLink).GetData(), nullptr))
      return false;
    const HANDLE directory = CreateFileW(ezStringWChar(sLink).GetData(), GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
      return false;
    EZ_SCOPE_EXIT(CloseHandle(directory));
    DWORD uiReturned = 0;
    return DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, &data, 8 + data.m_uiDataLength, nullptr, 0, &uiReturned, nullptr) != FALSE;
  }
} // namespace
#endif

EZ_CREATE_SIMPLE_TEST(GameApplication, RuntimeControlOptions)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Optional port and required control")
  {
    struct Case
    {
      const char* m_szArguments;
      bool m_bValid;
      bool m_bEnabled;
      bool m_bRequired;
      ezUInt16 m_uiPort;
    };
    const Case cases[] = {
      {"", true, false, false, 0},
      {"-mcpport 0", true, true, false, 0},
      {"-mcpport 65535", true, true, false, 65535},
      {"-editor-mcpport 1050", true, true, false, 1051},
      {"-mcpport 0 -editor-mcpport 1050", true, true, false, 0},
      {"-mcpRequired -mcpport 0", true, true, true, 0},
      {"-mcpRequired false", true, false, false, 0},
      {"-mcpRequired", false},
      {"-mcpRequired -editor-mcpport 1050", false},
      {"-mcpport", false},
      {"-mcpport -1", false},
      {"-mcpport 65536", false},
      {"-mcpport 99999999999999999999999", false},
      {"-mcpport abc", false},
      {"-mcpport 12x", false},
      {"-mcpport 1 2", false},
      {"-mcpport 0 -MCPPORT 1", false},
      {"-mcpRequired perhaps -mcpport 0", false},
      {"-mcpRequired -mcpRequired -mcpport 0", false},
    };
    for (const auto& test : cases)
    {
      ezDynamicArray<ezString> args;
      ezDynamicArray<const char*> argv;
      ezCommandLineUtils::SplitCommandLineString(test.m_szArguments, true, args, argv);
      ezCommandLineUtils commandLine;
      commandLine.SetCommandLine(args);
      ezRuntimeControlOptions options;
      const ezStatus status = ezRuntimeControlOptions::Parse(commandLine, options);
      EZ_TEST_BOOL_MSG(status.Succeeded() == test.m_bValid, "%s", test.m_szArguments);
      if (status.Succeeded())
      {
        EZ_TEST_BOOL(options.m_bEnabled == test.m_bEnabled);
        EZ_TEST_BOOL(options.m_bRequired == test.m_bRequired);
        EZ_TEST_INT(options.m_uiPort, test.m_uiPort);
      }
    }
  }
}

EZ_CREATE_SIMPLE_TEST(GameApplication, RuntimeSession)
{
#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT) && EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Private roots, CWD, immutable selection and exclusive ownership")
  {
    const ezString sOldCwd = ezOSFile::GetCurrentWorkingDirectory();
    EZ_SCOPE_EXIT(SetCurrentDirectoryW(ezStringWChar(sOldCwd).GetData()));
    ezStringBuilder root(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/RuntimeSession");
    EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(root).Succeeded());
    ezCommandLineUtils commandLine;
    {
      ezRuntimeSession session;
      const ezStatus configured = session.Configure(root, commandLine);
      EZ_TEST_BOOL(configured.Succeeded());
      if (configured.Failed())
        return;
      EZ_TEST_STRING(session.GetRoot(), root);
      EZ_TEST_STRING(ezOSFile::GetCurrentWorkingDirectory(), root);
      for (const char* szDirectory : {"appdata", "bin", "shadercache", "logs"})
      {
        ezStringBuilder path(root, "/", szDirectory);
        EZ_TEST_BOOL(ezOSFile::ExistsDirectory(path));
      }
      EZ_TEST_BOOL(session.Configure(root, commandLine).Failed());
      ezRuntimeSession contender;
      EZ_TEST_BOOL(contender.Configure(root, commandLine).Failed());
      EZ_TEST_BOOL(!contender.IsSelected());

      ezStringBuilder path;
      EZ_TEST_BOOL(session.ResolveOutputPath("logs/test.log", path).Succeeded());
      EZ_TEST_STRING(path, ezStringBuilder(root, "/logs/test.log"));
      EZ_TEST_BOOL(session.ResolveOutputPath("../escaped.log", path).Failed());
      EZ_TEST_BOOL(session.ResolveOutputPath("C:/outside.log", path).Failed());
      EZ_TEST_BOOL(session.ResolveOutputPath("logs/file.log:stream", path).Failed());
    }
    // Closing the owner does not delete the lock pathname; a new owner can acquire the same file.
    ezRuntimeSession successor;
    EZ_TEST_BOOL(successor.Configure(root, commandLine).Succeeded());
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Reject before private stores or CWD change")
  {
    const ezString sOldCwd = ezOSFile::GetCurrentWorkingDirectory();
    ezStringBuilder root(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/RuntimeSessionRejected");
    EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(root).Succeeded());
    ezCommandLineUtils commandLine;
    commandLine.InjectCustomArgument("-logfile");
    commandLine.InjectCustomArgument("C:/outside.log");
    ezRuntimeSession session;
    EZ_TEST_BOOL(session.Configure(root, commandLine).Failed());
    EZ_TEST_BOOL(!session.IsSelected());
    EZ_TEST_BOOL(!ezOSFile::ExistsFile(ezStringBuilder(root, "/runtime-owner.lock")));
    EZ_TEST_BOOL(!ezOSFile::ExistsDirectory(ezStringBuilder(root, "/appdata")));
    EZ_TEST_STRING(ezOSFile::GetCurrentWorkingDirectory(), sOldCwd);

    ezCommandLineUtils empty;
    EZ_TEST_BOOL(session.Configure("", empty).Failed());
    EZ_TEST_BOOL(session.Configure("relative", empty).Failed());
    EZ_TEST_BOOL(session.Configure(ezStringBuilder(root, "/missing"), empty).Failed());
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Reject routed roots and routed descendants before acquiring ownership")
  {
    const ezStringBuilder output(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/RuntimeSessionReparse");
    const ezStringBuilder root(output, "/root");
    const ezStringBuilder outside(output, "/outside");
    const ezStringBuilder link(root, "/routed");
    if (!EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(root).Succeeded()) ||
        !EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(outside).Succeeded()))
      return;
    EZ_SCOPE_EXIT(RemoveDirectoryW(ezStringWChar(link).GetData()));
    if (!EZ_TEST_BOOL(CreateJunction(link, outside)))
      return;
    ezCommandLineUtils commandLine;
    ezRuntimeSession session;
    EZ_TEST_BOOL(session.Configure(link, commandLine).Failed());
    EZ_TEST_BOOL(session.Configure(root, commandLine).Failed());
    EZ_TEST_BOOL(!ezOSFile::ExistsFile(ezStringBuilder(root, "/runtime-owner.lock")));
    EZ_TEST_BOOL(!ezOSFile::ExistsFile(ezStringBuilder(outside, "/runtime-owner.lock")));
  }
#endif
}

EZ_CREATE_SIMPLE_TEST(GameApplication, EarlyOptions)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Missing, empty and duplicate session options")
  {
    ezCommandLineUtils commandLine;
    ezStringView value;
    EZ_TEST_BOOL(ezRuntimeStartup::GetSingleOption(commandLine, "-runtimeSession", value).Succeeded());
    EZ_TEST_BOOL(value.IsEmpty());
    commandLine.InjectCustomArgument("-runtimeSession");
    EZ_TEST_BOOL(ezRuntimeStartup::GetSingleOption(commandLine, "-runtimeSession", value).Failed());
    commandLine.InjectCustomArgument("");
    EZ_TEST_BOOL(ezRuntimeStartup::GetSingleOption(commandLine, "-runtimeSession", value).Failed());
    commandLine.InjectCustomArgument("-runtimeSession");
    commandLine.InjectCustomArgument("C:/session");
    EZ_TEST_BOOL(ezRuntimeStartup::GetSingleOption(commandLine, "-runtimeSession", value).Failed());
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Help presentation recognizes intent before parsing option values")
  {
    ezCommandLineUtils interactive;
    interactive.InjectCustomArgument("-help");
    EZ_TEST_BOOL(!ezRuntimeStartup::IsUnattended(interactive));
    for (const char* szOption : {"-runtimeSession", "-mcpRequired", "-logfile", "-runframes", "-screenshot"})
    {
      ezCommandLineUtils unattended;
      unattended.InjectCustomArgument("-help");
      unattended.InjectCustomArgument(szOption);
      EZ_TEST_BOOL(ezRuntimeStartup::IsUnattended(unattended));
    }
  }
}
