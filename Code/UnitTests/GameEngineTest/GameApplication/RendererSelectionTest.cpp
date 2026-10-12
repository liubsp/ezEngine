#include <GameEngineTest/GameEngineTestPCH.h>

#include <Foundation/Configuration/Plugin.h>
#include <GameEngine/GameApplication/GameApplication.h>
#include <GameEngineTest/GameApplication/StartupTestDevice.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Device/DeviceFactory.h>
#include <TestFramework/Utilities/TestLogInterface.h>

EZ_CREATE_SIMPLE_TEST_GROUP(RendererSelection);

namespace
{
  class StartupApplication : public ezGameApplication
  {
  public:
    StartupApplication()
      : ezGameApplication("RendererSelection", nullptr)
    {
    }
    using ezGameApplication::Init_SetupGraphicsDevice;
  };
} // namespace

EZ_CREATE_SIMPLE_TEST(RendererSelection, CustomCreator)
{
  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Custom device rejection or initialization failure deletes the device and fails startup")
  {
    const bool bAllowed = ezGALDeviceFactory::IsRendererAllowed("StartupTestRenderer");

    StartupApplication app;

    ezUInt32 uiInitializations = 0;
    ezUInt32 uiDestructions = 0;
    ezGameApplication::SetOverrideDefaultDeviceCreator(
      [&](const ezGALDeviceCreationDescription& description) -> ezGALDevice*
      {
        return EZ_DEFAULT_NEW(StartupTestDevice, description, uiInitializations, uiDestructions);
      });
    EZ_SCOPE_EXIT(ezGameApplication::SetOverrideDefaultDeviceCreator({}));
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage(bAllowed ? "Graphics device creation failed" : "requires renderer", ezLogMsgType::ErrorMsg);
    app.Init_SetupGraphicsDevice();
    EZ_TEST_BOOL(app.ShouldApplicationQuit());
    EZ_TEST_INT(app.GetReturnCode(), 1);
    EZ_TEST_BOOL(!ezGALDevice::HasDefaultDevice());
    EZ_TEST_INT(uiInitializations, bAllowed ? 1 : 0);
    EZ_TEST_INT(uiDestructions, 1);
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "A permitted device initialization failure is propagated without publishing a default device")
  {
    StartupApplication app;

    ezUInt32 uiInitializations = 0;
    ezUInt32 uiDestructions = 0;
    ezStringView sRenderer = ezGALDeviceFactory::GetRequiredRenderer();
    if (sRenderer.IsEmpty())
      sRenderer = "StartupTestRenderer";
    ezGameApplication::SetOverrideDefaultDeviceCreator(
      [&](const ezGALDeviceCreationDescription& description) -> ezGALDevice*
      {
        return EZ_DEFAULT_NEW(StartupTestDevice, description, uiInitializations, uiDestructions, sRenderer);
      });
    EZ_SCOPE_EXIT(ezGameApplication::SetOverrideDefaultDeviceCreator({}));
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage("Graphics device creation failed", ezLogMsgType::ErrorMsg);
    app.Init_SetupGraphicsDevice();
    EZ_TEST_BOOL(app.ShouldApplicationQuit());
    EZ_TEST_INT(app.GetReturnCode(), 1);
    EZ_TEST_BOOL(!ezGALDevice::HasDefaultDevice());
    EZ_TEST_INT(uiInitializations, 1);
    EZ_TEST_INT(uiDestructions, 1);
  }
}

EZ_CREATE_SIMPLE_TEST(RendererSelection, FactoryConstraint)
{
  const ezStringView sRequired = ezGALDeviceFactory::GetRequiredRenderer();
#ifdef BUILDSYSTEM_REQUIRED_RENDERER
  EZ_TEST_STRING(sRequired, BUILDSYSTEM_REQUIRED_RENDERER);
#else
  EZ_TEST_BOOL(sRequired.IsEmpty());
#endif

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Registered selection preserves unconstrained behavior but cannot bypass the constraint")
  {
    bool bCalled = false;
    const char* szOtherRenderer = sRequired == "TestRenderer" ? "OtherTestRenderer" : "TestRenderer";
    ezGALDeviceFactory::RegisterCreatorFunc(szOtherRenderer, [&bCalled](ezAllocator* pAllocator, const ezGALDeviceCreationDescription&) -> ezInternal::NewInstance<ezGALDevice>
      {
        bCalled = true;
        return ezInternal::NewInstance<ezGALDevice>(nullptr, pAllocator); }, "TestShaderModel", "TestShaderCompiler");
    EZ_SCOPE_EXIT(ezGALDeviceFactory::UnregisterCreatorFunc(szOtherRenderer));

    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    if (!sRequired.IsEmpty())
      log.ExpectMessage("requires renderer", ezLogMsgType::ErrorMsg, 2);

    ezGALDeviceCreationDescription description;
    ezGALDevice* pDevice = ezGALDeviceFactory::CreateDevice(szOtherRenderer, ezFoundation::GetDefaultAllocator(), description);
    EZ_TEST_BOOL(pDevice == nullptr);
    EZ_TEST_BOOL(bCalled == sRequired.IsEmpty());
    const char* szShaderModel = "stale model";
    const char* szShaderCompiler = "stale compiler";
    ezGALDeviceFactory::GetShaderModelAndCompiler(szOtherRenderer, szShaderModel, szShaderCompiler);
    EZ_TEST_STRING(szShaderModel, sRequired.IsEmpty() ? "TestShaderModel" : "");
    EZ_TEST_STRING(szShaderCompiler, sRequired.IsEmpty() ? "TestShaderCompiler" : "");
  }

  EZ_TEST_BLOCK(ezTestBlock::Enabled, "Rejected names never reach plugin loading")
  {
    if (sRequired.IsEmpty())
      return;

    ezUInt32 uiPluginEvents = 0;
    // Use a real plugin name so stale generated output cannot bypass the pre-load check.
    const char* szUnregisteredRenderer = sRequired == "DX11" ? "OtherUnregisteredRenderer" : "DX11";
    auto subscription = ezPlugin::Events().AddEventHandler([&uiPluginEvents](const ezPluginEvent&)
      { ++uiPluginEvents; });
    EZ_SCOPE_EXIT(ezPlugin::Events().RemoveEventHandler(subscription));
    ezTestLogInterface log;
    ezTestLogSystemScope scope(&log, true);
    log.ExpectMessage("requires renderer", ezLogMsgType::ErrorMsg, 2);
    const char* szShaderModel = "stale";
    const char* szShaderCompiler = "stale";
    ezGALDeviceCreationDescription description;
    ezGALDevice* pDevice = ezGALDeviceFactory::CreateDevice(szUnregisteredRenderer, ezFoundation::GetDefaultAllocator(), description);
    EZ_TEST_BOOL(pDevice == nullptr);
    ezGALDeviceFactory::GetShaderModelAndCompiler(szUnregisteredRenderer, szShaderModel, szShaderCompiler);
    EZ_TEST_STRING(szShaderModel, "");
    EZ_TEST_STRING(szShaderCompiler, "");
    EZ_TEST_INT(uiPluginEvents, 0);
  }
}
