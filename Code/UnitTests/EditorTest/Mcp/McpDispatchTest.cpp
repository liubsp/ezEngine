#include <EditorTest/EditorTestPCH.h>

#include <EditorTest/TestClass/TestClass.h>
#include <Foundation/Utilities/Progress.h>
#include <Mcp/McpServer.h>
#include <Mcp/McpTool.h>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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
  void SetupSubTests() override { AddSubTest("Nested events and project close", 0); }

  ezTestAppRun RunSubTest(ezInt32, ezUInt32) override
  {
    if (!EZ_TEST_BOOL(CreateAndLoadProject("McpDispatch").Succeeded()))
      return ezTestAppRun::Quit;

    auto* pServer = ezMcpServer::GetInstance();
    if (!EZ_TEST_BOOL(pServer != nullptr && pServer->IsRunning()))
      return ezTestAppRun::Quit;

    const ezUInt16 uiPort = pServer->GetPort();
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
  QByteArray Request(ezUInt16 uiPort, const char* szTool, bool bDuringProgress = false)
  {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/mcp").arg(uiPort)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject params{{"name", szTool}, {"arguments", QJsonObject{}}};
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
