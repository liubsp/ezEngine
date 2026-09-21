#include <GameEngineTest/GameEngineTestPCH.h>

#ifdef BUILDSYSTEM_ENABLE_IMGUI_SUPPORT
#  include <Core/Input/InputManager.h>
#  include <GameEngine/Console/ImGuiConsole.h>
#  include <GameEngine/DearImgui/DearImgui.h>
#  include <GameEngine/DearImgui/DearImguiRenderer.h>
#  include <GameEngineTest/TestClass/TestClass.h>
#  include <Imgui/imgui_internal.h>
#  include <RendererCore/Pipeline/ExtractedRenderData.h>
#  include <RendererCore/Pipeline/RenderDataManager.h>
#  include <RendererCore/Pipeline/View.h>
#  include <RendererCore/RenderWorld/RenderWorld.h>
#  include <future>
#  include <thread>

class ezImguiLifetimeTest : public ezGameEngineTest
{
public:
  const char* GetTestName() const override { return "ImGui Lifetime"; }
  ezGameEngineTestApplication* CreateApplication() override { return EZ_DEFAULT_NEW(ezGameEngineTestApplication, "Basics"); }
  void SetupSubTests() override { AddSubTest("View and thread-local lifetimes", 0); }

  ezTestAppRun RunSubTest(ezInt32, ezUInt32) override
  {
    ezView* pFirst = nullptr;
    const auto first = ezRenderWorld::CreateView("ImGui lifetime first", pFirst);
    pFirst->SetViewport(ezRectFloat(0, 0, 1280, 720));
    pFirst->SetWorld(m_pApplication->GetWorld());
    pFirst->SetCameraUsageHint(ezCameraUsageHint::MainView);
    {
      EZ_LOCK(m_pApplication->GetWorld()->GetWriteMarker());
      m_pApplication->GetWorld()->GetOrCreateModule<ezRenderDataManager>();
    }

    // The console is a shared owner: it can create the singleton before a game-state UI does.
    ezInputManager::PollHardware();
    EZ_TEST_BOOL(ezImgui::GetSingleton() == nullptr);
    ezImGuiConsole console;
    console.RenderConsole(true);
    EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
    auto* pImgui = ezImgui::GetSingleton();
    if (!EZ_TEST_BOOL(pImgui != nullptr))
      return ezTestAppRun::Quit;
    for (auto it = pImgui->m_ViewToContextTable.GetIterator(); it.IsValid(); ++it)
      it.Value().m_pImGuiContext->IO.IniFilename = nullptr;
    const ezUInt32 uiOtherContexts = pImgui->m_ViewToContextTable.GetCount() - (pImgui->m_ViewToContextTable.Contains(first) ? 1 : 0);
    pImgui->SetPassInputToImgui(false);
    pImgui->SetCurrentContextForView(first);
    ImGui::GetIO().IniFilename = nullptr;
    auto* pContext = ImGui::GetCurrentContext();
    ImGui::TextUnformatted("Lifecycle probe");
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(40, 40), IM_COL32_WHITE);
    ImGui::SetCurrentContext(nullptr);
    ezGameApplicationExecutionEvent event;
    event.m_Type = ezGameApplicationExecutionEvent::Type::AfterUpdatePlugins;
    m_pApplication->m_ExecutionEvents.Broadcast(event);
    EZ_TEST_BOOL(!pContext->WithinFrameScope);
    EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
    m_pApplication->m_ExecutionEvents.Broadcast(event);

    ezImguiExtractor extractor;
    ezExtractedRenderData extracted;
    ezDynamicArray<const ezGameObject*> visible;
    extractor.Extract(*pFirst, visible, extracted);
    EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
    EZ_TEST_BOOL(!extracted.GetRawRenderDataWithCategory(ezDefaultRenderDataCategories::GUI).IsEmpty());
    EZ_TEST_INT(pImgui->m_ViewToContextTable[first].m_uiFrameRenderCounter, ezRenderWorld::GetFrameCounter());

