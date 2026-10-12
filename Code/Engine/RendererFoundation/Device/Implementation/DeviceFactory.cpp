#include <RendererFoundation/RendererFoundationPCH.h>

#include <RendererFoundation/Device/DeviceFactory.h>

struct CreatorFuncInfo
{
  ezGALDeviceFactory::CreatorFunc m_Func;
  ezString m_sShaderModel;
  ezString m_sShaderCompiler;
};

static ezHashTable<ezString, CreatorFuncInfo> s_CreatorFuncs;

CreatorFuncInfo* GetCreatorFuncInfo(ezStringView sRendererName)
{
  auto pFuncInfo = s_CreatorFuncs.GetValue(sRendererName);
  if (pFuncInfo == nullptr)
  {
    ezStringBuilder sPluginName = "ezRenderer";
    sPluginName.Append(sRendererName);

    if (ezPlugin::LoadPlugin(sPluginName).Failed())
      return nullptr;

    pFuncInfo = s_CreatorFuncs.GetValue(sRendererName);
    if (pFuncInfo == nullptr)
      ezLog::Error("Renderer '{}' is not registered by plugin '{}'.", sRendererName, sPluginName);
  }

  return pFuncInfo;
}

ezInternal::NewInstance<ezGALDevice> ezGALDeviceFactory::CreateDevice(ezStringView sRendererName, ezAllocator* pAllocator, const ezGALDeviceCreationDescription& desc)
{
  if (auto pFuncInfo = GetCreatorFuncInfo(sRendererName))
  {
    return pFuncInfo->m_Func(pAllocator, desc);
  }

  return ezInternal::NewInstance<ezGALDevice>(nullptr, pAllocator);
}

void ezGALDeviceFactory::GetShaderModelAndCompiler(ezStringView sRendererName, const char*& ref_szShaderModel, const char*& ref_szShaderCompiler)
{
  if (auto pFuncInfo = GetCreatorFuncInfo(sRendererName))
  {
    ref_szShaderModel = pFuncInfo->m_sShaderModel;
    ref_szShaderCompiler = pFuncInfo->m_sShaderCompiler;
  }
}

void ezGALDeviceFactory::RegisterCreatorFunc(const char* szRendererName, const CreatorFunc& func, const char* szShaderModel, const char* szShaderCompiler)
{
  CreatorFuncInfo funcInfo;
  funcInfo.m_Func = func;
  funcInfo.m_sShaderModel = szShaderModel;
  funcInfo.m_sShaderCompiler = szShaderCompiler;

  EZ_VERIFY(s_CreatorFuncs.Insert(szRendererName, funcInfo) == false, "Creator func already registered");
}

void ezGALDeviceFactory::UnregisterCreatorFunc(const char* szRendererName)
{
  EZ_VERIFY(s_CreatorFuncs.Remove(szRendererName), "Creator func not registered");
}
