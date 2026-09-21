#include <Foundation/FoundationPCH.h>

#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Utilities/AssetFileHeader.h>

static const char* g_szAssetTag = "ezAsset";

ezAssetFileHeader::ezAssetFileHeader() = default;

enum ezAssetFileHeaderVersion : ezUInt8
{
  Version1 = 1,
  Version2,
  Version3,

  VersionCount,
  VersionCurrent = VersionCount - 1
};

ezResult ezAssetFileHeader::Write(ezStreamWriter& inout_stream) const
{
  EZ_ASSERT_DEBUG(m_uiHash != 0xFFFFFFFFFFFFFFFF, "Cannot write an invalid hash to file");

  // 8 Bytes for identification + version
  EZ_SUCCEED_OR_RETURN(inout_stream.WriteBytes(g_szAssetTag, 7));

  const ezUInt8 uiVersion = ezAssetFileHeaderVersion::VersionCurrent;
  inout_stream << uiVersion;

  // 8 Bytes for the hash
  inout_stream << m_uiHash;
  // 2 for the type version
  inout_stream << m_uiVersion;

  inout_stream << m_sGenerator;
  return EZ_SUCCESS;
}

ezResult ezAssetFileHeader::Read(ezStreamReader& inout_stream)
{
  // initialize to 'invalid'
  m_uiHash = 0xFFFFFFFFFFFFFFFF;
  m_uiVersion = 0;
  m_sGenerator.Clear();

  char szTag[8] = {0};
  if (inout_stream.ReadBytes(szTag, 7) < 7)
  {
    return EZ_FAILURE;
  }

  szTag[7] = '\0';

  if (!ezStringUtils::IsEqual(szTag, g_szAssetTag))
    return EZ_FAILURE;

  ezUInt8 uiVersion = 0;
  if (inout_stream.ReadBytes(&uiVersion, sizeof(uiVersion)) != sizeof(uiVersion) ||
      uiVersion < Version1 || uiVersion > VersionCurrent)
    return EZ_FAILURE;

  ezUInt64 uiHash = 0;
  EZ_SUCCEED_OR_RETURN(inout_stream.ReadQWordValue(&uiHash));
  ezUInt16 uiTypeVersion = 0;

  if (uiVersion >= ezAssetFileHeaderVersion::Version2)
  {
    EZ_SUCCEED_OR_RETURN(inout_stream.ReadWordValue(&uiTypeVersion));
  }

  if (uiVersion >= ezAssetFileHeaderVersion::Version3)
  {
    // Asset headers precede serialized payloads (and their string-deduplication contexts).
    // Grow only for bytes actually present, never allocate an untrusted advertised string length.
    ezUInt32 uiLength = 0;
    EZ_SUCCEED_OR_RETURN(inout_stream.ReadDWordValue(&uiLength));
    ezHybridArray<char, 256> generator;
    while (uiLength > 0)
    {
      char buffer[256];
      const ezUInt32 uiChunk = ezMath::Min<ezUInt32>(uiLength, EZ_ARRAY_SIZE(buffer));
      if (inout_stream.ReadBytes(buffer, uiChunk) != uiChunk)
        return EZ_FAILURE;
      generator.PushBackRange(ezArrayPtr<const char>(buffer, uiChunk));
      uiLength -= uiChunk;
    }
    generator.PushBack('\0');
    // Unlike the debug-only string validation helper, file validation is required in every build.
    if (!utf8::is_valid(generator.GetData(), generator.GetData() + generator.GetCount() - 1))
      return EZ_FAILURE;
    m_sGenerator.Assign(ezStringView(generator.GetData(), generator.GetCount() - 1));
  }

  // Preserve the parsed type version for callers inspecting an outdated (but complete) header.
  m_uiVersion = uiTypeVersion;
  // older version? set the hash to 'invalid'
  if (uiVersion != ezAssetFileHeaderVersion::VersionCurrent)
    return EZ_FAILURE;

  m_uiHash = uiHash;

  return EZ_SUCCESS;
}
