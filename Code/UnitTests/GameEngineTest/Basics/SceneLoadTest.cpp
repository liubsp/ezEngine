#include <GameEngineTest/GameEngineTestPCH.h>

#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GameEngine/Utils/SceneLoadUtil.h>
#include <GameEngineTest/TestClass/TestClass.h>

class ezSceneLoadTest : public ezGameEngineTest
{
public:
  const char* GetTestName() const override { return "Scene Load Failure"; }
  ezGameEngineTestApplication* CreateApplication() override { return EZ_DEFAULT_NEW(ezGameEngineTestApplication, "Basics"); }
  void SetupSubTests() override { AddSubTest("Header failures and fresh-loader recovery", 0); }

  ezTestAppRun RunSubTest(ezInt32, ezUInt32) override
  {
    EZ_TEST_BOOL(ezFileSystem::AddDataDirectory(">testout/", "SceneLoadTest", "scene-load", ezDataDirUsage::AllowWrites).Succeeded());
    EZ_SCOPE_EXIT(ezFileSystem::RemoveDataDirectoryGroup("SceneLoadTest"));
    ezDynamicArray<ezUInt8> bytes;
    ezMemoryStreamContainerWrapperStorage<ezDynamicArray<ezUInt8>> storage(&bytes);
    ezMemoryStreamWriter writer(&storage);
    ezAssetFileHeader header;
    header.SetFileHashAndVersion(42, 1);
    ezStringBuilder generator;
    for (ezUInt32 i = 0; i < 255; ++i)
      generator.Append("a");
    generator.Append("\xC3\xA9"); // A valid UTF-8 character straddles the header reader's chunk boundary.
    header.SetGenerator(generator);
    EZ_TEST_BOOL(header.Write(writer).Succeeded());
    const ezUInt32 uiHeaderSize = bytes.GetCount();
    const char tag[16] = "[ezBinaryScene]";
    EZ_TEST_BOOL(writer.WriteBytes(tag, sizeof(tag)).Succeeded());
    const ezUInt32 uiEnvelopeSize = bytes.GetCount();
    {
      ezWorldDesc sourceDesc("Scene load fixture");
      ezWorld source(sourceDesc);
      EZ_LOCK(source.GetWriteMarker());
      ezGameObjectDesc desc;
      desc.m_sName.Assign("LoadedObject");
      source.CreateObject(desc);
      ezWorldWriter worldWriter;
      worldWriter.WriteWorld(writer, source);
    }

    const char* szFile = ":scene-load/probe.ezBinScene";
    auto writeFile = [&](ezUInt32 uiCount)
    {
      ezFileWriter file;
      EZ_TEST_BOOL(file.Open(szFile).Succeeded());
      EZ_TEST_BOOL(file.WriteBytes(bytes.GetData(), uiCount).Succeeded());
    };
    auto expectFailure = [&](ezStringView sFile)
    {
      ezWorldDesc targetDesc("Unchanged target");
      ezWorld target(targetDesc);
      {
        EZ_LOCK(target.GetWriteMarker());
        ezGameObjectDesc existing;
        existing.m_sName.Assign("RetainedObject");
        target.CreateObject(existing);
        EZ_TEST_BOOL(ezSceneLoadUtility::LoadSceneImmediate(target, sFile).Failed());
        EZ_TEST_INT(target.GetObjectCount(), 1);
      }
      const ezUInt32 uiWorlds = ezWorld::GetWorldCount();
      ezSceneLoadUtility loader;
      loader.StartSceneLoading(sFile, {});
      loader.TickSceneLoading();
      EZ_TEST_BOOL(loader.GetLoadingState() == ezSceneLoadUtility::LoadingState::Failed);
      if (sFile == szFile)
      {
        // Failure must close the file while the failed loader is still alive.
        ezFileWriter replacement;
        EZ_TEST_BOOL(replacement.Open(sFile).Succeeded());
      }
      EZ_TEST_BOOL(!loader.GetLoadingFailureReason().IsEmpty());
      EZ_TEST_FLOAT(loader.GetLoadingProgress(), 0, 0);
      loader.TickSceneLoading();
      EZ_TEST_BOOL(loader.GetLoadingState() == ezSceneLoadUtility::LoadingState::Failed);
      return EZ_TEST_INT(ezWorld::GetWorldCount(), uiWorlds);
    };
    // Every prefix covers truncation inside identification, version/hash/type, generator and scene tag.
    for (ezUInt32 n = 0; n < uiEnvelopeSize; ++n)
    {
      if (n < uiHeaderSize)
      {
        ezRawMemoryStreamReader truncated(bytes.GetData(), n);
        EZ_TEST_BOOL(header.Read(truncated).Failed());
        EZ_TEST_BOOL(header.GetFileHash() == ezMath::MaxValue<ezUInt64>());
        EZ_TEST_INT(header.GetFileVersion(), 0);
        EZ_TEST_BOOL(header.GetGenerator().IsEmpty());
      }
      writeFile(n);
      if (!expectFailure(szFile))
        return ezTestAppRun::Quit;
    }
    bytes[uiHeaderSize] = '!';
    writeFile(bytes.GetCount());
    ezRawMemoryStreamReader complete(bytes.GetData(), uiHeaderSize);
    EZ_TEST_BOOL(header.Read(complete).Succeeded());
    EZ_TEST_INT(header.GetFileHash(), 42);
    EZ_TEST_INT(header.GetFileVersion(), 1);
    EZ_TEST_STRING(header.GetGenerator().GetView(), generator.GetView());
    expectFailure(szFile);
    bytes[uiHeaderSize] = '[';
    bytes[0] = '!';
    writeFile(bytes.GetCount());
    expectFailure(szFile);
    bytes[0] = 'e';
    const ezUInt8 uiVersion = bytes[7];
    for (ezUInt8 version : {0, 1, 2, 255})
    {
      bytes[7] = version;
      if (version == 1 || version == 2)
      {
        // Legacy headers contain tag/version/hash, with the type version added in version 2.
        ezRawMemoryStreamReader legacy(bytes.GetData(), version == 1 ? 16 : 18);
        EZ_TEST_BOOL(header.Read(legacy).Failed());
        EZ_TEST_BOOL(header.GetFileHash() == ezMath::MaxValue<ezUInt64>());
        EZ_TEST_INT(header.GetFileVersion(), version == 2 ? 1 : 0);
      }
      writeFile(bytes.GetCount());
      expectFailure(szFile);
    }
    bytes[7] = uiVersion;
    const ezUInt32 uiGeneratorStart = uiHeaderSize - generator.GetElementCount();
    bytes[uiGeneratorStart] = 0xff;
    writeFile(bytes.GetCount());
    expectFailure(szFile);
    bytes[uiGeneratorStart] = 'a';
    ezUInt8 length[4];
    for (ezUInt32 i = 0; i < 4; ++i)
    {
      length[i] = bytes[uiGeneratorStart - 4 + i];
      bytes[uiGeneratorStart - 4 + i] = 0xff;
    }
    writeFile(bytes.GetCount());
    expectFailure(szFile); // Advertised length must not cause a multi-gigabyte allocation.
    for (ezUInt32 i = 0; i < 4; ++i)
      bytes[uiGeneratorStart - 4 + i] = length[i];
    expectFailure(":scene-load/missing.ezBinScene");

    writeFile(bytes.GetCount());
    {
      ezWorldDesc targetDesc("Immediate valid load");
      ezWorld target(targetDesc);
      EZ_LOCK(target.GetWriteMarker());
      EZ_TEST_BOOL(ezSceneLoadUtility::LoadSceneImmediate(target, szFile).Succeeded());
      EZ_TEST_INT(target.GetObjectCount(), 1);
    }
    // Utilities are single-use: recovery creates a fresh loader, not a retry on partial state.
    ezSceneLoadUtility recovered;
    recovered.StartSceneLoading(szFile, {});
    for (ezUInt32 i = 0; i < 100 && recovered.GetLoadingState() == ezSceneLoadUtility::LoadingState::Ongoing; ++i)
      recovered.TickSceneLoading();
    if (EZ_TEST_BOOL(recovered.GetLoadingState() == ezSceneLoadUtility::LoadingState::FinishedSuccessfully))
    {
      auto world = recovered.RetrieveLoadedScene();
      EZ_LOCK(world->GetReadMarker());
      EZ_TEST_BOOL(world->GetWorldSimulationEnabled());
      EZ_TEST_INT(world->GetObjectCount(), 1);
    }
    for (const char* szSource : {"Recovery.ezScene", "Recovery.ezPrefab"})
    {
      ezStringBuilder redirected;
      EZ_TEST_BOOL(ezSceneLoadUtility::FindRedirectedSceneFile(redirected, szSource).Succeeded());
      EZ_TEST_BOOL(redirected.StartsWith("AssetCache/Common/"));
      EZ_TEST_BOOL(redirected.HasExtension(ezStringUtils::IsEqual(szSource, "Recovery.ezScene") ? "ezBinScene" : "ezBinPrefab"));
      ezStringBuilder destination(":scene-load/", redirected);
      {
        ezFileWriter file;
        EZ_TEST_BOOL(file.Open(destination).Succeeded());
        EZ_TEST_BOOL(file.WriteBytes(bytes.GetData(), bytes.GetCount()).Succeeded());
      }
      ezSceneLoadUtility loader;
      loader.StartSceneLoading(szSource, {});
      for (ezUInt32 i = 0; i < 100 && loader.GetLoadingState() == ezSceneLoadUtility::LoadingState::Ongoing; ++i)
        loader.TickSceneLoading();
      EZ_TEST_BOOL(loader.GetLoadingState() == ezSceneLoadUtility::LoadingState::FinishedSuccessfully);
    }
    return ezTestAppRun::Quit;
  }
};
static ezSceneLoadTest s_SceneLoadTest;
