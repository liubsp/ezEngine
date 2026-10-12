#include <GameEngineTest/GameEngineTestPCH.h>

#ifdef BUILDSYSTEM_ENABLE_VULKAN_SUPPORT

#  include <Foundation/Configuration/Singleton.h>
#  include <Foundation/IO/OSFile.h>
#  include <RendererFoundation/Device/Device.h>
#  include <RendererFoundation/Device/DeviceFactory.h>
#  include <RendererVulkan/Device/ezVulkanInitInterface.h>
#  include <TestFramework/Utilities/TestLogInterface.h>

namespace
{
  class UnavailableVulkanExtension : public ezVulkanInitInterface
  {
  public:
    explicit UnavailableVulkanExtension(bool bDeviceExtension)
      : m_bDeviceExtension(bDeviceExtension)
    {
    }

    vk::Instance CreateInstance(const vk::InstanceCreateInfo& createInfo) override { return nullptr; }
    vk::PhysicalDevice GetPhysicalDevice(vk::Instance instance) override { return nullptr; }
    vk::Device CreateDevice(const vk::DeviceCreateInfo& createInfo) override { return nullptr; }

    void ExtendInstanceExtensions(const ezDynamicArray<vk::ExtensionProperties>& availableExtensions, ezDynamicArray<ezString>& inout_extensions) override
    {
      if (!m_bDeviceExtension)
        inout_extensions.PushBack("VK_EZ_deliberately_unavailable_extension");
    }

    void ExtendDeviceExtensions(const ezDynamicArray<vk::ExtensionProperties>& availableExtensions, ezDynamicArray<ezString>& inout_extensions) override
    {
      if (m_bDeviceExtension)
        inout_extensions.PushBack("VK_EZ_deliberately_unavailable_extension");
    }

  private:
    bool m_bDeviceExtension;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST_GROUP(VulkanStartup);

EZ_CREATE_SIMPLE_TEST(VulkanStartup, InitializationFailure)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Unavailable instance and device extensions return failure and permit a fresh initialization")
  {
    for (bool bDeviceExtension : {false, true})
    {
      UnavailableVulkanExtension extensions(bDeviceExtension);
      ezSingletonRegistry::Register<ezVulkanInitInterface>(&extensions);
      EZ_SCOPE_EXIT(ezSingletonRegistry::Unregister<ezVulkanInitInterface>());
      ezGALDeviceCreationDescription description;
      ezGALDevice* pDevice = ezGALDeviceFactory::CreateDevice("Vulkan", ezFoundation::GetDefaultAllocator(), description);
      if (!EZ_TEST_BOOL(pDevice != nullptr))
        return;
      EZ_SCOPE_EXIT(EZ_DEFAULT_DELETE(pDevice));
      ezTestLogInterface log;
      ezTestLogSystemScope scope(&log, true);
      log.ExpectMessage("ErrorExtensionNotPresent", ezLogMsgType::ErrorMsg);
      try
      {
        EZ_TEST_BOOL(pDevice->Init().Failed());
      }
      catch (const vk::SystemError&)
      {
        EZ_TEST_BOOL_MSG(false, "Vulkan initialization threw instead of returning failure.");
      }
    }

    // Failed initialization must release its instance/device without leaving cache/pool state behind.
    ezGALDeviceCreationDescription description;
    description.m_sPipelineCacheDirectory = ezStringBuilder(ezTestFramework::GetInstance()->GetAbsOutputPath(), "/VulkanCache");
    if (ezOSFile::ExistsDirectory(description.m_sPipelineCacheDirectory) &&
        !EZ_TEST_BOOL(ezOSFile::DeleteFolder(description.m_sPipelineCacheDirectory).Succeeded()))
      return;
    ezGALDevice* pDevice = ezGALDeviceFactory::CreateDevice("Vulkan", ezFoundation::GetDefaultAllocator(), description);
    if (!EZ_TEST_BOOL(pDevice != nullptr))
      return;
    EZ_SCOPE_EXIT(EZ_DEFAULT_DELETE(pDevice));
    if (EZ_TEST_BOOL(pDevice->Init().Succeeded()))
    {
      EZ_TEST_BOOL(pDevice->Shutdown().Succeeded());
      ezFileSystemIterator cache;
      cache.StartSearch(ezStringBuilder(description.m_sPipelineCacheDirectory, "/PipelineCache_*.bin"), ezFileSystemIteratorFlags::ReportFiles);
      EZ_TEST_BOOL(cache.IsValid());
    }
  }
}

#endif
