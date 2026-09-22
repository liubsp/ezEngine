#include <EditorTest/EditorTestPCH.h>

#include <EditorFramework/Assets/AssetProcessor.h>
#include <EditorFramework/Preferences/EditorPreferences.h>
#include <EditorTest/TestClass/TestClass.h>
#include <TestFramework/Utilities/TestLogInterface.h>

class ezEditorTestAssetProcessor : public ezEditorTest
{
public:
  const char* GetTestName() const override { return "Asset Processor Shutdown"; }
  void SetupSubTests() override
  {
    AddSubTest("Intentional stop and unexpected loss", 0);
    AddSubTest("Owned child and project close", 1);
  }

  ezTestAppRun RunSubTest(ezInt32 iIdentifier, ezUInt32) override
  {
    if (iIdentifier == 1)
      return TestOwnedChild();
    auto* pProcessor = ezAssetProcessor::GetSingleton();
    auto* pCurator = ezAssetCurator::GetSingleton();
    EZ_TEST_BOOL(pProcessor->GetProcessorState() == ezAssetProcessor::ProcessorState::Stopped);
    ezThreadSignal signal;
    using State = ezEditorProcessorProcess::State;
    {
      ezEditorProcessorProcess process;
      process.m_pNewWorkSignal = &signal;
      EZ_TEST_BOOL(!process.Tick(false)); // A graceful stop before startup must not launch a child.
      process.ShutdownProcess();
      EZ_TEST_BOOL(!process.Tick(true));  // An intentional stop is terminal, even with new-work permission.
      EZ_TEST_BOOL(!process.HasProcessCrashed());
    }
    // No child is needed: the real IPC channel's absent process group represents observed loss.
    // Only the owner's stop request distinguishes this from the intentional-close path.
    for (bool bIntentional : {true, false})
    {
      for (State state : {State::WaitingForConnection, State::LookingForWork, State::ReadyForProcessing, State::Processing, State::ReportResult})
      {
        ezEditorProcessorProcess process;
        process.m_uiProcessorID = 0;
        process.m_pNewWorkSignal = &signal;
        process.m_State = state;
        process.m_bProcessShouldBeRunning = true;
        process.m_AssetGuid = ezUuid::MakeUuid();
        ezString root = ezTestFramework::GetInstance()->GetAbsOutputPath();
        process.m_AssetPath = ezDataDirPath(ezStringBuilder(root, "/ProcessorFixture.ezPrefab"), ezMakeArrayPtr(&root, 1));
        EZ_TEST_BOOL(process.m_AssetPath.IsValid());
        const bool bHasWork = state >= State::ReadyForProcessing;
        const bool bStarted = state == State::Processing || state == State::ReportResult;
        if (bHasWork)
          pCurator->m_Updating.Insert(process.m_AssetGuid);
        if (bStarted)
        {
          process.m_ProcessingStartTime = ezTime::Now();
          if (state == State::ReportResult)
          {
            process.m_StartedProcessing = process.m_ProcessingStartTime;
            process.m_StartedTransform = process.m_ProcessingStartTime;
            process.m_FinishedProcessing = ezTime::Now();
          }
          ezAssetProcessorProgressEvent started;
          started.m_Type = ezAssetProcessorProgressEvent::Type::ProcessingStarted;
          started.m_uiProcessorID = 0;
          started.m_AssetGuid = process.m_AssetGuid;
          started.m_StartTime = process.m_ProcessingStartTime;
          pProcessor->m_ProgressEvents.Broadcast(started);
        }

        ezUInt32 uiFinished = 0;
        auto handler = pProcessor->m_ProgressEvents.AddEventHandler([&](const ezAssetProcessorProgressEvent& e)
          {
            EZ_TEST_BOOL(e.m_Type == ezAssetProcessorProgressEvent::Type::ProcessingFinished);
            EZ_TEST_BOOL(e.m_AssetGuid == process.m_AssetGuid);
            if (state != State::ReportResult)
              EZ_TEST_BOOL(e.m_Result.Failed());
            else
              EZ_TEST_BOOL(e.m_Result.Succeeded());
            EZ_TEST_BOOL(e.m_EndTime >= e.m_StartTime);
            EZ_TEST_BOOL(!e.m_StartTime.IsZero());
            ++uiFinished; });
        EZ_SCOPE_EXIT(pProcessor->m_ProgressEvents.RemoveEventHandler(handler));
        m_uiCuratorErrors = 0;
        auto curatorLog = ezMakeDelegate(&ezEditorTestAssetProcessor::OnCuratorLog, this);
        pProcessor->AddLogWriter(curatorLog);
        EZ_SCOPE_EXIT(pProcessor->RemoveLogWriter(curatorLog));
        ezTestLogInterface log;
        ezTestLogSystemScope logScope(&log, false);
        if (!bIntentional)
          log.ExpectMessage("crashed. Right-click", ezLogMsgType::ErrorMsg, 1);
        if (bIntentional)
          process.ShutdownProcess();

        EZ_TEST_BOOL(!process.Tick(false));
        EZ_TEST_BOOL(process.IsCrashed() == !bIntentional);
        EZ_TEST_BOOL(process.HasProcessCrashed() == !bIntentional);
        EZ_TEST_BOOL(!pCurator->m_Updating.Contains(process.m_AssetGuid));
        EZ_TEST_INT(uiFinished, bStarted ? 1 : 0);
        EZ_TEST_BOOL(bIntentional ? m_uiCuratorErrors == 0 : m_uiCuratorErrors > 0);
        // Neither repeated stop nor tick may report the result or the crash a second time.
        process.ShutdownProcess();
        EZ_TEST_BOOL(!process.Tick(false));
        EZ_TEST_INT(uiFinished, bStarted ? 1 : 0);
      }
    }
    return ezTestAppRun::Quit;
  }

