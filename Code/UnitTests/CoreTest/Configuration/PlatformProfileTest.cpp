#include <CoreTest/CoreTestPCH.h>

#include <Core/Configuration/PlatformProfile.h>
#include <Foundation/IO/ChunkStream.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/IO/OSFile.h>

#if EZ_ENABLED(EZ_PLATFORM_LINUX) && EZ_ENABLED(EZ_SUPPORTS_PROCESSES)
#  include <Foundation/System/Process.h>
#  include <Foundation/Utilities/CommandLineUtils.h>
#  include <signal.h>
#  include <sys/resource.h>
#  include <unistd.h>
#endif

EZ_CREATE_SIMPLE_TEST_GROUP(Configuration);

class ezRuntimeProfileTestConfig : public ezProfileConfigData
{
  EZ_ADD_DYNAMIC_REFLECTION(ezRuntimeProfileTestConfig, ezProfileConfigData);

public:
  ezUInt32 m_uiValue = 0;
  ezUInt32 m_uiPayloadSize = 0;

  void SaveRuntimeData(ezChunkStreamWriter& inout_stream) const override
  {
    inout_stream.BeginChunk("RuntimeProfileTest", 1);
    inout_stream << m_uiValue;
    ezDynamicArray<ezUInt8> payload;
    payload.SetCount(m_uiPayloadSize, 0x5A);
    inout_stream.WriteBytes(payload.GetData(), payload.GetCount()).IgnoreResult();
    inout_stream.EndChunk();
  }

  void LoadRuntimeData(ezChunkStreamReader& inout_stream) override
  {
    if (inout_stream.GetCurrentChunk().m_sChunkName == "RuntimeProfileTest")
      inout_stream >> m_uiValue;
  }
};

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezRuntimeProfileTestConfig, 1, ezRTTIDefaultAllocator<ezRuntimeProfileTestConfig>)
EZ_END_DYNAMIC_REFLECTED_TYPE;

namespace
{
  class FailingProfileWriter : public ezDataDirectoryWriter
  {
  public:
    FailingProfileWriter()
      : ezDataDirectoryWriter(0)
    {
    }
    ezUInt64 GetFileSize() const override { return 0; }
    ezResult Write(const void*, ezUInt64 uiBytes) override
    {
      if (uiBytes != 0)
        ++s_uiFailedWrites;
      return uiBytes == 0 ? EZ_SUCCESS : EZ_FAILURE;
    }
    static ezUInt32 s_uiFailedWrites;

  protected:
    ezResult InternalOpen(ezFileShareMode::Enum) override { return EZ_SUCCESS; }
    void InternalClose() override {}
  };

  ezUInt32 FailingProfileWriter::s_uiFailedWrites = 0;

  class FailingProfileDirectory : public ezDataDirectoryType
  {
  public:
    static ezDataDirectoryType* Factory(ezStringView sPath, ezStringView, ezStringView, ezDataDirUsage)
    {
      if (!sPath.EndsWith("/ProfileSaveFailure/"))
        return nullptr;
      FailingProfileDirectory* pDir = EZ_DEFAULT_NEW(FailingProfileDirectory);
      if (pDir->InitializeDataDirectory(sPath).Succeeded())
        return pDir;
      EZ_DEFAULT_DELETE(pDir);
      return nullptr;
    }

  protected:
    ezResult InternalInitializeDataDirectory(ezStringView) override { return EZ_SUCCESS; }
    ezDataDirectoryReader* OpenFileToRead(ezStringView, ezFileShareMode::Enum, bool) override { return nullptr; }
    ezDataDirectoryWriter* OpenFileToWrite(ezStringView sFile, ezFileShareMode::Enum shareMode) override
    {
      return m_Writer.Open(sFile, this, shareMode).Succeeded() ? &m_Writer : nullptr;
    }
    void RemoveDataDirectory() override
    {
      auto* pSelf = this;
      EZ_DEFAULT_DELETE(pSelf);
    }
    ezResult GetFileStats(ezStringView, bool, ezFileStats&) override { return EZ_FAILURE; }

