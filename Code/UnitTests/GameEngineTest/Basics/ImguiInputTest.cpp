#include <GameEngineTest/GameEngineTestPCH.h>

#ifdef BUILDSYSTEM_ENABLE_IMGUI_SUPPORT
#  define IMGUI_DEFINE_MATH_OPERATORS
#  include <Core/Input/InputDevice.h>
#  include <Core/Input/InputManager.h>
#  include <GameEngine/DearImgui/DearImgui.h>
#  include <GameEngineTest/TestClass/TestClass.h>
#  include <Imgui/imgui.h>
#  include <Imgui/imgui_internal.h>
#  include <RendererCore/Pipeline/View.h>
#  include <RendererCore/RenderWorld/RenderWorld.h>

namespace
{
  struct InputContext
  {
    ImGuiContext* m_pContext = ImGui::CreateContext();

    InputContext()
    {
      auto& io = m_pContext->IO;
      io.IniFilename = nullptr;
      io.DisplaySize = ImVec2(640, 480);
      io.DeltaTime = 1.0f / 60.0f;
      io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
      unsigned char* pPixels;
      int width, height;
      io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);
    }

    ~InputContext()
    {
      ImGui::SetCurrentContext(nullptr);
      ImGui::DestroyContext(m_pContext);
    }

    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;

