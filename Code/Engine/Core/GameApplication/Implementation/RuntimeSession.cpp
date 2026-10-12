#include <Core/CorePCH.h>

#include <Core/GameApplication/RuntimeSession.h>
#include <Foundation/Utilities/CommandLineOptions.h>
#include <Foundation/Utilities/CommandLineUtils.h>

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
#  include <Foundation/Platform/Win/Utils/IncludeWindows.h>
#  include <Foundation/Strings/StringConversion.h>
#endif

static ezCommandLineOptionString s_opt_RuntimeSession("_App", "-runtimeSession",
  "Existing absolute private directory for a Development runtime. Owns writable stores and CWD until shutdown.", "");
static ezCommandLineOptionInt s_opt_McpPort("_Mcp", "-mcpport",
  "Serve MCP on loopback. Explicit zero requests an ephemeral port. Absent means disabled unless an editor port is supplied.", 0, 0, 65535);
static ezCommandLineOptionBool s_opt_McpRequired("_Mcp", "-mcpRequired",
  "Fail startup if MCP cannot start. Requires an explicit -mcpport, including zero.", false);

namespace
{
  ezUInt32 CountOption(const ezCommandLineUtils& commandLine, ezStringView sOption)
  {
    ezUInt32 uiCount = 0;
    for (const auto& argument : commandLine.GetCommandLineArray())
    {
      if (argument.IsEqual_NoCase(sOption))
        ++uiCount;
    }
    return uiCount;
  }

  ezStatus ParsePort(ezStringView sValue, ezUInt16& out_uiPort)
  {
    if (sValue.IsEmpty())
      return ezStatus("MCP port must be a decimal integer in [0, 65535].");

    ezUInt32 uiPort = 0;
    for (const char* p = sValue.GetStartPointer(); p < sValue.GetEndPointer(); ++p)
    {
      if (*p < '0' || *p > '9')
        return ezStatus("MCP port must contain only decimal digits.");
      uiPort = uiPort * 10 + (*p - '0');
      if (uiPort > 65535)
        return ezStatus("MCP port must be in [0, 65535].");
    }
    out_uiPort = static_cast<ezUInt16>(uiPort);
    return EZ_SUCCESS;
  }

  // These checks are local to the supported runtime lane, not a general filesystem interceptor.
  // Reject namespace aliases, ambiguous Win32 components and reparse points before any write.
  ezStatus CheckOrdinaryPath(ezStringView sPath)
  {
#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
    ezStringBuilder path(sPath);
    path.ReplaceAll("\\", "/");
    if (path.GetElementCount() < 3 || path.GetData()[1] != ':' || path.GetData()[2] != '/' ||
        !((path.GetData()[0] >= 'A' && path.GetData()[0] <= 'Z') || (path.GetData()[0] >= 'a' && path.GetData()[0] <= 'z')))
      return ezStatus("Runtime paths require an absolute local drive path.");

    ezHybridArray<ezStringView, 16> components;
    path.GetView().Split(false, components, "/");
    for (ezUInt32 i = 1; i < components.GetCount(); ++i)
    {
      const ezStringView component = components[i];
      if (component.EndsWith(".") || component.EndsWith(" ") || component.FindSubString(":") != nullptr ||
          component.FindSubString("*") != nullptr || component.FindSubString("?") != nullptr)
        return ezStatus("Runtime paths cannot contain traversal, streams, wildcards or ambiguous trailing characters.");
      ezStringBuilder stem(component);
      const char* pDot = stem.FindSubString(".");
      if (pDot != nullptr)
        stem.SetSubString_FromTo(stem.GetData(), pDot);
      if (stem.IsEqual_NoCase("CON") || stem.IsEqual_NoCase("PRN") || stem.IsEqual_NoCase("AUX") || stem.IsEqual_NoCase("NUL") ||
          (stem.GetElementCount() == 4 && (stem.StartsWith_NoCase("COM") || stem.StartsWith_NoCase("LPT")) &&
            stem.GetData()[3] >= '0' && stem.GetData()[3] <= '9'))
        return ezStatus("Runtime paths cannot name DOS devices.");
    }

    path.TrimWordEnd("/");
    while (path.GetElementCount() > 2)
    {
      const DWORD attributes = GetFileAttributesW(ezStringWChar(path).GetData());
      if (attributes == INVALID_FILE_ATTRIBUTES)
      {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
          return ezStatus(ezFmt("Cannot inspect runtime path '{}'.", path));
      }
      else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      {
        return ezStatus(ezFmt("Runtime path '{}' is routed through a reparse point.", path));
      }
      path.PathParentDirectory();
      path.TrimWordEnd("/");
    }
    return EZ_SUCCESS;
#else
    return ezStatus("Runtime sessions are supported only on Windows desktop Development builds.");
#endif
  }

