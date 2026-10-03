#include <TestFramework/Framework/TestFramework.h>

#ifdef BUILDSYSTEM_ENABLE_MCP_TESTS

#  include <Core/GameApplication/GameApplicationBase.h>
#  include <Core/Input/InputManager.h>
#  include <Foundation/Configuration/Plugin.h>
#  include <Foundation/IO/JSONReader.h>
#  include <Foundation/IO/MemoryStream.h>
#  include <Foundation/Threading/ThreadUtils.h>
#  include <Foundation/Utilities/CommandLineUtils.h>
#  include <Mcp/McpJson.h>
#  include <Mcp/McpServer.h>
#  include <Mcp/McpSocket.h>
#  include <Mcp/McpToolRegistry.h>
#  include <McpPlugin/McpEngineHost.h>
#  include <limits>

namespace
{
  constexpr ezStringView s_Button = "mcp_test_button";
  constexpr ezStringView s_OtherButton = "mcp_test_other_button";
  constexpr ezStringView s_InputSet = "mcp_test";

  class HardwareInput : public ezInputDevice
  {
  public:
    void Set(ezStringView sSlot, float fValue) { m_InputSlotValues[sSlot] = fValue; }
    void Clear() { m_InputSlotValues.Clear(); }

  private:
    void InitializeDevice() override {}
    void UpdateInputSlotValues() override {}
    void RegisterInputSlots() override
    {
      RegisterInputSlot(s_Button, s_Button, ezInputSlotFlags::IsButton);
      RegisterInputSlot(s_OtherButton, s_OtherButton, ezInputSlotFlags::IsButton);
      RegisterInputSlot(ezInputSlot_MouseMovePosX, "Mouse right", ezInputSlotFlags::IsMouseAxisMove);
      RegisterInputSlot(ezInputSlot_MouseMoveNegX, "Mouse left", ezInputSlotFlags::IsMouseAxisMove);
      RegisterInputSlot(ezInputSlot_MouseWheelUp, "Wheel up", ezInputSlotFlags::IsMouseWheel);
      RegisterInputSlot(ezInputSlot_MousePositionX, "Mouse X", ezInputSlotFlags::IsMouseAxisPosition);
    }
  };

  void UpdateInput()
  {
    ezInputManager::Update(ezTime::MakeFromSeconds(1.0 / 60.0));
  }

  ezMcpToolResult Call(ezStringView sName, const ezVariantDictionary& arguments)
  {
    ezMcpToolResult toolResult;
    EZ_TEST_BOOL(ezMcpToolRegistry::Execute(sName, arguments, toolResult).Succeeded());
    return toolResult;
  }

  ezVariantDictionary SetArguments(ezStringView sSlot, const ezVariant& value, const ezVariant& frames = 0.0)
  {
    ezVariantDictionary slots;
    slots.Insert(sSlot, value);
    ezVariantDictionary arguments;
    arguments.Insert("slots", slots);
    arguments.Insert("frames", frames);
    return arguments;
  }

  ezVariantDictionary Step(ezStringView sName, const ezVariant& value)
  {
    ezVariantDictionary step;
    step.Insert(sName, value);
    return step;
  }

  ezVariantDictionary SequenceArguments(const ezVariantArray& steps)
  {
    ezVariantDictionary arguments;
    arguments.Insert("steps", steps);
    arguments.Insert("timeout", 1.0);
    return arguments;
  }

  ezVariantDictionary ButtonSet()
  {
    ezVariantDictionary slots;
    slots.Insert(s_Button, 1.0);
    return slots;
  }

  void ClearAll()
  {
    EZ_TEST_BOOL(!Call("input_set", Step("clearAll", true)).m_bIsError);
  }

  void CheckSlot(ezStringView sSlot, ezKeyState::Enum state, float fExpectedValue)
  {
    float fValue = -1.0f;
    EZ_TEST_INT(ezInputManager::GetInputSlotState(sSlot, &fValue), state);
    EZ_TEST_FLOAT(fValue, fExpectedValue, 0.0f);
  }

