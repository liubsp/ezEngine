#include <McpPlugin/McpPluginPCH.h>

#include <McpPlugin/McpEngineHost.h>
#include <McpPlugin/McpInputDevice.h>
#include <McpPlugin/Tools/McpInputTool.h>

#include <Core/Input/InputManager.h>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezMcpInputTool, 1, ezRTTIDefaultAllocator<ezMcpInputTool>)
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezMcpInputTool::ezMcpInputTool() = default;
ezMcpInputTool::~ezMcpInputTool() = default;

void ezMcpInputTool::OnActivate()
{
  m_pDevice = EZ_DEFAULT_NEW(ezMcpInputDevice);
}

void ezMcpInputTool::OnDeactivate()
{
  // Destroying the device unregisters it, which also releases anything it was still holding down - a
  // key left pressed by a plugin that is going away would otherwise stay pressed forever.
  ResetSequence(true);
  m_pDevice = nullptr;
}

void ezMcpInputTool::GetSupportedTools(ezDynamicArray<ezMcpToolDesc>& out_tools) const
{
  {
    ezMcpToolDesc& desc = out_tools.ExpandAndGetRef();
    desc.m_sName = "input_slots";
    desc.m_sDescription =
      "Lists the input slots this process knows about - the names input_set writes to. Call it before guessing a name: "
      "the spelling is 'keyboard_w', 'mouse_button_0', 'mouse_move_pos_x', and which slots exist at all depends on "
      "which input devices the platform registered.\n"
      "'flags' says what kind of thing a slot is, which decides what value makes sense: a button is 0 or 1, an analog "
      "axis is anything in between, and a relative slot like mouse movement is a per-input-update delta rather than a "
      "position. 'value' is what the slot currently reads, merged across all devices including this one.";

    desc.m_sInputSchema = R"({"type":"object","properties":{"contains":{"type":"string","description":"Only return slots whose name or display name contains this text, case insensitive. Omit for all of them."},"activeOnly":{"type":"boolean","description":"Only return slots whose current value is not zero. Useful to find out what is being pressed right now. Default false."}}})";
  }

  {
    ezMcpToolDesc& desc = out_tools.ExpandAndGetRef();
    desc.m_sName = "input_set";
    desc.m_sDescription =
      "Presses keys and moves the mouse, by writing input slot values that the game reads as if they came from real "
      "hardware. Get the slot names from input_slots.\n"
      "Button and relative values merge with hardware by taking the larger value. Absolute position slots written "
      "by this device override hardware positions until cleared or expired.\n"
      "'frames' counts completed input-manager updates, starting with the next update, not hardware polls, "
      "application ticks or render frames. Pass an integer from 1 to 4294967295 for a timed hold; omit it or pass 0 "
      "to hold indefinitely until changed or cleared. Relative values repeat on each consuming update; use the "
      "positive-valued directional slots for negative movement.\n"
      "Clearing stops this device's contribution; it cannot retract a value already gathered by a hardware poll. "
      "Returning only means the value is set, not consumed or acted on. game_wait counts presentation events, "
      "which need not include input updates. Use caller deadlines and explicit clearAll cleanup for stalled hosts.";

    desc.m_sInputSchema = R"({"type":"object","properties":{"slots":{"type":"object","description":"Slot name to finite float value, e.g. {\"keyboard_w\":1.0,\"mouse_move_pos_x\":0.01}. A value of 0 stops writing that slot. All values are validated before mutation.","additionalProperties":{"type":"number"}},"frames":{"type":"integer","minimum":0,"maximum":4294967295,"description":"Completed input-manager updates to contribute to. Relative deltas repeat per update. 0 or omitted holds until changed; framesRemaining uses this same unit."},"clearAll":{"type":"boolean","description":"Release everything this tool is currently holding, before applying validated 'slots'. Pass it on its own to let go of everything."}}})";
  }

  {
    ezMcpToolDesc& desc = out_tools.ExpandAndGetRef();
    desc.m_sName = "input_sequence";
    desc.m_sDescription =
      "Runs a chain of input steps in order, in a single call, instead of one input_set/game_wait round trip "
      "per step. Each step is exactly one of:\n"
      "  {\"set\": {...}} - writes slot values, same as input_set's 'slots' (same hardware merge/position override, "
      "held until changed or cleared).\n"
      "  {\"wait\": {\"frames\": n}} - waits for n presentation-counter increments, same as game_wait, "
      "not n input-manager updates. Omitted frames defaults to 1; valid integers are 1 to 10000.\n"
      "  {\"text\": \"...\"} - queues typed text, delivered as one batch the next time the game reads input - "
      "not one character per frame, so it costs no extra frames by itself.\n"
      "  {\"clear\": true} - releases every slot this tool is currently holding.\n"
      "A chord (several keys in the same frame) is one 'set' step naming all of them, not several steps. "
      "All steps are validated before execution. Timeout releases slots written by this sequence and resets "
      "its continuation; unrelated holds remain. Successful final holds intentionally remain until cleared. "
      "The call returns once every step has run; only a 'wait' step defers, so a sequence without waits may "
      "finish before any input is consumed. Caller deadlines and owned-host cleanup are still needed if pumping stops.";

    desc.m_sInputSchema = R"({"type":"object","properties":{"steps":{"type":"array","minItems":1,"maxItems":200,"description":"Executed in order. See the tool description for the four step shapes.","items":{"type":"object"}},"timeout":{"type":"number","exclusiveMinimum":0,"description":"Positive finite seconds allowed for each individual wait step. Default 30. A timeout releases sequence-written slots."}},"required":["steps"]})";
  }
}