    ImVector<ImWchar> Frame()
    {
      ImGui::SetCurrentContext(m_pContext);
      ImGui::NewFrame();
      // EndFrame clears text input. Observe delivery before that lifecycle boundary.
      const auto characters = m_pContext->IO.InputQueueCharacters;
      ImGui::EndFrame();
      return characters;
    }
  };

  void QueueNonMouse(ImGuiIO& inout_io)
  {
    inout_io.AddKeyEvent(ImGuiMod_Ctrl, true);
    inout_io.AddKeyEvent(ImGuiKey_A, true);
    inout_io.AddInputCharacter('x');
    inout_io.AddKeyEvent(ImGuiKey_A, false);
    inout_io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, true, .75f);
    inout_io.AddInputCharacter('y');
    inout_io.AddFocusEvent(true);
  }

  struct Widgets
  {
    float m_fValue = .5f;
    char m_Text[32] = "";
    ImVec2 m_SliderPos, m_TextPos, m_SourcePos, m_TargetPos;
    bool m_bDelivered = false;

    void Frame(InputContext& inout_context, bool bTransition = false)
    {
      ImGui::SetCurrentContext(inout_context.m_pContext);
      ImGui::NewFrame();
      if (bTransition)
      {
        ImGui::SetWindowFocus(nullptr);
        ImGui::SetWindowFocus("Input widgets");
      }
      ImGui::BeginDisabled(bTransition);
      ImGui::SetNextWindowPos(ImVec2(10, 10));
      ImGui::SetNextWindowSize(ImVec2(350, 350));
      ImGui::Begin("Input widgets", nullptr, ImGuiWindowFlags_NoSavedSettings | (bTransition ? ImGuiWindowFlags_NoMouseInputs : 0));
      ImGui::SliderFloat("Value", &m_fValue, 0, 1);
      m_SliderPos = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
      ImGui::InputText("Text", m_Text, sizeof(m_Text));
      m_TextPos = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
      ImGui::Button("Source");
      m_SourcePos = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
      if (ImGui::BeginDragDropSource())
      {
        const int payload = 42;
        ImGui::SetDragDropPayload("InputTest", &payload, sizeof(payload));
        ImGui::TextUnformatted("Payload");
        ImGui::EndDragDropSource();
      }
      ImGui::Button("Target");
      m_TargetPos = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
      if (ImGui::BeginDragDropTarget())
      {
        if (const auto* pPayload = ImGui::AcceptDragDropPayload("InputTest"))
          m_bDelivered |= pPayload->IsDelivery();
        ImGui::EndDragDropTarget();
      }
      ImGui::End();
      ImGui::EndDisabled();
      ImGui::EndFrame();
    }
  };
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(ImGui);
EZ_CREATE_SIMPLE_TEST(ImGui, ImGuiInput)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Selective queue cancellation and key repeat")
  {
    InputContext filtered;
    InputContext control;
    for (auto* pContext : {filtered.m_pContext, control.m_pContext})
      pContext->IO.AddKeyEvent(ImGuiKey_DownArrow, true);
    filtered.Frame();
    control.Frame();
    auto& io = filtered.m_pContext->IO;
    io.AddMousePosEvent(50, 50);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(60, 60);
    QueueNonMouse(io);
    io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
    io.AddMouseWheelEvent(1, -1);
    io.AddMouseButtonEvent(0, false);
    filtered.Frame();
    control.Frame();
    EZ_TEST_BOOL(filtered.m_pContext->InputEventsQueue.Size > 0);
    QueueNonMouse(control.m_pContext->IO);
    const float duration = ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_DownArrow)->DownDuration;
    const auto expected = control.m_pContext->InputEventsQueue;
    const auto queuedBefore = filtered.m_pContext->InputEventsQueue;
    const auto nextEventId = filtered.m_pContext->InputEventsNextEventId;
    // Cancellation uses this IO's owner, not the currently bound control context.
    io.DiscardMouseInput();
    io.DiscardMouseInput();
    EZ_TEST_INT(filtered.m_pContext->InputEventsNextEventId, nextEventId);
    int kept = 0;
    for (const auto& event : queuedBefore)
    {
      if (event.Type == ImGuiInputEventType_Key || event.Type == ImGuiInputEventType_Text || event.Type == ImGuiInputEventType_Focus)
      {
        if (kept < filtered.m_pContext->InputEventsQueue.Size)
          EZ_TEST_INT(filtered.m_pContext->InputEventsQueue[kept].EventId, event.EventId);
        ++kept;
      }
    }
    EZ_TEST_INT(filtered.m_pContext->InputEventsQueue.Size, expected.Size);
    if (filtered.m_pContext->InputEventsQueue.Size == expected.Size)
    {
      for (int i = 0; i < expected.Size; ++i)
      {
        const auto& actual = filtered.m_pContext->InputEventsQueue[i];
        EZ_TEST_INT(actual.Type, expected[i].Type);
        // Event identifiers legitimately differ due to the discarded mouse events.
        if (actual.Type == ImGuiInputEventType_Key)
        {
          EZ_TEST_INT(actual.Key.Key, expected[i].Key.Key);
          EZ_TEST_BOOL(actual.Key.Down == expected[i].Key.Down);
          EZ_TEST_FLOAT(actual.Key.AnalogValue, expected[i].Key.AnalogValue, 0);
        }
        else if (actual.Type == ImGuiInputEventType_Text)
          EZ_TEST_INT(actual.Text.Char, expected[i].Text.Char);
        else if (actual.Type == ImGuiInputEventType_Focus)
          EZ_TEST_BOOL(actual.AppFocused.Focused == expected[i].AppFocused.Focused);
      }
    }
    EZ_TEST_FLOAT(ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_DownArrow)->DownDuration, duration, 0);
    int deliveredCharacters = 0;
    for (int frame = 0; frame < 8; ++frame)
    {
      const auto filteredCharacters = filtered.Frame();
      const auto controlCharacters = control.Frame();
      EZ_TEST_BOOL(io.KeyCtrl == control.m_pContext->IO.KeyCtrl);
      EZ_TEST_BOOL(ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_A)->Down == ImGui::GetKeyData(control.m_pContext, ImGuiKey_A)->Down);
      EZ_TEST_FLOAT(ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_DownArrow)->DownDuration,
        ImGui::GetKeyData(control.m_pContext, ImGuiKey_DownArrow)->DownDuration, 0);
      EZ_TEST_INT(filteredCharacters.Size, controlCharacters.Size);
      for (int i = 0; i < ezMath::Min(filteredCharacters.Size, controlCharacters.Size); ++i)
        EZ_TEST_INT(filteredCharacters[i], controlCharacters[i]);
      deliveredCharacters += filteredCharacters.Size;
      EZ_TEST_BOOL(!io.MouseDown[0] && !io.MouseClicked[0] && !io.MouseReleased[0]);
      EZ_TEST_FLOAT(io.MouseWheel, 0, 0);
    }
    EZ_TEST_INT(filtered.m_pContext->InputEventsQueue.Size, 0);
    EZ_TEST_INT(deliveredCharacters, 2);
    EZ_TEST_FLOAT(ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_GamepadL2)->AnalogValue, .75f, 0);
    io.AddMousePosEvent(50, 50);
    io.AddMouseButtonEvent(0, true);
    filtered.Frame();
    EZ_TEST_BOOL(io.MouseClicked[0] && !io.MouseDoubleClicked[0]);
    io.AddFocusEvent(false);
    io.AddMouseWheelEvent(1, 1);
    io.DiscardMouseInput();
    filtered.Frame();
    EZ_TEST_BOOL(!io.KeyCtrl && !ImGui::GetKeyData(filtered.m_pContext, ImGuiKey_DownArrow)->Down);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Held mouse, click history and pending trickled input")
  {
    InputContext context;
    auto& io = context.m_pContext->IO;
    io.AddMousePosEvent(50, 50);
    io.AddMouseButtonEvent(0, true);
    context.Frame();
    io.AddMousePosEvent(100, 100);
    io.AddMouseButtonEvent(0, false);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseWheelEvent(1, -1);
    context.Frame();
    EZ_TEST_BOOL(context.m_pContext->InputEventsQueue.Size > 0);
    io.DiscardMouseInput();
    EZ_TEST_BOOL(!ImGui::IsMousePosValid(&io.MousePos));
    EZ_TEST_BOOL(!ImGui::IsMousePosValid(&io.MousePosPrev));
    for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
    {
      EZ_TEST_BOOL(!io.MouseDown[button] && !io.MouseClicked[button] && !io.MouseReleased[button] && !io.MouseDoubleClicked[button]);
      EZ_TEST_FLOAT(io.MouseDragMaxDistanceSqr[button], 0, 0);
      EZ_TEST_INT(io.MouseClickedLastCount[button], 0);
    }
    for (int key = ImGuiKey_Mouse_BEGIN; key < ImGuiKey_Mouse_END; ++key)
    {
      const auto* pKey = ImGui::GetKeyData(context.m_pContext, static_cast<ImGuiKey>(key));
      EZ_TEST_BOOL(!pKey->Down);
      EZ_TEST_FLOAT(pKey->DownDuration, -1, 0);
    }
    context.Frame();
    EZ_TEST_BOOL(!io.MouseReleased[0]);
    EZ_TEST_INT(context.m_pContext->InputEventsQueue.Size, 0);
    io.AddMousePosEvent(100, 100);
    io.AddMouseButtonEvent(0, true);
    context.Frame();
    EZ_TEST_BOOL(io.MouseClicked[0] && !io.MouseDoubleClicked[0]);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Mouse-focused text and active slider cancellation")
  {
    InputContext context;
    Widgets widgets;
    auto& io = context.m_pContext->IO;
    widgets.Frame(context);
    widgets.Frame(context);
    io.AddMousePosEvent(widgets.m_TextPos.x, widgets.m_TextPos.y);
    io.AddMouseButtonEvent(0, true);
    widgets.Frame(context);
    EZ_TEST_BOOL(context.m_pContext->InputTextState.ID != 0);
    const auto textId = context.m_pContext->ActiveId;
    io.AddMouseButtonEvent(0, false);
    io.AddInputCharactersUTF8("keep");
    io.DiscardMouseInput();
    EZ_TEST_INT(context.m_pContext->ActiveId, textId);
    widgets.Frame(context);
    EZ_TEST_STRING(widgets.m_Text, "keep");
    EZ_TEST_INT(context.m_pContext->ActiveId, textId);
    io.AddMousePosEvent(widgets.m_SliderPos.x, widgets.m_SliderPos.y);
    io.AddMouseButtonEvent(0, true);
    widgets.Frame(context);
    EZ_TEST_BOOL(context.m_pContext->ActiveId != 0 && context.m_pContext->ActiveId != textId);
    const float value = widgets.m_fValue;
    io.AddMousePosEvent(600, 100);
    io.AddMouseButtonEvent(0, false);
    io.DiscardMouseInput();
    widgets.Frame(context);
    EZ_TEST_FLOAT(widgets.m_fValue, value, 0);
    EZ_TEST_INT(context.m_pContext->ActiveId, 0);
    EZ_TEST_BOOL(!io.MouseReleased[0]);
    io.AddMousePosEvent(widgets.m_SliderPos.x, widgets.m_SliderPos.y);
    io.AddMouseButtonEvent(0, true);
    widgets.Frame(context);
    EZ_TEST_BOOL(context.m_pContext->ActiveId != 0);
    const float transitionValue = widgets.m_fValue;
    io.AddMousePosEvent(600, 100);
    widgets.Frame(context, true);
    EZ_TEST_FLOAT(widgets.m_fValue, transitionValue, 0);
    EZ_TEST_INT(context.m_pContext->ActiveId, 0);
    EZ_TEST_BOOL(context.m_pContext->NavWindow != nullptr);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Mouse drag/drop cancellation does not deliver a drop")
  {
    InputContext context;
    Widgets widgets;
    auto& io = context.m_pContext->IO;
    widgets.Frame(context);
    widgets.Frame(context);
    io.AddMousePosEvent(widgets.m_SourcePos.x, widgets.m_SourcePos.y);
    io.AddMouseButtonEvent(0, true);
    widgets.Frame(context);
    io.AddMousePosEvent(widgets.m_TargetPos.x, widgets.m_TargetPos.y);
    widgets.Frame(context);
    widgets.Frame(context);
    EZ_TEST_BOOL(context.m_pContext->DragDropActive);
    EZ_TEST_BOOL(context.m_pContext->DragDropAcceptIdPrev != 0);
    io.DiscardMouseInput();
    EZ_TEST_BOOL(!context.m_pContext->DragDropActive);
    widgets.Frame(context);
    EZ_TEST_BOOL(!widgets.m_bDelivered);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Deferred diagonal wheel and window drag")
  {
    InputContext context;
    auto& io = context.m_pContext->IO;
    ImGuiWindow* pParent = nullptr;
    ImGuiWindow* pChild = nullptr;
    auto draw = [&]()
    {
      ImGui::SetCurrentContext(context.m_pContext);
      ImGui::NewFrame();
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
      ImGui::SetNextWindowSize(ImVec2(350, 350));
      ImGui::SetNextWindowContentSize(ImVec2(800, 0));
      ImGui::Begin("Wheel parent", nullptr, ImGuiWindowFlags_HorizontalScrollbar);
      pParent = context.m_pContext->CurrentWindow;
      ImGui::BeginChild("Vertical child", ImVec2(150, 150));
      pChild = context.m_pContext->CurrentWindow;
      ImGui::Dummy(ImVec2(50, 800));
      ImGui::EndChild();
      ImGui::End();
      ImGui::EndFrame();
    };
    draw();
    draw();
    EZ_TEST_BOOL(pParent->ScrollMax.x > 0 && pChild->ScrollMax.y > 0 && pChild->ScrollMax.x == 0);
    io.AddMousePosEvent(80, 80);
    draw();
    io.AddMouseWheelEvent(-1, -1);
    draw();
    EZ_TEST_BOOL(context.m_pContext->WheelingWindowWheelRemainder.x != 0);
    const auto parentScroll = pParent->Scroll;
    const auto childScroll = pChild->Scroll;
    io.DiscardMouseInput();
    draw();
    EZ_TEST_FLOAT(pParent->Scroll.x, parentScroll.x, 0);
    EZ_TEST_FLOAT(pChild->Scroll.y, childScroll.y, 0);
    EZ_TEST_FLOAT(context.m_pContext->WheelingWindowWheelRemainder.x, 0, 0);
    EZ_TEST_BOOL(context.m_pContext->WheelingWindow == nullptr);
    io.AddMousePosEvent(100, 20);
    io.AddMouseButtonEvent(0, true);
    draw();
    EZ_TEST_BOOL(context.m_pContext->MovingWindow == pParent);
    const auto position = pParent->Pos;
    io.AddMousePosEvent(300, 200);
    io.DiscardMouseInput();
    draw();
    EZ_TEST_BOOL(context.m_pContext->MovingWindow == nullptr);
    EZ_TEST_FLOAT(pParent->Pos.x, position.x, 0);
    EZ_TEST_FLOAT(pParent->Pos.y, position.y, 0);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Keyboard-opened combo rejects discarded pointer input")
  {
    InputContext context;
    auto& io = context.m_pContext->IO;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    int selection = 0;
    ImVec2 second(0, 0);
    bool open = false;
    auto draw = [&](bool bFocus = false)
    {
      ImGui::SetCurrentContext(context.m_pContext);
      ImGui::NewFrame();
      ImGui::SetNextWindowPos(ImVec2(10, 10));
      ImGui::SetNextWindowSize(ImVec2(350, 350));
      ImGui::Begin("Combo input");
      if (bFocus)
        ImGui::SetWindowFocus();
      open = ImGui::BeginCombo("Choice", selection == 0 ? "First" : "Second");
      if (open)
      {
        if (ImGui::Selectable("First", selection == 0))
          selection = 0;
        if (ImGui::Selectable("Second", selection == 1))
          selection = 1;
        second = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
        ImGui::EndCombo();
      }
      ImGui::End();
      ImGui::EndFrame();
    };
    draw(true);
    draw();
    io.AddKeyEvent(ImGuiKey_Tab, true);
    draw();
    io.AddKeyEvent(ImGuiKey_Tab, false);
    draw();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    draw();
    EZ_TEST_BOOL(open);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    draw();
    io.AddMousePosEvent(second.x, second.y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
    io.DiscardMouseInput();
    draw();
    draw();
    EZ_TEST_INT(selection, 0);
    EZ_TEST_BOOL(open);
    io.AddMousePosEvent(second.x, second.y);
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    EZ_TEST_INT(selection, 1);
  }
}

class ezImguiInputBackendTest : public ezGameEngineTest
{
public:
  const char* GetTestName() const override { return "ImGui Input Backend"; }
  ezGameEngineTestApplication* CreateApplication() override { return EZ_DEFAULT_NEW(ezGameEngineTestApplication, "Basics"); }
  void SetupSubTests() override
  {
    AddSubTest("First-bind policy and modifier transitions", 0);
    AddSubTest("Short chords with pointer trickling", 1);
  }

  ezTestAppRun RunSubTest(ezInt32 iIdentifier, ezUInt32) override
  {
    if (iIdentifier == 1)
      return RunChordTest();
    auto* pImgui = ezImgui::GetSingleton();
    if (!pImgui)
      pImgui = EZ_DEFAULT_NEW(ezImgui);
    ezView* pView;
    const auto view = ezRenderWorld::CreateView("Input first view", pView);
    pView->SetViewport(ezRectFloat(0, 0, 640, 480));
    ezView* pOther;
    const auto other = ezRenderWorld::CreateView("Input second view", pOther);
    pOther->SetViewport(ezRectFloat(0, 0, 640, 480));
    auto begin = [&](bool bMouse, bool bAllInput = true)
    {
      ImGui::SetCurrentContext(nullptr);
      m_pApplication->Run();
      ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
      pImgui->SetPassInputToImgui(bAllInput);
      pImgui->SetCurrentContextForView(view, bMouse);
      ImGui::GetIO().IniFilename = nullptr;
    };
    begin(false);
    auto& io = ImGui::GetIO();
    EZ_TEST_BOOL((io.ConfigFlags & ImGuiConfigFlags_NoMouse) != 0);
    EZ_TEST_BOOL(!ImGui::IsMousePosValid(&io.MousePos));
    const int frame = ImGui::GetFrameCount();
    pImgui->SetCurrentContextForView(view);
    EZ_TEST_INT(ImGui::GetFrameCount(), frame);
    EZ_TEST_BOOL((io.ConfigFlags & ImGuiConfigFlags_NoMouse) != 0);
    ImGui::EndFrame();
    pImgui->SetCurrentContextForView(other);
    ImGui::GetIO().IniFilename = nullptr;
    EZ_TEST_BOOL((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0);
    ImGui::EndFrame();
    pImgui->SetCurrentContextForView(view);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseWheelEvent(0, 1);
    begin(false);
    EZ_TEST_BOOL(!io.MouseDown[0] && !io.MouseClicked[0] && !io.MouseReleased[0]);
    EZ_TEST_FLOAT(io.MouseWheel, 0, 0);
    ImGui::EndFrame();
    begin(true);
    EZ_TEST_BOOL((io.ConfigFlags & ImGuiConfigFlags_NoMouse) == 0);
    ImGui::EndFrame();

    struct Modifier
    {
      const char* m_szLeft;
      const char* m_szRight;
      ImGuiKey m_Key;
      ImGuiKey m_Left;
      ImGuiKey m_Right;
    };
    const Modifier modifiers[] = {
      {ezInputSlot_KeyLeftCtrl, ezInputSlot_KeyRightCtrl, ImGuiMod_Ctrl, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl},
      {ezInputSlot_KeyLeftShift, ezInputSlot_KeyRightShift, ImGuiMod_Shift, ImGuiKey_LeftShift, ImGuiKey_RightShift},
      {ezInputSlot_KeyLeftAlt, ezInputSlot_KeyRightAlt, ImGuiMod_Alt, ImGuiKey_LeftAlt, ImGuiKey_RightAlt},
      {ezInputSlot_KeyLeftWin, ezInputSlot_KeyRightWin, ImGuiMod_Super, ImGuiKey_LeftSuper, ImGuiKey_RightSuper}};
    for (const auto& modifier : modifiers)
    {
      for (int state : {1, 3, 2, 0})
      {
        // Advance the real render frame before injecting; consume the slots immediately before binding.
        ImGui::SetCurrentContext(nullptr);
        m_pApplication->Run();
        ezInputManager::InjectInputSlotValue(modifier.m_szLeft, (state & 1) ? 1.0f : 0.0f);
        ezInputManager::InjectInputSlotValue(modifier.m_szRight, (state & 2) ? 1.0f : 0.0f);
        ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
        pImgui->SetPassInputToImgui(true);
        pImgui->SetCurrentContextForView(view, false);
        EZ_TEST_BOOL(ImGui::IsKeyDown(modifier.m_Key) == (state != 0));
        EZ_TEST_BOOL(ImGui::IsKeyDown(modifier.m_Left) == ((state & 1) != 0));
        EZ_TEST_BOOL(ImGui::IsKeyDown(modifier.m_Right) == ((state & 2) != 0));
        ImGui::EndFrame();
      }
    }
    // A same-update Ctrl-click must observe Ctrl even if normal trickling defers the pointer event.
    bool clickedWithCtrl = false;
    for (int chordFrame = 0; chordFrame < 3; ++chordFrame)
    {
      ImGui::SetCurrentContext(nullptr);
      m_pApplication->Run();
      ezInputManager::InjectInputSlotValue(ezInputSlot_KeyLeftCtrl, chordFrame == 0 ? 1.0f : 0.0f);
      ezInputManager::InjectInputSlotValue(ezInputSlot_MouseButton0, chordFrame == 0 ? 1.0f : 0.0f);
      ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
      pImgui->SetPassInputToImgui(true);
      pImgui->SetCurrentContextForView(view);
      if (io.MouseClicked[0])
      {
        EZ_TEST_BOOL(io.KeyCtrl);
        clickedWithCtrl = io.KeyCtrl;
      }
      ImGui::EndFrame();
    }
    EZ_TEST_BOOL(clickedWithCtrl);
    // Both pending events and current held modifiers must be absent under all-input exclusion.
    ImGui::SetCurrentContext(nullptr);
    m_pApplication->Run();
    ezInputManager::InjectInputSlotValue(ezInputSlot_KeyLeftCtrl, 1);
    ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
    pImgui->SetPassInputToImgui(true);
    pImgui->SetCurrentContextForView(view, false);
    EZ_TEST_BOOL(io.KeyCtrl);
    io.AddKeyEvent(ImGuiKey_A, true);
    io.AddMouseButtonEvent(0, true);
    ImGui::EndFrame();
    begin(false, false);
    EZ_TEST_BOOL(!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper);
    EZ_TEST_BOOL(!ImGui::IsKeyDown(ImGuiKey_A));
    ImGui::EndFrame();
    ImGui::SetCurrentContext(nullptr);
    m_pApplication->Run();
    ezInputManager::InjectInputSlotValue(ezInputSlot_KeyLeftCtrl, 1);
    ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
    pImgui->SetPassInputToImgui(true);
    pImgui->SetCurrentContextForView(view, false);
    EZ_TEST_BOOL(io.KeyCtrl && ImGui::IsKeyDown(ImGuiKey_LeftCtrl));
    EZ_TEST_BOOL(!ImGui::IsKeyDown(ImGuiKey_A) && !io.MouseDown[0]);
    ImGui::EndFrame();
    ImGui::SetCurrentContext(nullptr);
    ezRenderWorld::DeleteView(view);
    ezRenderWorld::DeleteView(other);
    pImgui->SetPassInputToImgui(true);
    return ezTestAppRun::Quit;
  }

private:
  ezTestAppRun RunChordTest()
  {
    // Deliver text through the same device/manager path as platform character input.
    class TextDevice : public ezInputDevice
    {
    public:
      void Type(ezStringView sText) { m_sLastCharacters.Append(sText); }

    private:
      void InitializeDevice() override {}
      void RegisterInputSlots() override {}
      void UpdateInputSlotValues() override {}
    } textDevice;
    auto* pImgui = ezImgui::GetSingleton();
    if (!pImgui)
      pImgui = EZ_DEFAULT_NEW(ezImgui);
    ezView* pView;
    const auto view = ezRenderWorld::CreateView("Chord input", pView);
    pView->SetViewport(ezRectFloat(0, 0, 640, 480));
    ImGuiID buttons[3] = {};
    ImVec2 sliderPosition(0, 0);
    float value = .5f;
    char text[16] = "";
    bool focusText = false;
    auto frame = [&](float fPointerX, const char* szModifier = nullptr, const char* szKey = nullptr,
                   bool bClick = false, ezStringView sText = {})
    {
      ImGui::SetCurrentContext(nullptr);
      m_pApplication->Run();
      ezInputManager::InjectInputSlotValue(ezInputSlot_MousePositionX, (bClick ? sliderPosition.x : fPointerX) / 640.0f);
      ezInputManager::InjectInputSlotValue(ezInputSlot_MousePositionY, bClick ? sliderPosition.y / 480.0f : .8f);
      if (szModifier)
        ezInputManager::InjectInputSlotValue(szModifier, 1);
      if (szKey)
        ezInputManager::InjectInputSlotValue(szKey, 1);
      if (bClick)
      {
        ezInputManager::InjectInputSlotValue(ezInputSlot_MouseButton0, 1);
      }
      textDevice.Type(sText);
      ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
      pImgui->SetPassInputToImgui(true);
      pImgui->SetCurrentContextForView(view);
      ImGui::GetIO().IniFilename = nullptr;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      EZ_TEST_BOOL(ImGui::GetIO().ConfigInputTrickleEventQueue);
      ImGui::SetNextWindowPos(ImVec2(10, 10));
      ImGui::SetNextWindowSize(ImVec2(350, 350));
      ImGui::Begin("Chord widgets");
      ImGui::SetWindowFocus();
      for (int button = 0; button < 3; ++button)
      {
        const char* labels[] = {"First", "Second", "Third"};
        ImGui::Button(labels[button]);
        buttons[button] = ImGui::GetItemID();
      }
      ImGui::SliderFloat("Precise value", &value, 0, 1);
      sliderPosition = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * .5f;
      if (focusText)
        ImGui::SetKeyboardFocusHere();
      ImGui::InputText("Typed text", text, sizeof(text));
      ImGui::End();
      ImGui::EndFrame();
    };
    frame(500);
    frame(500);
    for (int button = 0; button < 2; ++button)
    {
      frame(500, nullptr, ezInputSlot_KeyTab);
      frame(500);
      frame(500);
      EZ_TEST_INT(ImGui::GetCurrentContext()->NavId, buttons[button]);
    }
    // One-update chord, changed pointer position, and release on the immediately following update.
    frame(501, ezInputSlot_KeyLeftShift, ezInputSlot_KeyTab);
    frame(501);
    frame(501);
    EZ_TEST_INT(ImGui::GetCurrentContext()->NavId, buttons[0]);
    // Forward Tab still works after release; reverse navigation also works without a pointer change.
    frame(501, nullptr, ezInputSlot_KeyTab);
    frame(501);
    frame(501);
    EZ_TEST_INT(ImGui::GetCurrentContext()->NavId, buttons[1]);
    frame(501, ezInputSlot_KeyRightShift, ezInputSlot_KeyTab);
    frame(501);
    frame(501);
    EZ_TEST_INT(ImGui::GetCurrentContext()->NavId, buttons[0]);

    // Changed position and a one-update Ctrl-click must open the real slider's text entry.
    frame(501, ezInputSlot_KeyLeftCtrl, nullptr, true);
    frame(501);
    frame(501);
    EZ_TEST_BOOL(ImGui::GetCurrentContext()->InputTextState.ID != 0);
    EZ_TEST_BOOL(ImGui::GetIO().WantTextInput);
    // Ctrl-A and pointer motion in one update must retain select-all semantics. Text follows
    // with the next update's key releases (Ctrl-held text is intentionally ignored by ImGui).
    // Enter commits without losing or duplicating the replacement text.
    frame(502, ezInputSlot_KeyLeftCtrl, ezInputSlot_KeyA);
    frame(502, nullptr, nullptr, false, "0.25");
    frame(502);
    frame(502, nullptr, ezInputSlot_KeyReturn);
    frame(502);
    frame(502);
    EZ_TEST_FLOAT(value, .25f, 0);
    EZ_TEST_BOOL(!ImGui::GetIO().KeyCtrl && !ImGui::IsKeyDown(ImGuiKey_A));
    focusText = true;
    frame(502);
    focusText = false;
    frame(502);
    // Ordinary key presses and their platform text can share an update; trickling must
    // retain character order and deliver each character once after the immediate key release.
    frame(503, nullptr, ezInputSlot_KeyA, false, "a");
    frame(503);
    frame(503);
    frame(504, nullptr, ezInputSlot_KeyC, false, "c");
    frame(504);
    frame(504);
    EZ_TEST_STRING(text, "ac");
    ImGui::SetCurrentContext(nullptr);
    ezRenderWorld::DeleteView(view);
    return ezTestAppRun::Quit;
  }
};
static ezImguiInputBackendTest s_ImguiInputBackendTest;
#endif