  float SlotValue(ezStringView sSlot)
  {
    float fValue = -1.0f;
    ezInputManager::GetInputSlotState(sSlot, &fValue);
    return fValue;
  }

  ezVariantDictionary ReadResult(const ezMcpToolResult& toolResult)
  {
    ezRawMemoryStreamReader stream(toolResult.m_sText.GetData(), toolResult.m_sText.GetElementCount());
    ezJSONReader reader;
    EZ_TEST_BOOL(reader.Parse(stream).Succeeded());
    return reader.GetTopLevelObject();
  }

  void CheckHolding(ezStringView sSlot, ezUInt32 uiRemaining)
  {
    const auto response = ReadResult(Call("input_set", Step("slots", ezVariantDictionary())));
    const auto* pHolding = ezMcpJson::GetArray(response, "holding");
    EZ_TEST_BOOL(pHolding != nullptr);
    if (pHolding == nullptr)
      return;
    for (const auto& held : *pHolding)
    {
      const auto& dict = held.Get<ezVariantDictionary>();
      if (ezMcpJson::GetString(dict, "name") == sSlot)
      {
        EZ_TEST_INT(ezMcpJson::GetInt(dict, "framesRemaining", 0), uiRemaining);
        return;
      }
    }
    EZ_TEST_BOOL_MSG(false, "Expected a holding entry for '%s'.", sSlot.GetStartPointer());
  }

  void CheckButton(ezKeyState::Enum state, float fExpectedValue)
  {
    float fValue = -1.0f;
    EZ_TEST_INT(ezInputManager::GetInputSlotState(s_Button, &fValue), state);
    EZ_TEST_FLOAT(fValue, fExpectedValue, 0.0f);
    EZ_TEST_INT(ezInputManager::GetInputActionState(s_InputSet, "button", &fValue), state);
    EZ_TEST_FLOAT(fValue, fExpectedValue, 0.0f);
  }

  struct InputFixture
  {
    HardwareInput m_Hardware;

    InputFixture()
    {
      EZ_TEST_BOOL(ezPlugin::LoadPlugin("ezMcpPlugin").Succeeded());
      ezMcpToolRegistry::UpdateProviders();
      ezInputActionConfig config;
      config.m_bApplyTimeScaling = false;
      config.m_sInputSlotTrigger[0] = s_Button;
      ezInputManager::SetInputActionConfig(s_InputSet, "button", config, true);
      UpdateInput();
      UpdateInput();
    }

    ~InputFixture()
    {
      ezMcpToolRegistry::Clear();
      m_Hardware.Clear();
      UpdateInput();
      UpdateInput();
      ezInputManager::RemoveInputAction(s_InputSet, "button");
      ezPlugin::UnloadAllPlugins();
    }
  };

  // No window, renderer or app loop. Drive the real host's public event hook so presentation waits
  // can be tested independently of InputManager updates, without exposing a production test clock.
  class PresentationFixture : public ezGameApplicationBase
  {
  public:
    PresentationFixture()
      : ezGameApplicationBase("MCP input test")
    {
      auto* pCommandLine = ezCommandLineUtils::GetGlobalInstance();
      m_OriginalArguments = pCommandLine->GetCommandLineArray();
      ezMcpSocket probe;
      EZ_TEST_BOOL(probe.Listen(0).Succeeded());
      ezStringBuilder sPort;
      sPort.SetFormat("{}", probe.GetPort());
      probe.Close();
      ezDynamicArray<ezString> arguments;
      arguments.PushBack("McpInputTest");
      arguments.PushBack("-mcpport");
      arguments.PushBack(sPort);
      pCommandLine->SetCommandLine(arguments);
      ezMcpEngineHost::Startup();
      auto* pServer = ezMcpServer::GetInstance();
      EZ_TEST_BOOL(pServer != nullptr && pServer->IsRunning());
    }

    ~PresentationFixture()
    {
      ezMcpEngineHost::Shutdown();
      EZ_TEST_BOOL(ezMcpServer::GetInstance() == nullptr);
      ezCommandLineUtils::GetGlobalInstance()->SetCommandLine(m_OriginalArguments);
    }

