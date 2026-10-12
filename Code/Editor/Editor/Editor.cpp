#include <Editor/EditorPCH.h>

#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/Utilities/CommandLineOptions.h>
#include <RendererFoundation/Device/DeviceFactory.h>

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
#  include <Foundation/Platform/Win/Utils/IncludeWindows.h>
#  include <shellscalingapi.h>
#endif

namespace
{
  /// Writes to inherited stdout (including a redirected file), or attaches to the parent's console.
  /// A double-click launch with neither has nowhere to write; never open UI for help/diagnostics.
  bool WriteCommandLineOutput(ezStringView sText)
  {
    const ezStringBuilder sOut(sText);

#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
    // Attaching may replace standard handles. Preserve an explicit inherited capture handle.
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE)
    {
      ::AttachConsole(ATTACH_PARENT_PROCESS);
      hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    }

    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE)
    {
      return false;
    }

    // written through the handle rather than printf, because the CRT's stdout is not connected to a
    // console that was only attached just now
    DWORD uiOffset = 0;
    while (uiOffset < sOut.GetElementCount())
    {
      DWORD uiWritten = 0;
      if (!::WriteFile(hOut, sOut.GetData() + uiOffset, sOut.GetElementCount() - uiOffset, &uiWritten, nullptr) || uiWritten == 0)
      {
        return false;
      }
      uiOffset += uiWritten;
    }
    return true;
#else
    // every other platform this builds for has a normal stdout
    const size_t uiWritten = fwrite(sOut.GetData(), 1, sOut.GetElementCount(), stdout);
    return fflush(stdout) == 0 && uiWritten == sOut.GetElementCount();
#endif
  }
} // namespace

class ezEditorApplication : public ezApplication
{
public:
  using SUPER = ezApplication;

  ezEditorApplication()
    : ezApplication("ezEditor")
  {
#if EZ_ENABLED(EZ_PLATFORM_WINDOWS_DESKTOP)
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif
    EnableMemoryLeakReporting(true);

    m_pEditorApp = new ezQtEditorApp;
  }

  virtual ezResult BeforeCoreSystemsStartup() override
  {
    ezStartup::AddApplicationTag("tool");
    ezStartup::AddApplicationTag("editor");
    ezStartup::AddApplicationTag("editorapp");

    ezQtEditorApp::GetSingleton()->InitQt(GetArgumentCount(), (char**)GetArgumentsArray());

    return EZ_SUCCESS;
  }

  virtual void AfterCoreSystemsShutdown() override
  {
    ezQtEditorApp::GetSingleton()->DeInitQt();

    delete m_pEditorApp;
    m_pEditorApp = nullptr;
  }

  virtual void Run() override
  {
    {
      ezStringBuilder cmdHelp;
      if (ezCommandLineOption::LogAvailableOptionsToBuffer(cmdHelp, ezCommandLineOption::LogAvailableModes::IfHelpRequested, "_Editor;cvar"))
      {
        const ezStringView sRequiredRenderer = ezGALDeviceFactory::GetRequiredRenderer();
        if (!sRequiredRenderer.IsEmpty())
          cmdHelp.AppendFormat("\nRequired renderer: {}\n", sRequiredRenderer);

        if (!WriteCommandLineOutput(cmdHelp))
        {
          SetReturnCode(1);
        }

        QuitApplication();
        return;
      }
    }

    ezQtEditorApp::GetSingleton()->StartupEditor();
    {
      const ezInt32 iReturnCode = ezQtEditorApp::GetSingleton()->RunEditor();
      SetReturnCode(iReturnCode);
    }
    ezQtEditorApp::GetSingleton()->ShutdownEditor();

    QuitApplication();
  }

private:
  ezQtEditorApp* m_pEditorApp;
};

EZ_APPLICATION_ENTRY_POINT(ezEditorApplication);
