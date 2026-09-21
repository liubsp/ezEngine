#include <FoundationTest/FoundationTestPCH.h>

#include <TestFramework/Framework/TestResults.h>

EZ_CREATE_SIMPLE_TEST(Utility, TestOutputNames)
{
  // Stop before exercising the old table's out-of-bounds FinalResult lookup. This reports the
  // missing ImageDiffFile entry as a normal regression failure rather than crashing the runner.
  if (!EZ_TEST_STRING(ezTestOutput::ToString(ezTestOutput::ImageDiffFile), "ImageDiffFile"))
    return;

  const struct
  {
    ezTestOutput::Enum m_Type;
    const char* m_szName;
  } cases[] = {
    {ezTestOutput::StartOutput, "StartOutput"},
    {ezTestOutput::BeginBlock, "BeginBlock"},
    {ezTestOutput::EndBlock, "EndBlock"},
    {ezTestOutput::ImportantInfo, "ImportantInfo"},
    {ezTestOutput::Details, "Details"},
    {ezTestOutput::Success, "Success"},
    {ezTestOutput::Message, "Message"},
    {ezTestOutput::Warning, "Warning"},
    {ezTestOutput::Error, "Error"},
    {ezTestOutput::ImageDiffFile, "ImageDiffFile"},
    {ezTestOutput::Duration, "Duration"},
    {ezTestOutput::FinalResult, "FinalResult"},
  };

  static_assert(EZ_ARRAY_SIZE(cases) == ezTestOutput::AllOutputTypes);
  for (const auto& entry : cases)
  {
    EZ_TEST_STRING(ezTestOutput::ToString(entry.m_Type), entry.m_szName);
    EZ_TEST_INT(ezTestOutput::FromString(entry.m_szName), entry.m_Type);
  }
  EZ_TEST_INT(ezTestOutput::FromString("not-an-output-type"), ezTestOutput::InvalidType);
}