    void Present()
    {
      ezGameApplicationExecutionEvent event;
      event.m_Type = ezGameApplicationExecutionEvent::Type::BeforePresent;
      m_ExecutionEvents.Broadcast(event);
    }

  private:
    ezString FindProjectDirectory() const override { return {}; }
    void Init_SetupGraphicsDevice() override {}
    void Deinit_ShutdownGraphicsDevice() override {}
    void Run_WorldUpdateAndRender() override {}
    ezDynamicArray<ezString> m_OriginalArguments;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(McpInput);

EZ_CREATE_SIMPLE_TEST(McpInput, TimedUpdates)
{
  InputFixture fixture;
  for (const ezUInt32 uiUpdates : {1u, 3u})
  {
    EZ_TEST_BOOL(!Call("input_set", SetArguments(s_Button, 1.0, uiUpdates)).m_bIsError);
    for (ezUInt32 i = 0; i < uiUpdates; ++i)
    {
      // Polling is not input consumption and must not spend the duration.
      ezInputManager::PollHardware();
      ezInputManager::PollHardware();
      UpdateInput();
      CheckButton(i == 0 ? ezKeyState::Pressed : ezKeyState::Down, 1.0f);
    }
    UpdateInput();
    CheckButton(ezKeyState::Released, 0.0f);
    UpdateInput();
    CheckButton(ezKeyState::Up, 0.0f);
  }
}

EZ_CREATE_SIMPLE_TEST(McpInput, InvalidDuration)
{
  InputFixture fixture;
  for (const ezVariant& duration : {ezVariant(-1.0), ezVariant(0.5), ezVariant(4294967296.0),
         ezVariant(std::numeric_limits<double>::quiet_NaN()), ezVariant(std::numeric_limits<double>::infinity()),
         ezVariant(-std::numeric_limits<double>::infinity()), ezVariant(std::numeric_limits<ezUInt64>::max()),
         ezVariant(true), ezVariant("1"), ezVariant()})
  {
    auto arguments = SetArguments(s_Button, 1.0, duration);
    arguments.Insert("clearAll", true);
    Call("input_set", SetArguments(s_OtherButton, 0.75));
    UpdateInput();
    EZ_TEST_BOOL(Call("input_set", arguments).m_bIsError);
    UpdateInput();
    CheckButton(ezKeyState::Up, 0.0f);
    CheckSlot(s_OtherButton, ezKeyState::Down, 0.75f);
    ezVariantDictionary clear;
    clear.Insert("clearAll", true);
    Call("input_set", clear);
    UpdateInput();
    UpdateInput();
  }
}

EZ_CREATE_SIMPLE_TEST(McpInput, RelativeUpdates)
{
  InputFixture fixture;
  for (const ezStringView sSlot : {ezStringView(ezInputSlot_MouseMovePosX), ezStringView(ezInputSlot_MouseMoveNegX), ezStringView(ezInputSlot_MouseWheelUp)})
  {
    for (const ezUInt32 uiUpdates : {1u, 3u})
    {
      Call("input_set", SetArguments(sSlot, 0.25, uiUpdates));
      float fDisplacement = 0.0f;
      for (ezUInt32 i = 0; i < uiUpdates; ++i)
      {
        ezInputManager::PollHardware();
        ezInputManager::PollHardware();
        UpdateInput();
        const float fDelta = SlotValue(sSlot);
        EZ_TEST_FLOAT(fDelta, 0.25f, 0.0f);
        fDisplacement += fDelta;
      }
      EZ_TEST_FLOAT(fDisplacement, uiUpdates * 0.25f, 0.0f);
      UpdateInput();
      CheckSlot(sSlot, ezKeyState::Released, 0.0f);
      UpdateInput();
      CheckSlot(sSlot, ezKeyState::Up, 0.0f);
    }
  }

  Call("input_set", SetArguments(ezInputSlot_MouseMovePosX, -0.25, 1));
  UpdateInput();
  // Negative numeric values are not negative motion: the manager only gathers positive slots.
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMovePosX), 0.0f, 0.0f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, ReplacementAndClear)
{
  InputFixture fixture;
  Call("input_set", SetArguments(s_Button, 1.0, 3));
  ezInputManager::PollHardware();
  ezInputManager::PollHardware();
  CheckHolding(s_Button, 3);
  UpdateInput();
  CheckHolding(s_Button, 2);
  CheckButton(ezKeyState::Pressed, 1.0f);

  Call("input_set", SetArguments(s_Button, 0.5, 1));
  UpdateInput();
  CheckButton(ezKeyState::Down, 0.5f);
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);

