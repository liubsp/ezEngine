#include <EditorTest/EditorTestPCH.h>

#include <EditorPluginMcp/McpTools/AppTool.h>
#include <EditorTest/TestClass/TestClass.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

class ezEditorQuitSafetyTest : public ezEditorTest
{
public:
  const char* GetTestName() const override { return "Editor Quit Safety"; }

  void SetupSubTests() override
  {
    AddSubTest("Unsaved refusal and deferred modification recheck", 0);
  }

  ezTestAppRun RunSubTest(ezInt32, ezUInt32) override
  {
    // This fixture tests document closure, not desktop mapping. Project opening normally maximizes
    // the test-owned main window; keep it offscreen rather than changing production window policy.
    ezQtContainerWindow::GetContainerWindow()->setAttribute(Qt::WA_DontShowOnScreen);
    if (!EZ_TEST_BOOL(CreateAndLoadProject("QuitSafety").Succeeded()))
      return ezTestAppRun::Quit;

    auto* pEditor = ezQtEditorApp::GetSingleton();
    ezStringBuilder sDocument(m_sProjectPath, "/QuitSafety.ezScene");
    ezDocument* pDocument = pEditor->CreateDocument(sDocument, ezDocumentFlags::None);
    if (!EZ_TEST_BOOL(pDocument != nullptr))
      return ezTestAppRun::Quit;

    ezMcpEditorAppTool provider;
    ezMcpToolResult info;
    provider.Execute("app_info", {}, info);
    const auto instance = QJsonDocument::fromJson(QByteArray(info.m_sText.GetData())).object().value("editorQuitInstance").toString().toUtf8();
    ezVariantDictionary arguments;
    arguments.Insert("expectedInstance", ezString(instance.constData()));

    if (!Modify(*pDocument))
      return ezTestAppRun::Quit;
    EZ_TEST_BOOL(!pDocument->HasWindowBeenRequested());
    ezMcpToolResult refused;
    provider.Execute("app_quit_if_instance", arguments, refused);
    EZ_TEST_BOOL(refused.m_bIsError);
    EZ_TEST_BOOL(!QJsonDocument::fromJson(QByteArray(refused.m_sText.GetData())).object().value("quitting").toBool(true));
    ProcessEvents(1);
    EZ_TEST_BOOL(pEditor->IsRunning());
    if (!EZ_TEST_BOOL(IsDocumentOpen(pDocument)))
      return ezTestAppRun::Quit;
    EZ_TEST_BOOL(pDocument->IsModified());

    if (!EZ_TEST_BOOL(pDocument->SaveDocument().Succeeded()))
      return ezTestAppRun::Quit;
    ezMcpToolResult accepted;
    provider.Execute("app_quit_if_instance", arguments, accepted);
    if (!EZ_TEST_BOOL(!accepted.m_bIsError))
      return ezTestAppRun::Quit;
    EZ_TEST_BOOL(QJsonDocument::fromJson(QByteArray(accepted.m_sText.GetData())).object().value("quitting").toBool());

    // Change the document after acceptance, before Qt can execute the queued close.
    // Pump only after mutation succeeds, so a broken fixture cannot accidentally close the editor.
    const bool bModified = Modify(*pDocument);
    if (!bModified)
      return ezTestAppRun::Quit;
    ProcessEvents(1);
    EZ_TEST_BOOL(pEditor->IsRunning());
    EZ_TEST_BOOL(ezQtContainerWindow::GetContainerWindow()->GetDockManager() != nullptr);
    if (!EZ_TEST_BOOL(IsDocumentOpen(pDocument)))
      return ezTestAppRun::Quit;
    EZ_TEST_BOOL(pDocument->IsModified());
    EZ_TEST_BOOL(pDocument->SaveDocument().Succeeded());
    return ezTestAppRun::Quit;
  }

private:
  bool IsDocumentOpen(const ezDocument* pDocument) const
  {
    for (const auto* pManager : ezDocumentManager::GetAllDocumentManagers())
    {
      for (const auto* pOpenDocument : pManager->GetAllOpenDocuments())
      {
        if (pOpenDocument == pDocument)
          return true;
      }
    }
    return false;
  }

  bool Modify(ezDocument& inout_document)
  {
    auto* pAccessor = inout_document.GetObjectAccessor();
    pAccessor->StartTransaction("Modify quit-safety fixture");
    ezUuid object;
    const ezStatus status = pAccessor->AddObjectByName(inout_document.GetObjectManager()->GetRootObject(), "Children", -1,
      ezRTTI::FindTypeByName("ezGameObject"), object);
    if (!EZ_TEST_BOOL(status.Succeeded()))
    {
      pAccessor->CancelTransaction();
      return false;
    }
    pAccessor->FinishTransaction();
    return EZ_TEST_BOOL(inout_document.IsModified());
  }
};

static ezEditorQuitSafetyTest s_EditorQuitSafetyTest;
