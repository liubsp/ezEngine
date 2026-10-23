#include <EditorTest/EditorTestPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorTest/TestClass/TestClass.h>
#include <Foundation/Utilities/Progress.h>
#include <GameEngine/Configuration/RendererProfileConfigs.h>
#include <Mcp/McpServer.h>
#include <Mcp/McpTool.h>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTimer>
#include <ToolsFoundation/Project/ToolsProject.h>

namespace
{
  ezUInt32 s_uiCalls = 0;
  bool s_bInsideTool = false;
  bool s_bReentered = false;
} // namespace

// Test-only tools execute through the real server and editor wrapper, including a nested Qt loop.
class ezMcpDispatchTestTool : public ezMcpToolProvider
{
  EZ_ADD_DYNAMIC_REFLECTION(ezMcpDispatchTestTool, ezMcpToolProvider);

public:
  void GetSupportedTools(ezDynamicArray<ezMcpToolDesc>& out_tools) const override
  {
    for (const char* szName : {"test_dispatch_nested", "test_dispatch_close", "test_dispatch_error", "test_dispatch_cancel"})
    {
      auto& desc = out_tools.ExpandAndGetRef();
      desc.m_sName = szName;
      desc.m_sDescription = "EditorTest-only dispatch regression probe.";
    }
  }

  void Execute(ezStringView sToolName, const ezVariantDictionary&, ezMcpToolResult& out_result) override
  {
    ++s_uiCalls;
    if (sToolName == "test_dispatch_error")
    {
      out_result.SetError("Expected test error");
      return;
    }
    if (sToolName == "test_dispatch_cancel")
    {
      // Close after returning an unfinished result: the transport still has a pending request,
      // but no handler is executing when shutdown cancels it.
      QTimer::singleShot(0, qApp, []()
        { ezToolsProject::CloseProject(); });
      out_result.m_bNotFinished = true;
      return;
    }
    if (s_bInsideTool)
    {
      s_bReentered = true;
      out_result.SetError("Reentrant dispatch");
      return;
    }

    s_bInsideTool = true;
    if (sToolName == "test_dispatch_close")
      ezToolsProject::CloseProject();

    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
    s_bInsideTool = false;
    out_result.m_sText = "completed";
  }
};

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezMcpDispatchTestTool, 1, ezRTTIDefaultAllocator<ezMcpDispatchTestTool>)
EZ_END_DYNAMIC_REFLECTED_TYPE;

class ezEditorTestMcpDispatch : public ezEditorTest
{
public:
  const char* GetTestName() const override { return "MCP Dispatch"; }
  void SetupSubTests() override
  {
    AddSubTest("No listener without explicit port", 0);
    AddSubTest("Nested events and project close", 1);
    AddSubTest("Save loaded runtime profile", 2);
  }

