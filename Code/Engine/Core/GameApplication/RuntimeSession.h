#pragma once

#include <Core/CoreDLL.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Types/Status.h>

class ezCommandLineUtils;

/// Early, immutable writable storage for one Development desktop runtime. Not an OS sandbox.
/// Configure before loading plugins or opening logs. The caller owns an ordinary, existing directory;
/// it must not replace its contents with links while the runtime uses it. The exclusive owner file is
/// held until destruction, including application shutdown. CWD is deliberately not restored.
class EZ_CORE_DLL ezRuntimeSession
{
  EZ_DISALLOW_COPY_AND_ASSIGN(ezRuntimeSession);

public:
  ezRuntimeSession() = default;

  /// Validates outputs and acquires ownership before creating private stores or changing CWD.
  /// Failure never selects ordinary stores. A successfully selected instance cannot be reconfigured.
  /// Currently supported on Windows desktop Development DLL builds only.
  /// Inspection rejects trees beyond 4096 entries or 64 levels; callers should supply a fresh root.
  ezStatus Configure(ezStringView sRoot, const ezCommandLineUtils& commandLine);

  bool IsSelected() const { return !m_sRoot.IsEmpty(); }
  ezStringView GetRoot() const { return m_sRoot; }

  /// Resolves a relative output against the selected root, rejects escapes and reparse-routed paths.
  /// No filesystem mutation. Call before creating a new output, not on every frame.
  ezStatus ResolveOutputPath(ezStringView sPath, ezStringBuilder& out_sAbsolutePath) const;

private:
  ezString m_sRoot;
  ezOSFile m_OwnerFile;
};

/// Parsed control intent. Port zero is an enabled ephemeral listener, not a disabled sentinel.
struct EZ_CORE_DLL ezRuntimeControlOptions
{
  bool m_bEnabled = false;
  bool m_bRequired = false;
  ezUInt16 m_uiPort = 0;

  /// Strict own port/required parsing; an own port overrides editor fallback. No logging or IO.
  static ezStatus Parse(const ezCommandLineUtils& commandLine, ezRuntimeControlOptions& out_options);
};

namespace ezRuntimeStartup
{
  /// Rejects duplicate options and anything other than one nonempty value when specified.
  EZ_CORE_DLL ezStatus GetSingleOption(const ezCommandLineUtils& commandLine, ezStringView sOption, ezStringView& out_sValue);

  /// Player uses inherited output instead of a modal dialog for these launch intents, even for help.
  EZ_CORE_DLL bool IsUnattended(const ezCommandLineUtils& commandLine);
} // namespace ezRuntimeStartup