void ezMcpInputTool::Execute(ezStringView sToolName, const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  if (sToolName == "input_slots")
  {
    ExecuteSlots(arguments, out_result);
  }
  else if (sToolName == "input_set")
  {
    ExecuteSet(arguments, out_result);
  }
  else if (sToolName == "input_sequence")
  {
    ExecuteSequence(arguments, out_result);
  }
}

namespace
{
  bool ReadFrames(const ezVariantDictionary& arguments, ezUInt32 uiDefault, ezUInt32 uiMinimum, ezUInt32 uiMaximum,
    ezUInt32& out_uiFrames, ezMcpToolResult& out_result)
  {
    out_uiFrames = uiDefault;
    const ezVariant* pValue = nullptr;
    if (!arguments.TryGetValue("frames", pValue))
      return true;

    // The JSON reader supplies doubles. Check the original value before any integral narrowing:
    // fractional/negative/out-of-range input must never turn into an indefinite hold.
    if (pValue->IsNumber() && !pValue->IsA<bool>())
    {
      const double fFrames = pValue->ConvertTo<double>();
      if (ezMath::IsFinite(fFrames) && fFrames >= uiMinimum && fFrames <= uiMaximum && ezMath::Floor(fFrames) == fFrames)
      {
        out_uiFrames = static_cast<ezUInt32>(fFrames);
        return true;
      }
    }

    ezStringBuilder sError;
    sError.SetFormat("'frames' must be a finite integer between {} and {}.", uiMinimum, uiMaximum);
    out_result.SetError(sError);
    return false;
  }

  bool ValidateSlots(const ezVariantDictionary& slots, ezMcpToolResult& out_result)
  {
    for (auto it = slots.GetIterator(); it.IsValid(); ++it)
    {
      ezResult conversion = EZ_SUCCESS;
      const float fValue = it.Value().ConvertTo<float>(&conversion);
      if (conversion.Failed() || !ezMath::IsFinite(fValue))
      {
        ezStringBuilder sError;
        sError.SetFormat("The value for slot '{}' must be a finite float.", it.Key());
        out_result.SetError(sError);
        return false;
      }
    }
    return true;
  }

  /// \brief The flags of one slot, as the names a caller would recognise.
  ///
  /// Only the ones that change how a value should be chosen. The rest describe implementation details of
  /// the hardware and would just be noise in every listing.
  void WriteSlotFlags(ezMcpJsonWriter& ref_writer, ezBitflags<ezInputSlotFlags> flags)
  {
    ref_writer.BeginArray("flags");

    if (flags.IsSet(ezInputSlotFlags::ValueBinaryZeroOrOne))
      ref_writer.WriteString("binary");
    if (flags.IsSet(ezInputSlotFlags::ValueRangeZeroToOne))
      ref_writer.WriteString("analog");
    if (flags.IsSet(ezInputSlotFlags::ReportsRelativeValues))
      ref_writer.WriteString("relative");
    if (flags.IsSet(ezInputSlotFlags::Pressable))
      ref_writer.WriteString("pressable");
    if (flags.IsSet(ezInputSlotFlags::Holdable))
      ref_writer.WriteString("holdable");

    ref_writer.EndArray();
  }