  ezTestAppRun RunSubTest(ezInt32 iIdentifier, ezUInt32) override
  {
    auto* pCommandLine = ezCommandLineUtils::GetGlobalInstance();
    auto savedArguments = pCommandLine->GetCommandLineArray();
    EZ_SCOPE_EXIT(pCommandLine->SetCommandLine(savedArguments));
    ezInt32 iPort = pCommandLine->GetIntOption("-editor-mcpport", 0);
    const char* defaultArguments[] = {"EditorTest"};
    pCommandLine->SetCommandLine(1, defaultArguments);
    if (iIdentifier != 0)
    {
      // Avoid taking the conventional port from a user's editor when no test port was supplied.
      if (iPort == 0)
      {
        QTcpServer reservation;
        if (!EZ_TEST_BOOL(reservation.listen(QHostAddress::LocalHost, 0)))
          return ezTestAppRun::Quit;
        iPort = reservation.serverPort();
      }
      pCommandLine->InjectCustomArgument("-editor-mcpport");
      ezStringBuilder port;
      port.SetFormat("{}", iPort);
      pCommandLine->InjectCustomArgument(port);
    }
    // The editor application's project-switch command launches another process after its first
    // project. Reuse this disposable project's loaded plugin set in the test host instead.
    const ezResult opened = m_sProjectPath.IsEmpty() ? CreateAndLoadProject("McpDispatch") : ezToolsProject::OpenProject(ezStringBuilder(m_sProjectPath, "/ezProject")).GetResult();
    if (!EZ_TEST_BOOL(opened.Succeeded()))
      return ezTestAppRun::Quit;
    EZ_SCOPE_EXIT(ezToolsProject::CloseProject());

    auto* pServer = ezMcpServer::GetInstance();
    if (iIdentifier == 0)
    {
      EZ_TEST_BOOL(pServer == nullptr || !pServer->IsRunning());
      ezToolsProject::CloseProject();
      return ezTestAppRun::Quit;
    }
    if (!EZ_TEST_BOOL(pServer != nullptr && pServer->IsRunning()))
      return ezTestAppRun::Quit;

    const ezUInt16 uiPort = pServer->GetPort();
    if (iIdentifier == 2)
    {
      TestRuntimeProfile(uiPort);
      return ezTestAppRun::Quit;
    }
    s_uiCalls = 0;
    s_bReentered = false;
    const QByteArray response = Request(uiPort, "test_dispatch_nested", true);
    EZ_TEST_BOOL(response.contains("completed"));
    EZ_TEST_INT(s_uiCalls, 1);
    EZ_TEST_BOOL(!s_bReentered);

    // Closing from the request must not destroy the transport's stack-backed request/response
    // while the handler is still using them. The connection may be dropped by shutdown.
    Request(uiPort, "test_dispatch_close");
    EZ_TEST_INT(s_uiCalls, 2);
    EZ_TEST_BOOL(!s_bReentered);
    EZ_TEST_BOOL(!pServer->IsRunning());
    ProcessEvents(5);

    // A subsequent project must start serving again, without a stale callback from the old one.
    // The editor application's public project-switch command launches a new editor process.
    // Reopen through ToolsFoundation here to exercise this host's ProjectOpened subscription.
    if (EZ_TEST_BOOL(ezToolsProject::OpenProject(ezStringBuilder(m_sProjectPath, "/ezProject")).Succeeded()))
    {
      pServer = ezMcpServer::GetInstance();
      if (EZ_TEST_BOOL(pServer != nullptr && pServer->IsRunning()))
      {
        EZ_TEST_BOOL(Request(pServer->GetPort(), "test_dispatch_nested").contains("completed"));
        EZ_TEST_INT(s_uiCalls, 3);
        EZ_TEST_BOOL(!s_bReentered);
        const QByteArray errorResponse = Request(pServer->GetPort(), "test_dispatch_error");
        EZ_TEST_BOOL(errorResponse.contains("Expected test error"));
        EZ_TEST_BOOL(QJsonDocument::fromJson(errorResponse).object().value("result").toObject().value("isError").toBool());
        Request(pServer->GetPort(), "test_dispatch_cancel");
        EZ_TEST_INT(s_uiCalls, 5);
        EZ_TEST_BOOL(!pServer->IsRunning());
        ProcessEvents(5);
      }
    }
    return ezTestAppRun::Quit;
  }

private:
  void TestRuntimeProfile(ezUInt16 uiPort)
  {
    auto* pCurator = ezAssetCurator::GetSingleton();
    const ezUInt32 uiActive = pCurator->GetActiveAssetProfileIndex();
    auto* pProfile = pCurator->CreateAssetProfile();
    pProfile->SetConfigName("McpSaveTest");
    pProfile->AddMissingConfigs();
    EZ_SCOPE_EXIT(if (pProfile != nullptr) pCurator->DeleteAssetProfile(pProfile).IgnoreResult());
    auto* pConfig = pProfile->GetTypeConfig<ezRenderPipelineProfileConfig>();
    if (!EZ_TEST_BOOL(pConfig != nullptr))
      return;
    pConfig->m_sMainRenderPipeline = "{ 0b404d2d-2438-4536-ae48-87e1b19b75d3 }";

    const QString sProject = QString::fromUtf8(m_sProjectPath.GetData());
    auto ReadFile = [](const QString& sPath)
    {
      QFile file(sPath);
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QString sDdl = sProject + "/Editor/AssetProfiles.ddl";
    const QString sDefault = sProject + "/RuntimeConfigs/Default.ezProfile";
    const QByteArray ddlBefore = ReadFile(sDdl);
    const QByteArray defaultBefore = ReadFile(sDefault);
    const QString sOutput = sProject + "/RuntimeConfigs/McpSaveTest.ezProfile";

    auto Call = [&](const QJsonObject& arguments)
    {
      return QJsonDocument::fromJson(Request(uiPort, "project_save_runtime_profile", false, arguments))
        .object()
        .value("result")
        .toObject();
    };
    for (const QJsonObject& arguments : {QJsonObject{}, QJsonObject{{"profile", 42}},
           QJsonObject{{"profile", ""}}, QJsonObject{{"profile", "UnknownProfile"}}})
    {
      const auto response = Call(arguments);
      EZ_TEST_BOOL(response.value("isError").toBool());
    }
    EZ_TEST_BOOL(!QFile::exists(sOutput));

    for (const char* szName : {"../Escape", "sub/name", "sub\\name", ":project/Escape", ".", ".."})
    {
      pProfile->SetConfigName(szName);
      EZ_TEST_BOOL(Call(QJsonObject{{"profile", szName}}).value("isError").toBool());
    }
    pProfile->SetConfigName("McpSaveTest");

    QByteArray firstOutput;
    for (ezUInt32 i = 0; i < 2; ++i)
    {
      const auto response = Call(QJsonObject{{"profile", "mcpsavetest"}});
      EZ_TEST_BOOL(!response.isEmpty() && !response.value("isError").toBool());
      EZ_TEST_BOOL(QJsonDocument(response).toJson().contains("RuntimeConfigs/McpSaveTest.ezProfile"));
      const QByteArray output = ReadFile(sOutput);
      EZ_TEST_BOOL(!output.isEmpty());
      if (i == 0)
        firstOutput = output;
      else
        EZ_TEST_BOOL(firstOutput == output);
      ezPlatformProfile loaded;
      loaded.AddMissingConfigs();
      EZ_TEST_BOOL(loaded.LoadForRuntime(":project/RuntimeConfigs/McpSaveTest.ezProfile").Succeeded());
      const auto* pLoaded = loaded.GetTypeConfig<ezRenderPipelineProfileConfig>();
      if (EZ_TEST_BOOL(pLoaded != nullptr))
      {
        EZ_TEST_STRING(pLoaded->m_sMainRenderPipeline, pConfig->m_sMainRenderPipeline);
      }
    }

    // A directory occupying the exact output path is a portable open failure; repair and retry.
    EZ_TEST_BOOL(QFile::remove(sOutput));
    EZ_TEST_BOOL(QDir().mkdir(sOutput));
    EZ_TEST_BOOL(Call(QJsonObject{{"profile", "McpSaveTest"}}).value("isError").toBool());
    EZ_TEST_BOOL(QDir().rmdir(sOutput));
    EZ_TEST_BOOL(!Call(QJsonObject{{"profile", "McpSaveTest"}}).value("isError").toBool());
    EZ_TEST_BOOL(ReadFile(sOutput) == firstOutput);
    EZ_TEST_BOOL(ReadFile(sDdl) == ddlBefore);
    EZ_TEST_BOOL(ReadFile(sDefault) == defaultBefore);
    EZ_TEST_INT(pCurator->GetActiveAssetProfileIndex(), uiActive);

    // Closing the project also stops its listener. Exercise the provider's closed-project guard
    // directly, before creating any new host/project state.
    EZ_TEST_BOOL(pCurator->DeleteAssetProfile(pProfile).Succeeded());
    pProfile = nullptr;
    ezToolsProject::CloseProject();
    const ezRTTI* pType = ezRTTI::FindTypeByName("ezMcpProjectTool");
    if (!EZ_TEST_BOOL(pType != nullptr))
      return;
    ezMcpToolProvider* pProvider = pType->GetAllocator()->Allocate<ezMcpToolProvider>();
    EZ_SCOPE_EXIT(pType->GetAllocator()->Deallocate(pProvider));
    ezMcpToolResult response;
    ezVariantDictionary arguments;
    arguments.Insert("profile", "Default");
    pProvider->Execute("project_save_runtime_profile", arguments, response);
    EZ_TEST_BOOL(response.m_bIsError);
  }

  QByteArray Request(ezUInt16 uiPort, const char* szTool, bool bDuringProgress = false, const QJsonObject& arguments = {})
  {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/mcp").arg(uiPort)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject params{{"name", szTool}, {"arguments", arguments}};
    QJsonObject message{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", params}};
    QNetworkReply* pReply = manager.post(request, QJsonDocument(message).toJson());
    if (bDuringProgress)
    {
      const ezUInt32 uiCallsBefore = s_uiCalls;
      ezProgressRange progress("MCP dispatch exclusion", 2, false);
      QEventLoop nestedLoop;
      QTimer::singleShot(1500, &nestedLoop, &QEventLoop::quit);
      nestedLoop.exec();
      EZ_TEST_BOOL(ezMcpServer::GetInstance()->HasPendingRequest());
      EZ_TEST_INT(s_uiCalls, uiCallsBefore);
      EZ_TEST_BOOL(!pReply->isFinished());
    }
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(pReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    EZ_TEST_BOOL_MSG(pReply->isFinished(), "MCP request timed out");
    const QByteArray body = pReply->isFinished() ? pReply->readAll() : QByteArray();
    if (!pReply->isFinished())
      pReply->abort();
    delete pReply;
    return body;
  }
};

static ezEditorTestMcpDispatch s_McpDispatch;
