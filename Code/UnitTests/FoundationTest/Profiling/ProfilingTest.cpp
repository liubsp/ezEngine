#include <FoundationTest/FoundationTestPCH.h>

#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/Profiling/Profiling.h>
#include <Foundation/Threading/ThreadUtils.h>
#include <Foundation/Utilities/CommandLineUtils.h>

namespace
{
  void WriteOutProfilingCapture(const char* szFilePath)
  {
    ezStringBuilder outputPath = ezTestFramework::GetInstance()->GetAbsOutputPath();
    EZ_TEST_BOOL(ezFileSystem::AddDataDirectory(outputPath.GetData(), "test", "output", ezDataDirUsage::AllowWrites) == EZ_SUCCESS);

    ezFileWriter fileWriter;
    if (fileWriter.Open(szFilePath) == EZ_SUCCESS)
    {
      ezProfilingSystem::ProfilingData profilingData;
      ezProfilingSystem::Capture(profilingData);
      profilingData.Write(fileWriter).IgnoreResult();
      ezLog::Info("Profiling capture saved to '{0}'.", fileWriter.GetFilePathAbsolute().GetData());
    }
  }
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(Profiling);

EZ_CREATE_SIMPLE_TEST(Profiling, Profiling)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Local capture and explicit network profiling")
  {
#if TRACY_ENABLE
    EZ_TEST_BOOL(TracyIsStarted == ezCommandLineUtils::GetGlobalInstance()->GetBoolOption("-tracy", false));
#endif
    {
      EZ_PROFILE_SCOPE("OfflineCaptureRegression");
      ezThreadUtils::Sleep(ezTime::MakeFromMilliseconds(1));
    }
    ezProfilingSystem::ProfilingData captured;
    ezProfilingSystem::Capture(captured);
    bool bFound = false;
    for (const auto& buffer : captured.m_AllEventBuffers)
    {
      for (const auto& scope : buffer.m_Data)
      {
        if (ezStringUtils::IsEqual(scope.m_szName, "OfflineCaptureRegression"))
        {
          bFound = true;
          EZ_TEST_BOOL(scope.m_EndTime > scope.m_BeginTime);
        }
      }
    }
#if EZ_ENABLED(EZ_USE_PROFILING)
    EZ_TEST_BOOL(bFound);
#endif
  }
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Nested scopes")
  {
    ezProfilingSystem::Clear();

    {
      EZ_PROFILE_SCOPE("Prewarm scope");
      ezThreadUtils::Sleep(ezTime::MakeFromMilliseconds(1));
    }

    ezTime endTime = ezTime::Now() + ezTime::MakeFromMilliseconds(1);

    {
      EZ_PROFILE_SCOPE("Outer scope");

      {
        EZ_PROFILE_SCOPE("Inner scope");

        while (ezTime::Now() < endTime)
        {
        }
      }
    }

    WriteOutProfilingCapture(":output/profilingScopes.json");
  }
}