    ezView* pSecond = nullptr;
    const auto second = ezRenderWorld::CreateView("ImGui lifetime survivor", pSecond);
    pSecond->SetViewport(ezRectFloat(0, 0, 1280, 720));
    pImgui->SetCurrentContextForView(second);
    ImGui::GetIO().IniFilename = nullptr;
    auto* pSurvivor = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nullptr);
    // An old, still-open frame must not be finalized as though this update produced it.
    pImgui->m_ViewToContextTable[second].m_uiFrameBeginCounter = ezRenderWorld::GetFrameCounter() - 1;
    m_pApplication->m_ExecutionEvents.Broadcast(event);
    EZ_TEST_BOOL(pSurvivor->WithinFrameScope);
    ImGui::SetCurrentContext(pSurvivor);
    extractor.Extract(*pSecond, visible, extracted);
    EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
    pImgui->m_ViewToContextTable[second].m_uiFrameBeginCounter = ezRenderWorld::GetFrameCounter();

    ezView* pWorkerView = nullptr;
    const auto workerView = ezRenderWorld::CreateView("ImGui worker binding", pWorkerView);
    pWorkerView->SetViewport(ezRectFloat(0, 0, 1280, 720));
    const auto* pAtlas = pImgui->m_pSharedFontAtlas.Borrow();
    const ezUInt32 uiTextures = pImgui->m_RegisteredTextures.GetCount();
    std::promise<void> bound, deleted;
    auto ready = bound.get_future();
    auto released = deleted.get_future();
    bool bWorkerCleared = false;
    auto workerBatch = [&]()
    {
      // Intentionally retain TLS after a finished batch, independently of normal caller cleanup.
      pImgui->SetCurrentContextForView(workerView);
      ImGui::GetIO().IniFilename = nullptr;
      ImGui::TextUnformatted("Worker UI batch");
      ImGui::EndFrame();
      bound.set_value();
      if (released.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
      {
        m_pApplication->m_ExecutionEvents.Broadcast(event);
        bWorkerCleared = ImGui::GetCurrentContext() == nullptr;
      }
      ImGui::SetCurrentContext(nullptr);
    };
    std::thread worker(workerBatch);
    const bool bReady = ready.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    EZ_TEST_BOOL(bReady);
    if (bReady)
    {
      ezRenderWorld::DeleteView(workerView);
      EZ_TEST_BOOL(!pImgui->m_ViewToContextTable.Contains(workerView));
    }
    deleted.set_value();
    worker.join();
    // On synchronization failure, join before cleanup rather than racing unfinished UI work.
    if (!bReady)
      ezRenderWorld::DeleteView(workerView);
    EZ_TEST_BOOL(bWorkerCleared);
    EZ_TEST_BOOL(!pSurvivor->WithinFrameScope);
    EZ_TEST_BOOL(pImgui->m_ViewToContextTable.Contains(second));
    EZ_TEST_BOOL(pImgui->m_pSharedFontAtlas.Borrow() == pAtlas);
    EZ_TEST_INT(pImgui->m_RegisteredTextures.GetCount(), uiTextures);
    EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
    for (ezUInt32 i = 0; i < 10; ++i)
    {
      ezView* pReplacement = nullptr;
      const auto replacement = ezRenderWorld::CreateView("ImGui replacement beside survivor", pReplacement);
      pReplacement->SetViewport(ezRectFloat(0, 0, 1280, 720));
      pImgui->SetCurrentContextForView(replacement);
      ImGui::GetIO().IniFilename = nullptr;
      ImGui::TextUnformatted("Shared atlas after deletion");
      ImGui::SetCurrentContext(nullptr);
      ezRenderWorld::DeleteView(replacement);
      EZ_TEST_INT(pImgui->m_ViewToContextTable.GetCount(), uiOtherContexts + 2);
      EZ_TEST_BOOL(pImgui->m_ViewToContextTable[second].m_pImGuiContext == pSurvivor);
      EZ_TEST_BOOL(pImgui->m_pSharedFontAtlas.Borrow() == pAtlas);
      EZ_TEST_INT(pImgui->m_RegisteredTextures.GetCount(), uiTextures);
    }
    ezRenderWorld::DeleteView(first);
    ezRenderWorld::DeleteView(first);
    ezRenderWorld::DeleteView(second);
    EZ_TEST_INT(pImgui->m_ViewToContextTable.GetCount(), uiOtherContexts);
    EZ_DEFAULT_DELETE(pImgui);

    // Repeated view-first and singleton-first teardown, including absent contexts and invalid views.
    for (ezUInt32 i = 0; i < 10; ++i)
    {
      pImgui = EZ_DEFAULT_NEW(ezImgui);
      pImgui->SetPassInputToImgui(false);
      ezView* pView = nullptr;
      const auto absent = ezRenderWorld::CreateView("ImGui unused view", pView);
      extractor.Extract(*pView, visible, extracted);
      EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
      ezRenderWorld::DeleteView(absent);
      EZ_TEST_BOOL(pImgui->m_ViewToContextTable.IsEmpty());
      const auto view = ezRenderWorld::CreateView("ImGui recreated view", pView);
      pView->SetViewport(ezRectFloat(0, 0, 1280, 720));
      pImgui->SetCurrentContextForView(view);
      ImGui::GetIO().IniFilename = nullptr;
      ImGui::TextUnformatted("Recreated context");
      ImGui::SetCurrentContext(nullptr);
      // Teardown also owns an unrendered open frame after its UI batch has finished.
      if (i < 5)
        m_pApplication->m_ExecutionEvents.Broadcast(event);
      if ((i & 1) == 0)
      {
        ezRenderWorld::DeleteView(view);
        EZ_TEST_BOOL(pImgui->m_ViewToContextTable.IsEmpty());
        EZ_TEST_BOOL(!pImgui->m_pSharedFontAtlas->Locked);
        pImgui->SetCurrentContextForView(view);
        EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
        EZ_TEST_BOOL(pImgui->m_ViewToContextTable.IsEmpty());
        EZ_DEFAULT_DELETE(pImgui);
      }
      else
      {
        EZ_DEFAULT_DELETE(pImgui);
        ezRenderWorld::DeleteView(view);
      }
      EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
      EZ_TEST_BOOL(ezImgui::GetSingleton() == nullptr);
      ezRenderWorld::DeleteView(view);
      const auto unused = ezRenderWorld::CreateView("ImGui absent context", pView);
      extractor.Extract(*pView, visible, extracted);
      EZ_TEST_BOOL(ImGui::GetCurrentContext() == nullptr);
      ezRenderWorld::DeleteView(unused);
    }
    return ezTestAppRun::Quit;
  }
};
static ezImguiLifetimeTest s_ImguiLifetimeTest;
#endif