  /// \brief What the device is currently holding, shared between input_set's and input_sequence's result.
  void WriteHeldSlots(ezMcpJsonWriter& ref_writer, const ezDynamicArray<ezMcpInputDevice::HeldSlot>& held)
  {
    ref_writer.BeginArray("holding");

    for (const ezMcpInputDevice::HeldSlot& slot : held)
    {
      ref_writer.BeginObject();
      ref_writer.AddVariableString("name", slot.m_sSlot);
      ref_writer.AddVariableFloat("value", slot.m_fValue);

      if (slot.m_uiFramesRemaining > 0)
      {
        ref_writer.AddVariableUInt32("framesRemaining", slot.m_uiFramesRemaining);
      }

      ref_writer.EndObject();
    }

    ref_writer.EndArray();
  }
} // namespace

void ezMcpInputTool::ExecuteSlots(const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  const ezStringView sContains = ezMcpJson::GetString(arguments, "contains");
  const bool bActiveOnly = ezMcpJson::GetBool(arguments, "activeOnly", false);

  ezDynamicArray<ezStringView> slots;
  ezInputManager::RetrieveAllKnownInputSlots(slots);

  ezMcpJsonWriter writer;
  writer.BeginObject();
  writer.BeginArray("slots");

  ezUInt32 uiMatches = 0;
  ezUInt32 uiReturned = 0;

  for (ezStringView sSlot : slots)
  {
    const ezStringView sDisplayName = ezInputManager::GetInputSlotDisplayName(sSlot);

    if (!sContains.IsEmpty())
    {
      ezStringBuilder sName = sSlot;
      ezStringBuilder sDisplay = sDisplayName;

      if (sName.FindSubString_NoCase(sContains) == nullptr && sDisplay.FindSubString_NoCase(sContains) == nullptr)
        continue;
    }

    float fValue = 0.0f;
    ezInputManager::GetInputSlotState(sSlot, &fValue);

    if (bActiveOnly && fValue == 0.0f)
      continue;

    ++uiMatches;

    if (uiReturned >= s_uiMaxResults)
      continue;

    writer.BeginObject();
    writer.AddVariableString("name", sSlot);

    // The display name is what a human calls the key. It differs from the slot name often enough to be
    // worth reporting - 'keyboard_left_ctrl' displays as 'Left Ctrl' - and it is also localised.
    if (!sDisplayName.IsEmpty() && sDisplayName != sSlot)
    {
      writer.AddVariableString("displayName", sDisplayName);
    }

    writer.AddVariableFloat("value", fValue);
    WriteSlotFlags(writer, ezInputManager::GetInputSlotFlags(sSlot));
    writer.EndObject();

    ++uiReturned;
  }

  writer.EndArray();

  writer.AddVariableUInt32("returned", uiReturned);
  writer.AddVariableUInt32("totalMatches", uiMatches);
  writer.AddVariableBool("truncated", uiMatches > uiReturned);

  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void ezMcpInputTool::ExecuteSet(const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  if (m_pDevice == nullptr)
  {
    out_result.SetError("The synthetic input device does not exist, so no input can be written.");
    return;
  }

  const bool bClearAll = ezMcpJson::GetBool(arguments, "clearAll", false);

  ezUInt32 uiFrames = 0;
  if (!ReadFrames(arguments, 0, 0, ezMath::MaxValue<ezUInt32>(), uiFrames, out_result))
    return;

  const ezVariant* pSlotArgument = nullptr;
  const ezVariantDictionary* pSlots = ezMcpJson::GetDict(arguments, "slots");
  if (arguments.TryGetValue("slots", pSlotArgument) && pSlots == nullptr)
  {
    out_result.SetError("'slots' must be an object of slot names and values.");
    return;
  }

  if (pSlots == nullptr && !bClearAll)
  {
    out_result.SetError("Nothing to do: pass 'slots' with the values to write, or 'clearAll' to release everything.");
    return;
  }

  ezUInt32 uiSet = 0;

  if (pSlots != nullptr && !ValidateSlots(*pSlots, out_result))
    return;

  if (bClearAll)
    m_pDevice->ClearAllSlots();

  if (pSlots != nullptr)
  {
    ApplySlots(*pSlots, uiFrames);
    uiSet = pSlots->GetCount();
  }

  ezMcpJsonWriter writer;
  writer.BeginObject();
  writer.AddVariableUInt32("slotsWritten", uiSet);

  // What is being held afterwards, so that a caller never has to remember what it pressed. This is also
  // the answer to 'did my earlier call already expire'.
  ezDynamicArray<ezMcpInputDevice::HeldSlot> held;
  m_pDevice->GetHeldSlots(held);
  WriteHeldSlots(writer, held);

  writer.AddVariableString("note",
    "framesRemaining counts completed input-manager updates, not polls or presents. Values are set, not yet "
    "necessarily consumed. game_wait counts presentation events and does not guarantee input consumption.");

  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void ezMcpInputTool::ApplySlots(const ezVariantDictionary& slots, ezUInt32 uiFrames)
{
  for (auto it = slots.GetIterator(); it.IsValid(); ++it)
  {
    const float fValue = it.Value().ConvertTo<float>();

    if (fValue == 0.0f)
    {
      m_pDevice->ClearSlot(it.Key());
    }
    else
    {
      m_pDevice->SetSlotValue(it.Key(), fValue, uiFrames);
    }
  }
}

bool ezMcpInputTool::ValidateSequence(const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  const ezVariantArray* pSteps = ezMcpJson::GetArray(arguments, "steps");

  if (pSteps == nullptr || pSteps->IsEmpty())
  {
    out_result.SetError("'steps' must be a non-empty array of step objects.");
    return false;
  }

  if (pSteps->GetCount() > s_uiMaxSteps)
  {
    ezStringBuilder sError;
    sError.SetFormat("'steps' has {} entries, which is more than the limit of {}.", pSteps->GetCount(), s_uiMaxSteps);
    out_result.SetError(sError);
    return false;
  }

  ezTime waitTimeout = s_DefaultWaitTimeout;
  const ezVariant* pTimeout = nullptr;
  if (arguments.TryGetValue("timeout", pTimeout))
  {
    if (!pTimeout->IsNumber() || pTimeout->IsA<bool>() || !ezMath::IsFinite(pTimeout->ConvertTo<double>()) || pTimeout->ConvertTo<double>() <= 0)
    {
      out_result.SetError("'timeout' must be a positive finite number of seconds.");
      return false;
    }
    waitTimeout = ezTime::MakeFromSeconds(pTimeout->ConvertTo<double>());
  }

  for (ezUInt32 i = 0; i < pSteps->GetCount(); ++i)
  {
    const ezVariant& step = (*pSteps)[i];
    if (!step.IsA<ezVariantDictionary>() || step.Get<ezVariantDictionary>().GetCount() != 1)
    {
      ezStringBuilder sError;
      sError.SetFormat("Step {} must be an object with exactly one of 'set', 'wait', 'text' or 'clear'.", i);
      out_result.SetError(sError);
      return false;
    }

    const ezVariantDictionary& stepDict = step.Get<ezVariantDictionary>();
    const auto entry = stepDict.GetIterator();
    bool bValid = false;
    if (entry.Key() == "set" && entry.Value().IsA<ezVariantDictionary>())
    {
      bValid = ValidateSlots(entry.Value().Get<ezVariantDictionary>(), out_result);
    }
    else if (entry.Key() == "wait" && entry.Value().IsA<ezVariantDictionary>())
    {
      ezUInt32 uiFrames;
      bValid = ReadFrames(entry.Value().Get<ezVariantDictionary>(), 1, 1, s_uiMaxWaitFrames, uiFrames, out_result);
    }
    else if (entry.Key() == "text" && entry.Value().IsA<ezString>())
    {
      bValid = true;
    }
    else if (entry.Key() == "clear" && entry.Value().IsA<bool>() && entry.Value().Get<bool>())
    {
      bValid = true;
    }

    if (!bValid)
    {
      if (!out_result.m_bIsError)
        out_result.SetError("Expected 'set'/'wait' objects, 'text' string or 'clear': true.");
      out_result.m_sText.PrependFormat("Step {}: ", i);
      return false;
    }
  }

  m_SequenceWaitTimeout = waitTimeout;
  // Own the prevalidated arguments across deferred re-entries; no partial execution or repeated
  // validation/conversion of earlier steps is needed while waiting.
  m_SequenceSteps = *pSteps;
  return true;
}

void ezMcpInputTool::ResetSequence(bool bReleaseTouchedSlots)
{
  if (bReleaseTouchedSlots && m_pDevice != nullptr)
  {
    for (const ezString& sSlot : m_SequenceTouchedSlots)
      m_pDevice->ClearSlot(sSlot);
  }
  m_SequenceTouchedSlots.Clear();
  m_SequenceSteps.Clear();
  m_bSequenceActive = false;
  m_bSequenceWaiting = false;
  m_uiSequenceStep = 0;
  m_uiSequenceWaitUntilFrame = 0;
  m_SequenceWaitStarted = ezTime::MakeZero();
  m_SequenceWaitTimeout = ezTime::MakeZero();
}

void ezMcpInputTool::ExecuteSequence(const ezVariantDictionary& arguments, ezMcpToolResult& out_result)
{
  if (m_pDevice == nullptr)
  {
    ResetSequence(true);
    out_result.SetError("The synthetic input device does not exist, so no input can be written.");
    return;
  }

  if (!m_bSequenceActive)
  {
    // First entry. Later ones are re-entries from a 'wait' step's deferral, with the same 'arguments'
    // - see ezMcpToolResult::m_bNotFinished - so steps before m_uiSequenceStep must not run again.
    if (!ValidateSequence(arguments, out_result))
    {
      ResetSequence(true);
      return;
    }
    m_bSequenceActive = true;
  }
  else if (m_bSequenceWaiting)
  {
    const ezUInt64 uiNow = ezMcpEngineHost::GetFrameCount();
    const bool bDone = uiNow >= m_uiSequenceWaitUntilFrame;
    const bool bTimedOut = !bDone && (ezTime::Now() - m_SequenceWaitStarted >= m_SequenceWaitTimeout);

    if (!bDone && !bTimedOut)
    {
      out_result.m_bNotFinished = true;
      return;
    }

    m_bSequenceWaiting = false;

    if (bTimedOut)
    {
      ezStringBuilder sError;
      sError.SetFormat("Sequence timed out on step {} (a 'wait') after {} seconds. The game is not producing "
                       "presentation events fast enough, or not at all. Sequence-written slots have been released.",
        m_uiSequenceStep, (ezTime::Now() - m_SequenceWaitStarted).GetSeconds());
      out_result.SetError(sError);
      ResetSequence(true);
      return;
    }

    // The 'wait' step that was in flight is now done; move on to whatever follows it.
    ++m_uiSequenceStep;
  }

  for (; m_uiSequenceStep < m_SequenceSteps.GetCount(); ++m_uiSequenceStep)
  {
    const ezVariantDictionary& stepDict = m_SequenceSteps[m_uiSequenceStep].Get<ezVariantDictionary>();

    if (const ezVariantDictionary* pSet = ezMcpJson::GetDict(stepDict, "set"))
    {
      ApplySlots(*pSet, 0);
      for (auto it = pSet->GetIterator(); it.IsValid(); ++it)
        m_SequenceTouchedSlots.Insert(it.Key());
    }
    else if (ezMcpJson::GetBool(stepDict, "clear", false))
    {
      m_pDevice->ClearAllSlots();
    }
    else
    {
      const ezVariantDictionary* pWait = ezMcpJson::GetDict(stepDict, "wait");

      if (pWait != nullptr)
      {
        ezUInt32 uiFrames;
        if (!ReadFrames(*pWait, 1, 1, s_uiMaxWaitFrames, uiFrames, out_result))
        {
          ResetSequence(true);
          return;
        }

        m_uiSequenceWaitUntilFrame = ezMcpEngineHost::GetFrameCount() + uiFrames;
        m_SequenceWaitStarted = ezTime::Now();
        m_bSequenceWaiting = true;

        out_result.m_bNotFinished = true;
        return;
      }
      else
      {
        m_pDevice->QueueText(stepDict.GetIterator().Value().Get<ezString>());
      }
    }
  }

  const ezUInt32 uiStepsRun = m_SequenceSteps.GetCount();
  ResetSequence(false); // Successful final holds deliberately outlive the sequence.

  ezMcpJsonWriter writer;
  writer.BeginObject();
  writer.AddVariableUInt32("stepsRun", uiStepsRun);
  writer.AddVariableUInt64("frameCount", ezMcpEngineHost::GetFrameCount());

  ezDynamicArray<ezMcpInputDevice::HeldSlot> held;
  m_pDevice->GetHeldSlots(held);
  WriteHeldSlots(writer, held);

  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}