  ezStatus ResolveBelowRoot(ezStringView sRoot, ezStringView sPath, ezStringBuilder& out_sPath)
  {
    if (sPath.IsEmpty())
      return ezStatus("Runtime output path cannot be empty.");
    ezStringBuilder path(sPath);
    if (!path.IsAbsolutePath())
      path.Prepend(sRoot, "/");
    EZ_SUCCEED_OR_RETURN(CheckOrdinaryPath(path));
    path.MakeCleanPath();
    const ezStringBuilder root(sRoot);
    if (path.IsEqual_NoCase(root) || !path.IsPathBelowFolder(root))
      return ezStatus("Runtime output must be below the selected session directory.");
    out_sPath = path;
    return EZ_SUCCESS;
  }

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT) && EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)
  ezStatus CheckPrivateTree(ezStringView sDirectory, ezUInt32 uiDepth, ezUInt32& inout_uiEntries)
  {
    // Fresh sessions normally contain only launch records. Bound inspection of accidentally reused
    // trees, and never recurse through a reparse point (the OS iterator's recursive mode would).
    if (uiDepth > 64)
      return ezStatus("Runtime session directory nesting exceeds 64 levels.");
    WIN32_FIND_DATAW entry;
    const ezStringBuilder pattern(sDirectory, "/*");
    const HANDLE search = FindFirstFileW(ezStringWChar(pattern).GetData(), &entry);
    if (search == INVALID_HANDLE_VALUE)
    {
      if (GetLastError() == ERROR_FILE_NOT_FOUND)
        return EZ_SUCCESS;
      return ezStatus("Cannot inspect the runtime session directory contents.");
    }
    EZ_SCOPE_EXIT(FindClose(search));
    do
    {
      const ezStringUtf8 name(entry.cFileName);
      if (name.GetView() == "." || name.GetView() == "..")
        continue;
      if (++inout_uiEntries > 4096)
        return ezStatus("Runtime session must be a fresh directory, not an existing large data tree.");
      const ezStringBuilder path(sDirectory, "/", name.GetView());
      EZ_SUCCEED_OR_RETURN(CheckOrdinaryPath(path));
      if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        EZ_SUCCEED_OR_RETURN(CheckPrivateTree(path, uiDepth + 1, inout_uiEntries));
    } while (FindNextFileW(search, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
      return ezStatus("Cannot finish inspecting the runtime session directory contents.");
    return EZ_SUCCESS;
  }

  ezStatus CreateWritableStore(ezStringView sPath)
  {
    if (ezOSFile::CreateDirectoryStructure(sPath).Failed())
      return ezStatus(ezFmt("Cannot create private runtime store '{}'.", sPath));
    // CREATE_NEW never truncates a retained file. This reserved probe also checks ACLs on an
    // already existing store before any logger/plugin can start. Ownership excludes other runtimes.
    const ezStringBuilder probe(sPath, "/runtime-write-probe.tmp");
    const HANDLE file = CreateFileW(ezStringWChar(probe).GetData(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
      return ezStatus(ezFmt("Private runtime store '{}' is not writable or contains a conflicting probe.", sPath));
    CloseHandle(file);
    if (!DeleteFileW(ezStringWChar(probe).GetData()))
      return ezStatus("Cannot remove the private runtime write probe.");
    return EZ_SUCCESS;
  }
#endif
} // namespace

ezStatus ezRuntimeStartup::GetSingleOption(const ezCommandLineUtils& commandLine, ezStringView sOption, ezStringView& out_sValue)
{
  out_sValue = {};
  const ezUInt32 uiCount = CountOption(commandLine, sOption);
  if (uiCount == 0)
    return EZ_SUCCESS;
  if (uiCount != 1 || commandLine.GetStringOptionArguments(sOption) != 1)
    return ezStatus(ezFmt("{} requires exactly one value and cannot be repeated.", sOption));
  out_sValue = commandLine.GetStringOption(sOption);
  if (out_sValue.IsEmpty())
    return ezStatus(ezFmt("{} cannot be empty.", sOption));
  return EZ_SUCCESS;
}

bool ezRuntimeStartup::IsUnattended(const ezCommandLineUtils& commandLine)
{
  for (const char* szOption : {"-runtimeSession", "-mcpRequired", "-mcpport", "-demoSession", "-demoOffscreen",
         "-runframes", "-timeout", "-screenshot", "-logfile", "-failonerror", "-fixedtimestep", "-seed"})
  {
    if (commandLine.HasOption(szOption))
      return true;
  }
  return false;
}

ezStatus ezRuntimeControlOptions::Parse(const ezCommandLineUtils& commandLine, ezRuntimeControlOptions& out_options)
{
  ezRuntimeControlOptions options;
  const ezUInt32 uiRequiredCount = CountOption(commandLine, "-mcpRequired");
  if (uiRequiredCount > 1 || commandLine.GetStringOptionArguments("-mcpRequired") > 1)
    return ezStatus("-mcpRequired cannot be repeated and accepts at most one boolean value.");
  if (uiRequiredCount == 1)
  {
    const ezStringView value = commandLine.GetStringOption("-mcpRequired", 0, "true");
    if (!value.IsEqual_NoCase("true") && !value.IsEqual_NoCase("false"))
      return ezStatus("-mcpRequired expects true or false.");
    options.m_bRequired = value.IsEqual_NoCase("true");
  }

  ezStringView ownPort;
  EZ_SUCCEED_OR_RETURN(ezRuntimeStartup::GetSingleOption(commandLine, "-mcpport", ownPort));
  if (commandLine.HasOption("-mcpport"))
  {
    EZ_SUCCEED_OR_RETURN(ParsePort(ownPort, options.m_uiPort));
    options.m_bEnabled = true;
  }
  else
  {
    if (options.m_bRequired)
      return ezStatus("-mcpRequired requires an explicit -mcpport.");
    // Preserve the optional editor fallback: only a valid nonzero editor port admits +1.
    ezStringView editorPort;
    ezUInt16 uiEditorPort = 0;
    if (ezRuntimeStartup::GetSingleOption(commandLine, "-editor-mcpport", editorPort).Succeeded() &&
        ParsePort(editorPort, uiEditorPort).Succeeded() && uiEditorPort > 0 && uiEditorPort < 65535)
    {
      options.m_bEnabled = true;
      options.m_uiPort = uiEditorPort + 1;
    }
  }
  out_options = options;
  return EZ_SUCCESS;
}

ezStatus ezRuntimeSession::Configure(ezStringView sRoot, const ezCommandLineUtils& commandLine)
{
  if (IsSelected())
    return ezStatus("Runtime session selection is immutable.");
#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP) && EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT) && EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)
  EZ_SUCCEED_OR_RETURN(CheckOrdinaryPath(sRoot));
  ezStringBuilder root(sRoot);
  root.MakeCleanPath();
  root.TrimWordEnd("/");
  if (root.GetElementCount() <= 3 || !ezOSFile::ExistsDirectory(root))
    return ezStatus("Runtime session requires an existing directory, not a drive root.");
  if (commandLine.GetBoolOption("-telemetry") || commandLine.HasOption("-fs_server") ||
      commandLine.HasOption("-fs_port") || commandLine.HasOption("-fileserve"))
    return ezStatus("Runtime sessions do not support telemetry or fileserve activation.");

  for (const char* szOutput : {"-logfile", "-screenshot"})
  {
    ezStringView value;
    EZ_SUCCEED_OR_RETURN(ezRuntimeStartup::GetSingleOption(commandLine, szOutput, value));
    if (commandLine.HasOption(szOutput))
    {
      ezStringBuilder resolved;
      EZ_SUCCEED_OR_RETURN(ResolveBelowRoot(root, value, resolved));
    }
  }
  for (const char* szChild : {"runtime-owner.lock", "appdata", "bin", "shadercache", "logs"})
  {
    EZ_SUCCEED_OR_RETURN(CheckOrdinaryPath(ezStringBuilder(root, "/", szChild)));
  }
  ezUInt32 uiEntries = 0;
  EZ_SUCCEED_OR_RETURN(CheckPrivateTree(root, 0, uiEntries));

  const ezStringBuilder ownerPath(root, "/runtime-owner.lock");
  if (m_OwnerFile.Open(ownerPath, ezFileOpenMode::Append, ezFileShareMode::Exclusive).Failed())
    return ezStatus("Cannot acquire runtime-owner.lock; the session is owned or not writable.");
  EZ_SCOPE_EXIT(if (!IsSelected()) { m_OwnerFile.Close(); });

  for (const char* szChild : {"appdata", "bin", "shadercache", "logs"})
  {
    EZ_SUCCEED_OR_RETURN(CreateWritableStore(ezStringBuilder(root, "/", szChild)));
  }
  if (!SetCurrentDirectoryW(ezStringWChar(root).GetData()))
    return ezStatus("Cannot select the runtime session working directory.");
  m_sRoot = root;
  return EZ_SUCCESS;
#else
  return ezStatus("Runtime sessions require a Windows desktop Development DLL build.");
#endif
}

ezStatus ezRuntimeSession::ResolveOutputPath(ezStringView sPath, ezStringBuilder& out_sAbsolutePath) const
{
  if (!IsSelected())
    return ezStatus("No runtime session is selected.");
  return ResolveBelowRoot(m_sRoot, sPath, out_sAbsolutePath);
}