    FailingProfileWriter m_Writer;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST(Configuration, PlatformProfile)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Buffered flush and streaming write failures propagate")
  {
    ezFileSystem::RegisterDataDirectoryFactory(FailingProfileDirectory::Factory);
    ezStringBuilder sPath(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/ProfileSaveFailure/");
    if (!EZ_TEST_BOOL(ezFileSystem::AddDataDirectory(sPath, "ProfileSaveTest", "profilefail", ezDataDirUsage::AllowWrites).Succeeded()))
      return;
    EZ_SCOPE_EXIT(ezFileSystem::RemoveDataDirectoryGroup("ProfileSaveTest"));

    ezPlatformProfile profile;
    profile.AddMissingConfigs();
    auto* pConfig = profile.GetTypeConfig<ezRuntimeProfileTestConfig>();
    if (!EZ_TEST_BOOL(pConfig != nullptr))
      return;
    for (ezUInt32 uiSize : {0u, 2u * 1024u * 1024u})
    {
      pConfig->m_uiPayloadSize = uiSize;
      FailingProfileWriter::s_uiFailedWrites = 0;
      EZ_TEST_BOOL(profile.SaveForRuntime(":profilefail/test.ezProfile").Failed());
      EZ_TEST_BOOL(FailingProfileWriter::s_uiFailedWrites > 0);
    }
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Established chunk format and loader remain compatible")
  {
    ezStringBuilder sPath(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/ProfileSaveSuccess/");
    EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(sPath).Succeeded());
    if (!EZ_TEST_BOOL(ezFileSystem::AddDataDirectory(sPath, "ProfileSaveTest", "profilesave", ezDataDirUsage::AllowWrites).Succeeded()))
      return;
    EZ_SCOPE_EXIT(ezFileSystem::RemoveDataDirectoryGroup("ProfileSaveTest"));

    ezPlatformProfile profile;
    profile.AddMissingConfigs();
    auto* pConfig = profile.GetTypeConfig<ezRuntimeProfileTestConfig>();
    if (!EZ_TEST_BOOL(pConfig != nullptr))
      return;
    pConfig->m_uiValue = 731;

    // The established writer streamed version 1 chunks directly. Use the maintained chunk/config
    // writers as a reference, rather than duplicating their binary encoding in this test.
    ezDefaultMemoryStreamStorage reference;
    ezMemoryStreamWriter memory(&reference);
    ezChunkStreamWriter chunks(memory);
    chunks.BeginStream(1);
    pConfig->SaveRuntimeData(chunks);
    chunks.EndStream();
    ezDynamicArray<ezUInt8> expected;
    expected.SetCountUninitialized(static_cast<ezUInt32>(reference.GetStorageSize64()));
    ezMemoryStreamReader reader(&reference);
    EZ_TEST_INT(reader.ReadBytes(expected.GetData(), expected.GetCount()), expected.GetCount());

    for (ezUInt32 i = 0; i < 2; ++i)
    {
      EZ_TEST_BOOL(profile.SaveForRuntime(":profilesave/test.ezProfile").Succeeded());
      ezFileReader file;
      if (EZ_TEST_BOOL(file.Open(":profilesave/test.ezProfile").Succeeded()))
      {
        ezDynamicArray<ezUInt8> actual;
        actual.SetCountUninitialized(static_cast<ezUInt32>(file.GetFileSize()));
        EZ_TEST_INT(file.ReadBytes(actual.GetData(), actual.GetCount()), reference.GetStorageSize64());
        EZ_TEST_BOOL(actual == expected);
      }
      ezPlatformProfile loaded;
      loaded.AddMissingConfigs();
      EZ_TEST_BOOL(loaded.LoadForRuntime(":profilesave/test.ezProfile").Succeeded());
      const auto* pLoaded = loaded.GetTypeConfig<ezRuntimeProfileTestConfig>();
      if (EZ_TEST_BOOL(pLoaded != nullptr))
        EZ_TEST_INT(pLoaded->m_uiValue, 731);
    }
    EZ_TEST_BOOL(profile.SaveForRuntime(":unmounted/test.ezProfile").Failed());
  }
}

#if EZ_ENABLED(EZ_PLATFORM_LINUX) && EZ_ENABLED(EZ_SUPPORTS_PROCESSES)
EZ_CREATE_SIMPLE_TEST(Configuration, PlatformProfilePosixFailure)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Real folder buffered write failure in an isolated child")
  {
    if (!ezCommandLineUtils::GetGlobalInstance()->GetBoolOption("-profileWriteFailureChild"))
    {
      // Only the child changes its file-size limit and signal disposition. Give it separate
      // framework output and a finite lifetime; ezProcess also terminates on an early return.
      ezProcessOptions options;
      options.m_sProcess = ezOSFile::GetApplicationPath();
      options.m_Arguments.PushBack("-noGui");
      options.m_Arguments.PushBack("-filter");
      options.m_Arguments.PushBack("PlatformProfilePosixFailure");
      options.m_Arguments.PushBack("-profileWriteFailureChild");
      options.m_Arguments.PushBack("-outputDir");
      options.m_Arguments.PushBack(ezStringBuilder(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/PosixWriteChild"));

      ezProcess child;
      if (!EZ_TEST_BOOL(child.Launch(options).Succeeded()))
        return;
      if (!EZ_TEST_BOOL(child.WaitToFinish(ezTime::MakeFromSeconds(30)).Succeeded()))
      {
        EZ_TEST_BOOL(child.Terminate().Succeeded());
        return;
      }
      EZ_TEST_BOOL(child.GetState() == ezProcessState::Finished);
      EZ_TEST_INT(child.GetExitCode(), 0);
      return;
    }

    const ezStringBuilder sFolder(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/RealProfileFolder/");
    if (!EZ_TEST_BOOL(ezOSFile::CreateDirectoryStructure(sFolder).Succeeded()))
      return;
    if (!EZ_TEST_BOOL(ezFileSystem::AddDataDirectory(sFolder, "PosixProfileTest", "posixprofile", ezDataDirUsage::AllowWrites).Succeeded()))
      return;
    EZ_SCOPE_EXIT(ezFileSystem::RemoveDataDirectoryGroup("PosixProfileTest"));
    const ezStringView sOutput = ":posixprofile/small.ezProfile";
    const ezStringBuilder sAbsolute(sFolder, "small.ezProfile");
    EZ_SCOPE_EXIT(ezOSFile::DeleteFile(sAbsolute).IgnoreResult());

    ezPlatformProfile profile;
    profile.AddMissingConfigs();
    auto* pConfig = profile.GetTypeConfig<ezRuntimeProfileTestConfig>();
    if (!EZ_TEST_BOOL(pConfig != nullptr))
      return;
    pConfig->m_uiValue = 731;
    if (!EZ_TEST_BOOL(profile.SaveForRuntime(sOutput).Succeeded()))
      return;

    struct rlimit previousLimit;
    if (!EZ_TEST_BOOL(getrlimit(RLIMIT_FSIZE, &previousLimit) == 0))
      return;
    struct sigaction ignoreSignal = {};
    struct sigaction previousSignal;
    ignoreSignal.sa_handler = SIG_IGN;
    sigemptyset(&ignoreSignal.sa_mask);
    if (!EZ_TEST_BOOL(sigaction(SIGXFSZ, &ignoreSignal, &previousSignal) == 0))
      return;

    struct rlimit limited = previousLimit;
    limited.rlim_cur = 0; // Leave the hard limit unchanged so the child can restore its soft limit.
    const bool bLimitSet = setrlimit(RLIMIT_FSIZE, &limited) == 0;
    bool bOpened = false;
    bool bTruncated = false;
    ezResult saveResult = EZ_SUCCESS;
    if (bLimitSet)
    {
      // Open/truncate must succeed under the limit; otherwise the fixture only tests open failure.
      ezFileWriter file;
      bOpened = file.Open(sOutput).Succeeded();
      if (bOpened)
      {
        bTruncated = file.GetFileSize() == 0;
        file.Close();
        saveResult = profile.SaveForRuntime(sOutput);
      }
    }

    const bool bLimitRestored = setrlimit(RLIMIT_FSIZE, &previousLimit) == 0;
    const bool bSignalRestored = sigaction(SIGXFSZ, &previousSignal, nullptr) == 0;
    // Never run framework reporting or file cleanup under a failed restoration. The parent
    // reports this distinct child failure and its own limits/disposition remain unaffected.
    if (!bLimitRestored || !bSignalRestored)
      _exit(3);

    EZ_TEST_BOOL(bLimitSet);
    EZ_TEST_BOOL(bOpened);
    EZ_TEST_BOOL(bTruncated);
    EZ_TEST_BOOL(saveResult.Failed());
    EZ_TEST_BOOL(profile.SaveForRuntime(sOutput).Succeeded());
    ezPlatformProfile loaded;
    loaded.AddMissingConfigs();
    EZ_TEST_BOOL(loaded.LoadForRuntime(sOutput).Succeeded());
    const auto* pLoaded = loaded.GetTypeConfig<ezRuntimeProfileTestConfig>();
    if (EZ_TEST_BOOL(pLoaded != nullptr))
      EZ_TEST_INT(pLoaded->m_uiValue, 731);
  }
}
#endif