  Call("input_set", SetArguments(s_Button, 1.0, 2));
  UpdateInput();
  Call("input_set", SetArguments(s_Button, 0.75, 0));
  for (ezUInt32 i = 0; i < 4; ++i)
    UpdateInput();
  CheckHolding(s_Button, 0);
  CheckButton(ezKeyState::Down, 0.75f);

  Call("input_set", SetArguments(s_Button, 0.5, 1));
  UpdateInput();
  CheckButton(ezKeyState::Down, 0.5f);
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  auto omitted = Step("slots", ButtonSet());
  Call("input_set", omitted);
  UpdateInput();
  UpdateInput();
  CheckHolding(s_Button, 0);
  CheckButton(ezKeyState::Down, 1.0f);
  Call("input_set", SetArguments(s_Button, 0.0));
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);

  Call("input_set", SetArguments(s_Button, 1.0, 100));
  Call("input_set", SetArguments(s_OtherButton, 1.0));
  UpdateInput();
  auto replacement = SetArguments(s_Button, 0.5, 1);
  replacement.Insert("clearAll", true);
  Call("input_set", replacement);
  UpdateInput();
  CheckButton(ezKeyState::Down, 0.5f);
  CheckSlot(s_OtherButton, ezKeyState::Released, 0.0f);
  ClearAll();
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);

  Call("input_set", SetArguments(s_Button, 1.0, 4294967295.0));
  CheckHolding(s_Button, 4294967295u);
  UpdateInput();
  CheckHolding(s_Button, 4294967294u);
  ClearAll();
}

