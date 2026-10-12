#include <EditorTest/EditorTestPCH.h>

#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <EditorPluginMcp/McpTools/AppTool.h>
#include <Foundation/Application/Application.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/Utilities/CommandLineOptions.h>
#include <Mcp/McpToolRegistry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <TestFramework/Framework/TestFramework.h>

namespace
{
  // Core/Qt only: no project, renderer, preference redirection or full editor startup.
  class PresentationApplication : public ezApplication
  {
  public:
    PresentationApplication()
      : ezApplication("EditorPresentationTest")
    {
    }

    ezResult BeforeCoreSystemsStartup() override
    {
      ezStartup::AddApplicationTag("tool");
      m_Editor.InitQt(GetArgumentCount(), const_cast<char**>(GetArgumentsArray()));
      return EZ_SUCCESS;
    }

    void AfterCoreSystemsShutdown() override { m_Editor.DeInitQt(); }
    void Run() override {}

  private:
    ezQtEditorApp m_Editor;
  };

  // Execute and AddHostInfo are the real editor provider; only shutdown effects are replaced.
  class QuitProbe : public ezMcpEditorAppTool
  {
  public:
    ezUInt32 m_uiChecks = 0;
    ezUInt32 m_uiRequests = 0;
    bool m_bRefuse = false;
    bool m_bDiscard = false;

  protected:
    ezResult CanQuit(bool bDiscardChanges) override
    {
      ++m_uiChecks;
      m_bDiscard = bDiscardChanges;
      return m_bRefuse ? EZ_FAILURE : EZ_SUCCESS;
    }

    void RequestQuit(bool bDiscardChanges) override
    {
      ++m_uiRequests;
      m_bDiscard = bDiscardChanges;
    }
  };

  ezString GetQuitInstance(QuitProbe& inout_provider)
  {
    ezMcpToolResult result;
    inout_provider.Execute("app_info", {}, result);
    EZ_TEST_BOOL(!result.m_bIsError);
    const auto info = QJsonDocument::fromJson(QByteArray(result.m_sText.GetData())).object();
    const QByteArray instance = info.value("editorQuitInstance").toString().toUtf8();
    EZ_TEST_BOOL(!instance.isEmpty());
    return ezString(instance.constData());
  }
} // namespace

class ezEditorPresentationTest : public ezTestBaseClass
{
public:
  const char* GetTestName() const override { return "Editor Presentation"; }

  void SetupSubTests() override
  {
    AddSubTest("Minimized startup and ordinary default", 0);
    AddSubTest("Execution-bound guarded quit", 1);
  }

  ezResult InitializeTest() override
  {
    m_Arguments = ezCommandLineUtils::GetGlobalInstance()->GetCommandLineArray();
    for (const auto& argument : m_Arguments)
      m_ArgumentPointers.PushBack(argument.GetData());
    m_pApplication = EZ_DEFAULT_NEW(PresentationApplication);
    m_pApplication->SetCommandLineArguments(m_ArgumentPointers.GetCount(), m_ArgumentPointers.GetData());
    return ezRun_Startup(m_pApplication);
  }

  ezResult DeInitializeTest() override
  {
    if (m_pApplication != nullptr)
    {
      ezRun_Shutdown(m_pApplication);
      EZ_DEFAULT_DELETE(m_pApplication);
    }
    m_ArgumentPointers.Clear();
    ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(m_Arguments);
    return EZ_SUCCESS;
  }