  ezTestAppRun TestOwnedChild()
  {
    if (!EZ_TEST_BOOL(CreateAndLoadProject("ProcessorShutdown").Succeeded()))
      return ezTestAppRun::Quit;
    auto* pProcessor = ezAssetProcessor::GetSingleton();
    pProcessor->StopProcessor(true);
    ezThreadSignal signal;
    {
      ezEditorProcessorProcess process;
      process.m_uiProcessorID = 0;
      process.m_pNewWorkSignal = &signal;
      if (!EZ_TEST_BOOL(process.StartProcess().Succeeded()))
        return ezTestAppRun::Quit;
      process.m_State = ezEditorProcessorProcess::State::WaitingForConnection;
      const ezTime deadline = ezTime::Now() + ezTime::MakeFromSeconds(20);
      while (!process.IsConnected() && ezTime::Now() < deadline)
      {
        ProcessEvents(1);
        ezThreadUtils::Sleep(ezTime::MakeFromMilliseconds(10));
      }
      if (!EZ_TEST_BOOL(process.IsConnected()))
        return ezTestAppRun::Quit;
      EZ_TEST_BOOL(!process.Tick(false));
      EZ_TEST_BOOL(!process.HasProcessCrashed());
      // Close only this test-owned group, without an owner stop request: the next tick must detect loss.
      process.m_pIPC->CloseConnection();
      ezTestLogInterface log;
      ezTestLogSystemScope logScope(&log, false);
      log.ExpectMessage("crashed. Right-click", ezLogMsgType::ErrorMsg, 1);
      EZ_TEST_BOOL(!process.Tick(false));
      EZ_TEST_BOOL(process.IsCrashed());
    }

    auto* pPreferences = ezPreferences::QueryPreferences<ezEditorPreferencesUser>();
    const ezUInt32 uiOldMax = pPreferences->m_uiMaxAssetProcessors;
    pPreferences->m_uiMaxAssetProcessors = 1;
    pProcessor->StartProcessor();
    pPreferences->m_uiMaxAssetProcessors = uiOldMax;
    const ezTime deadline = ezTime::Now() + ezTime::MakeFromSeconds(20);
    while (!pProcessor->GetProcessState(0).m_bConnected && ezTime::Now() < deadline)
    {
      ProcessEvents(1);
      ezThreadUtils::Sleep(ezTime::MakeFromMilliseconds(10));
    }
    EZ_TEST_INT(pProcessor->GetProcessCount(), 1);
    EZ_TEST_BOOL(pProcessor->GetProcessState(0).m_bConnected);
    // Ordinary project close owns StopProcessor(true), IPC teardown and the worker join.
    CloseCurrentProject();
    ProcessEvents(1); // CloseProject queues the normal editor close slot.
    EZ_TEST_BOOL(pProcessor->GetProcessorState() == ezAssetProcessor::ProcessorState::Stopped);
    EZ_TEST_INT(pProcessor->GetProcessCount(), 0);
    return ezTestAppRun::Quit;
  }

private:
  void OnCuratorLog(const ezLoggingEventData& e)
  {
    if (e.m_EventType == ezLogMsgType::ErrorMsg)
      ++m_uiCuratorErrors;
  }

  ezUInt32 m_uiCuratorErrors = 0;
};
static ezEditorTestAssetProcessor s_AssetProcessorTest;