EZ_CREATE_SIMPLE_TEST(McpInput, HardwareCoexistence)
{
  InputFixture fixture;
  fixture.m_Hardware.Set(s_Button, 1.0f);
  fixture.m_Hardware.Set(ezInputSlot_MousePositionX, 0.9f);
  fixture.m_Hardware.Set(ezInputSlot_MouseMovePosX, 0.5f);
  Call("input_set", SetArguments(s_Button, 0.5, 1));
  Call("input_set", SetArguments(ezInputSlot_MousePositionX, 0.25, 1));
  Call("input_set", SetArguments(ezInputSlot_MouseMovePosX, 0.25, 1));
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MousePositionX), 0.25f, 0.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMovePosX), 0.5f, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MousePositionX), 0.9f, 0.0f);
  Call("input_set", SetArguments(s_Button, 0.0));
  ClearAll();
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
  fixture.m_Hardware.Clear();
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);

  ezInputManager::SetInputSlotDeadZone(s_Button, 0.25f);
  Call("input_set", SetArguments(s_Button, 0.125, 1));
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);
  ezInputManager::SetInputSlotDeadZone(s_Button, 0.0001f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, GatheredSnapshot)
{
  InputFixture fixture;
  Call("input_set", SetArguments(s_Button, 1.0, 1));
  ezInputManager::PollHardware();
  ClearAll();
  UpdateInput();
  // A hardware poll has already gathered this contribution into the manager's pending snapshot.
  // Device clear cannot retract it; it disappears on the following consuming update.
  CheckButton(ezKeyState::Pressed, 1.0f);
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, AtomicValues)
{
  InputFixture fixture;
  ezVariantDictionary slots;
  slots.Insert(s_Button, 1.0);
  slots.Insert(s_OtherButton, 1.0);
  // Dictionary order is unspecified. Put the invalid value last in its actual iteration order.
  auto invalid = slots.GetIterator();
  ++invalid;
  invalid.Value() = "not a number";
  EZ_TEST_BOOL(Call("input_set", Step("slots", slots)).m_bIsError);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);
  float fValue;
  EZ_TEST_INT(ezInputManager::GetInputSlotState(s_OtherButton, &fValue), ezKeyState::Up);
  EZ_TEST_FLOAT(fValue, 0.0f, 0.0f);

  Call("input_set", SetArguments(s_Button, 1.0));
  UpdateInput();
  ezVariantDictionary arguments = SetArguments(s_OtherButton, "not a number");
  arguments.Insert("clearAll", true);
  EZ_TEST_BOOL(Call("input_set", arguments).m_bIsError);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);

  for (const ezVariant& invalidValue : {ezVariant(std::numeric_limits<double>::quiet_NaN()),
         ezVariant(std::numeric_limits<double>::infinity()), ezVariant(-std::numeric_limits<double>::infinity()),
         ezVariant(std::numeric_limits<double>::max()), ezVariant(ezVariantArray()), ezVariant()})
  {
    arguments = SetArguments(s_OtherButton, invalidValue);
    arguments.Insert("clearAll", true);
    EZ_TEST_BOOL(Call("input_set", arguments).m_bIsError);
    UpdateInput();
    CheckButton(ezKeyState::Down, 1.0f);
    CheckSlot(s_OtherButton, ezKeyState::Up, 0.0f);
  }
  arguments = Step("slots", 5.0);
  arguments.Insert("clearAll", true);
  EZ_TEST_BOOL(Call("input_set", arguments).m_bIsError);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, SequencePrevalidation)
{
  InputFixture fixture;
  ezVariantArray invalidSteps;
  invalidSteps.PushBack(5.0);
  invalidSteps.PushBack(Step("unknown", true));
  invalidSteps.PushBack(Step("set", 1.0));
  invalidSteps.PushBack(Step("set", Step(s_OtherButton, "invalid")));
  invalidSteps.PushBack(Step("wait", 1.0));
  invalidSteps.PushBack(Step("text", 1.0));
  invalidSteps.PushBack(Step("clear", false));
  auto ambiguous = Step("clear", true);
  ambiguous.Insert("set", ButtonSet());
  invalidSteps.PushBack(ambiguous);
  for (const ezVariant& frames : {ezVariant(0.0), ezVariant(-1.0), ezVariant(0.5), ezVariant(10001.0),
         ezVariant(4294967296.0), ezVariant(std::numeric_limits<double>::quiet_NaN()),
         ezVariant(std::numeric_limits<double>::infinity()), ezVariant("1"), ezVariant(true)})
    invalidSteps.PushBack(Step("wait", Step("frames", frames)));

  Call("input_set", SetArguments(s_OtherButton, 0.75));
  UpdateInput();
  for (const auto& invalidStep : invalidSteps)
  {
    ezVariantArray steps;
    steps.PushBack(Step("text", "must not be queued"));
    steps.PushBack(Step("clear", true));
    steps.PushBack(Step("set", ButtonSet()));
    steps.PushBack(invalidStep);
    const auto toolResult = Call("input_sequence", SequenceArguments(steps));
    EZ_TEST_BOOL(toolResult.m_bIsError && !toolResult.m_bNotFinished);
    UpdateInput();
    CheckButton(ezKeyState::Up, 0.0f);
    CheckSlot(s_OtherButton, ezKeyState::Down, 0.75f);
    EZ_TEST_BOOL(ezInputManager::RetrieveLastCharacters().IsEmpty());
  }

  ezVariantArray steps;
  steps.PushBack(Step("set", ButtonSet()));
  for (const ezVariant& timeout : {ezVariant(-1.0), ezVariant(0.0), ezVariant(true), ezVariant("1"),
         ezVariant(std::numeric_limits<double>::quiet_NaN()), ezVariant(std::numeric_limits<double>::infinity())})
  {
    auto arguments = SequenceArguments(steps);
    arguments.Insert("timeout", timeout);
    EZ_TEST_BOOL(Call("input_sequence", arguments).m_bIsError);
    UpdateInput();
    CheckButton(ezKeyState::Up, 0.0f);
  }
  while (steps.GetCount() < 201)
    steps.PushBack(Step("clear", true));
  EZ_TEST_BOOL(Call("input_sequence", SequenceArguments(steps)).m_bIsError);
  EZ_TEST_BOOL(Call("input_sequence", SequenceArguments(ezVariantArray())).m_bIsError);
  UpdateInput();
  CheckSlot(s_OtherButton, ezKeyState::Down, 0.75f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, SequenceTimeoutCleanup)
{
  InputFixture fixture;
  PresentationFixture presentations;
  Call("input_set", SetArguments(s_OtherButton, 0.75));
  auto touched = ButtonSet();
  touched.Insert(ezInputSlot_MouseMoveNegX, 0.25);
  ezVariantArray steps;
  steps.PushBack(Step("set", touched));
  steps.PushBack(Step("wait", Step("frames", 1.0)));
  const auto arguments = SequenceArguments(steps);
  EZ_TEST_BOOL(Call("input_sequence", arguments).m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMoveNegX), 0.25f, 0.0f);
  EZ_TEST_BOOL(Call("input_sequence", arguments).m_bNotFinished);
  // Timeout is the behavior under test; no presentation progress is provided.
  ezThreadUtils::Sleep(ezTime::MakeFromSeconds(1.05));
  const auto timedOut = Call("input_sequence", arguments);
  EZ_TEST_BOOL(timedOut.m_bIsError);
  EZ_TEST_BOOL(!timedOut.m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMoveNegX), 0.0f, 0.0f);
  float fValue;
  ezInputManager::GetInputSlotState(s_OtherButton, &fValue);
  EZ_TEST_FLOAT(fValue, 0.75f, 0.0f);

  steps.Clear();
  steps.PushBack(Step("set", ButtonSet()));
  steps.PushBack(Step("wait", Step("frames", 1.0)));
  const auto nextArguments = SequenceArguments(steps);
  const auto subsequent = Call("input_sequence", nextArguments);
  EZ_TEST_BOOL(!subsequent.m_bIsError && subsequent.m_bNotFinished);
  EZ_TEST_BOOL(Call("input_sequence", nextArguments).m_bNotFinished);
  presentations.Present();
  const auto nextCompleted = Call("input_sequence", nextArguments);
  EZ_TEST_BOOL(!nextCompleted.m_bIsError && !nextCompleted.m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
}