  ezTestAppRun RunSubTest(ezInt32 iIdentifier, ezUInt32) override
  {
    if (iIdentifier == 0)
    {
      ezStringBuilder help;
      EZ_TEST_BOOL(ezCommandLineOption::LogAvailableOptionsToBuffer(help, ezCommandLineOption::LogAvailableModes::Always, "_Editor"));
      EZ_TEST_BOOL(help.FindSubString("-minimized") != nullptr);
      EZ_TEST_BOOL(help.FindSubString("-editorWindow") == nullptr);

      ezQtContainerWindow container;
      // Logical state coverage without mapping an automated maximized window onto the desktop.
      container.setAttribute(Qt::WA_DontShowOnScreen);
      ezQtEditorApp::ShowStartupWindow(true);
      EZ_TEST_BOOL(container.isMinimized());
      EZ_TEST_BOOL(container.isVisible());
      ezQtEditorApp::ShowStartupWindow(false);
      EZ_TEST_BOOL(container.isMaximized());
      EZ_TEST_BOOL(!container.isMinimized());
      container.hide();
    }
    else
    {
      TestGuardedQuit();
    }
    return ezTestAppRun::Quit;
  }

private:
  void TestGuardedQuit()
  {
    QuitProbe original;
    const ezString instance = GetQuitInstance(original);
    EZ_TEST_STRING(GetQuitInstance(original), instance);
    QuitProbe replacement;
    const ezString replacementInstance = GetQuitInstance(replacement);
    EZ_TEST_BOOL(instance != replacementInstance);

    ezDynamicArray<ezMcpToolDesc> tools;
    original.GetSupportedTools(tools);
    bool bGuarded = false;
    bool bOrdinary = false;
    for (const auto& tool : tools)
    {
      bGuarded |= tool.m_sName == "app_quit_if_instance";
      bOrdinary |= tool.m_sName == "app_quit";
    }
    EZ_TEST_BOOL(bGuarded && bOrdinary);

    ezVariantDictionary arguments;
    auto ExpectRejected = [&original](const ezVariantDictionary& input)
    {
      ezMcpToolResult result;
      original.Execute("app_quit_if_instance", input, result);
      EZ_TEST_BOOL(result.m_bIsError);
      EZ_TEST_INT(original.m_uiChecks, 0);
      EZ_TEST_INT(original.m_uiRequests, 0);
    };
    ExpectRejected(arguments);
    for (const char* szInvalid : {"", "not-a-uuid", "{ z0000000-0000-0000-0000-000000000000 }", "{ 00000000-0000-0000-0000-000000000000 }"})
    {
      arguments.Insert("expectedInstance", ezString(szInvalid));
      ExpectRejected(arguments);
    }
    arguments.Insert("expectedInstance", 42);
    ExpectRejected(arguments);
    arguments.Insert("expectedInstance", replacementInstance);
    ExpectRejected(arguments);
    arguments.Insert("expectedInstance", instance);
    arguments.Insert("discardChanges", false);
    ExpectRejected(arguments);
    arguments.Insert("discardChanges", true);
    ExpectRejected(arguments);
    arguments.Remove("discardChanges");
    arguments.Insert("extra", true);
    ExpectRejected(arguments);
    arguments.Remove("extra");

    // A preflight token from A must not authorize B, even if the endpoint changed after preflight.
    ezMcpToolResult replaced;
    replacement.Execute("app_quit_if_instance", arguments, replaced);
    EZ_TEST_BOOL(replaced.m_bIsError);
    EZ_TEST_INT(replacement.m_uiChecks, 0);
    EZ_TEST_INT(replacement.m_uiRequests, 0);

    original.m_bRefuse = true;
    ezMcpToolResult refused;
    original.Execute("app_quit_if_instance", arguments, refused);
    EZ_TEST_BOOL(refused.m_bIsError);
    EZ_TEST_INT(original.m_uiChecks, 1);
    EZ_TEST_INT(original.m_uiRequests, 0);
    EZ_TEST_BOOL(!original.m_bDiscard);

    original.m_bRefuse = false;
    ezMcpToolResult accepted;
    original.Execute("app_quit_if_instance", arguments, accepted);
    EZ_TEST_BOOL(!accepted.m_bIsError);
    EZ_TEST_BOOL(QJsonDocument::fromJson(QByteArray(accepted.m_sText.GetData())).object().value("quitting").toBool());
    EZ_TEST_INT(original.m_uiRequests, 1);
    EZ_TEST_BOOL(!original.m_bDiscard);

    // Other base operations retain their dispatch, including explicit direct-call discard.
    ezMcpToolResult ping;
    original.Execute("app_ping", {}, ping);
    EZ_TEST_BOOL(QJsonDocument::fromJson(QByteArray(ping.m_sText.GetData())).object().value("responsive").toBool());
    arguments.Clear();
    arguments.Insert("discardChanges", true);
    ezMcpToolResult direct;
    original.Execute("app_quit", arguments, direct);
    EZ_TEST_INT(original.m_uiRequests, 2);
    EZ_TEST_BOOL(original.m_bDiscard);
  }

  PresentationApplication* m_pApplication = nullptr;
  ezDynamicArray<ezString> m_Arguments;
  ezDynamicArray<const char*> m_ArgumentPointers;
};

static ezEditorPresentationTest s_EditorPresentationTest;