EZ_CREATE_SIMPLE_TEST(McpInput, SequencePresentationProgress)
{
  InputFixture fixture;
  PresentationFixture presentations;
  ezVariantDictionary chord = ButtonSet();
  chord.Insert(s_OtherButton, 1.0);
  chord.Insert(ezInputSlot_MouseMoveNegX, 0.25);
  ezVariantArray steps;
  steps.PushBack(Step("set", chord));
  steps.PushBack(Step("text", "once"));
  steps.PushBack(Step("wait", Step("frames", 1.0)));
  steps.PushBack(Step("clear", true));
  const auto arguments = SequenceArguments(steps);
  EZ_TEST_BOOL(Call("input_sequence", arguments).m_bNotFinished);
  EZ_TEST_BOOL(Call("input_sequence", arguments).m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  CheckSlot(s_OtherButton, ezKeyState::Pressed, 1.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMoveNegX), 0.25f, 0.0f);
  EZ_TEST_STRING(ezInputManager::RetrieveLastCharacters(), "once");
  // Input consumption alone must not finish a presentation wait or repeat earlier text/set steps.
  EZ_TEST_BOOL(Call("input_sequence", arguments).m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
  EZ_TEST_BOOL(ezInputManager::RetrieveLastCharacters().IsEmpty());
  presentations.Present();
  const auto completed = Call("input_sequence", arguments);
  EZ_TEST_BOOL(!completed.m_bNotFinished && !completed.m_bIsError);
  EZ_TEST_INT(ezMcpJson::GetInt(ReadResult(completed), "stepsRun", 0), 4);
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  CheckSlot(s_OtherButton, ezKeyState::Released, 0.0f);
  EZ_TEST_FLOAT(SlotValue(ezInputSlot_MouseMoveNegX), 0.0f, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);

  // Conversely, a presentation event may finish a sequence without any consuming input update.
  steps.Clear();
  steps.PushBack(Step("set", ButtonSet()));
  steps.PushBack(Step("wait", ezVariantDictionary())); // Default one present.
  steps.PushBack(Step("clear", true));
  auto renderOnly = SequenceArguments(steps);
  EZ_TEST_BOOL(Call("input_sequence", renderOnly).m_bNotFinished);
  presentations.Present();
  EZ_TEST_BOOL(!Call("input_sequence", renderOnly).m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);

  // Two separate waits count from their own starting presentation, not from sequence entry.
  steps.Clear();
  steps.PushBack(Step("wait", Step("frames", 1.0)));
  steps.PushBack(Step("set", ButtonSet()));
  steps.PushBack(Step("wait", Step("frames", 2.0)));
  auto twoWaits = SequenceArguments(steps);
  EZ_TEST_BOOL(Call("input_sequence", twoWaits).m_bNotFinished);
  presentations.Present();
  EZ_TEST_BOOL(Call("input_sequence", twoWaits).m_bNotFinished);
  presentations.Present();
  EZ_TEST_BOOL(Call("input_sequence", twoWaits).m_bNotFinished);
  presentations.Present();
  const auto heldSuccess = Call("input_sequence", twoWaits);
  EZ_TEST_BOOL(!heldSuccess.m_bNotFinished && !heldSuccess.m_bIsError);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
  CheckHolding(s_Button, 0);
}

EZ_CREATE_SIMPLE_TEST(McpInput, Deactivation)
{
  InputFixture fixture;
  ezMcpToolRegistry::Clear();
  const auto* pType = ezRTTI::FindTypeByName("ezMcpInputTool");
  EZ_TEST_BOOL(pType != nullptr && pType->GetAllocator()->CanAllocate());
  if (pType == nullptr || !pType->GetAllocator()->CanAllocate())
    return;
  ezMcpToolProvider* pProvider = pType->GetAllocator()->Allocate<ezMcpToolProvider>();
  pProvider->OnActivate();
  ezVariantArray steps;
  steps.PushBack(Step("set", ButtonSet()));
  steps.PushBack(Step("wait", Step("frames", 100.0)));
  ezMcpToolResult waiting;
  pProvider->Execute("input_sequence", SequenceArguments(steps), waiting);
  EZ_TEST_BOOL(waiting.m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  pProvider->OnDeactivate();
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  UpdateInput();
  CheckButton(ezKeyState::Up, 0.0f);
  pProvider->OnActivate();
  steps.Clear();
  steps.PushBack(Step("set", ButtonSet()));
  ezMcpToolResult completed;
  pProvider->Execute("input_sequence", SequenceArguments(steps), completed);
  EZ_TEST_BOOL(!completed.m_bIsError && !completed.m_bNotFinished);
  UpdateInput();
  CheckButton(ezKeyState::Pressed, 1.0f);
  fixture.m_Hardware.Set(s_Button, 1.0f);
  pProvider->OnDeactivate();
  pType->GetAllocator()->Deallocate(pProvider);
  UpdateInput();
  CheckButton(ezKeyState::Down, 1.0f);
  fixture.m_Hardware.Clear();
  UpdateInput();
  CheckButton(ezKeyState::Released, 0.0f);
  // Registry teardown must also unregister an active timed contribution.
  ezMcpToolRegistry::UpdateProviders();
  Call("input_set", SetArguments(s_OtherButton, 1.0, 100));
  UpdateInput();
  CheckSlot(s_OtherButton, ezKeyState::Pressed, 1.0f);
  ezMcpToolRegistry::Clear();
  UpdateInput();
  CheckSlot(s_OtherButton, ezKeyState::Released, 0.0f);
}

#endif
