#pragma once

#include <RendererFoundation/Device/Device.h>

// Deliberately has no graphics implementation. The counters distinguish renderer
// rejection before Init from a permitted device's initialization failure.
class StartupTestDevice : public ezGALDevice
{
public:
  StartupTestDevice(const ezGALDeviceCreationDescription& description, ezUInt32& inout_uiInitializations, ezUInt32& inout_uiDestructions,
    ezStringView sRenderer = "StartupTestRenderer")
    : ezGALDevice(description)
    , m_uiInitializations(inout_uiInitializations)
    , m_uiDestructions(inout_uiDestructions)
    , m_sRenderer(sRenderer)
  {
  }
  ~StartupTestDevice() override { ++m_uiDestructions; }
  const ezGALSharedTexture* GetSharedTexture(ezGALTextureHandle) const override { return nullptr; }

protected:
  ezResult InitPlatform() override
  {
    ++m_uiInitializations;
    return EZ_FAILURE;
  }
  ezResult ShutdownPlatform() override { return EZ_SUCCESS; }
  ezStringView GetRendererPlatform() override { return m_sRenderer; }
  ezGALCommandEncoder* BeginCommandsPlatform(const char*) override { return nullptr; }
  void EndCommandsPlatform(ezGALCommandEncoder*) override {}
  ezGALBlendState* CreateBlendStatePlatform(const ezGALBlendStateCreationDescription&) override { return nullptr; }
  void DestroyBlendStatePlatform(ezGALBlendState*) override {}
  ezGALDepthStencilState* CreateDepthStencilStatePlatform(const ezGALDepthStencilStateCreationDescription&) override { return nullptr; }
  void DestroyDepthStencilStatePlatform(ezGALDepthStencilState*) override {}
  ezGALRasterizerState* CreateRasterizerStatePlatform(const ezGALRasterizerStateCreationDescription&) override { return nullptr; }
  void DestroyRasterizerStatePlatform(ezGALRasterizerState*) override {}
  ezGALSamplerState* CreateSamplerStatePlatform(const ezGALSamplerStateCreationDescription&) override { return nullptr; }
  void DestroySamplerStatePlatform(ezGALSamplerState*) override {}
  void RecreateSamplerStatePlatform(ezGALSamplerState*) override {}
  ezGALBindGroupLayout* CreateBindGroupLayoutPlatform(const ezGALBindGroupLayoutCreationDescription&) override { return nullptr; }
  void DestroyBindGroupLayoutPlatform(ezGALBindGroupLayout*) override {}
  ezGALBindGroup* CreateBindGroupPlatform(const ezGALBindGroupCreationDescription&) override { return nullptr; }
  void DestroyBindGroupPlatform(ezGALBindGroup*) override {}
  void RecreateBindGroupPlatform(ezGALBindGroup*) override {}
  ezGALPipelineLayout* CreatePipelineLayoutPlatform(const ezGALPipelineLayoutCreationDescription&) override { return nullptr; }
  void DestroyPipelineLayoutPlatform(ezGALPipelineLayout*) override {}
  ezGALGraphicsPipeline* CreateGraphicsPipelinePlatform(const ezGALGraphicsPipelineCreationDescription&) override { return nullptr; }
  void DestroyGraphicsPipelinePlatform(ezGALGraphicsPipeline*) override {}
  ezGALComputePipeline* CreateComputePipelinePlatform(const ezGALComputePipelineCreationDescription&) override { return nullptr; }
  void DestroyComputePipelinePlatform(ezGALComputePipeline*) override {}
  ezGALShader* CreateShaderPlatform(const ezGALShaderCreationDescription&) override { return nullptr; }
  void DestroyShaderPlatform(ezGALShader*) override {}
  ezGALBuffer* CreateBufferPlatform(const ezGALBufferCreationDescription&, ezArrayPtr<const ezUInt8>) override { return nullptr; }
  void DestroyBufferPlatform(ezGALBuffer*) override {}
  ezGALTexture* CreateTexturePlatform(const ezGALTextureCreationDescription&, ezArrayPtr<ezGALSystemMemoryDescription>) override { return nullptr; }
  void DestroyTexturePlatform(ezGALTexture*) override {}
  ezGALTexture* CreateSharedTexturePlatform(const ezGALTextureCreationDescription&, ezArrayPtr<ezGALSystemMemoryDescription>, ezEnum<ezGALSharedTextureType>, ezGALPlatformSharedHandle) override { return nullptr; }
  void DestroySharedTexturePlatform(ezGALTexture*) override {}
  ezGALReadbackBuffer* CreateReadbackBufferPlatform(const ezGALBufferCreationDescription&) override { return nullptr; }
  void DestroyReadbackBufferPlatform(ezGALReadbackBuffer*) override {}
  ezGALReadbackTexture* CreateReadbackTexturePlatform(const ezGALTextureCreationDescription&) override { return nullptr; }
  void DestroyReadbackTexturePlatform(ezGALReadbackTexture*) override {}
  ezGALRenderTargetView* CreateRenderTargetViewPlatform(ezGALTexture*, const ezGALRenderTargetViewCreationDescription&) override { return nullptr; }
  void DestroyRenderTargetViewPlatform(ezGALRenderTargetView*) override {}
  ezGALVertexDeclaration* CreateVertexDeclarationPlatform(const ezGALVertexDeclarationCreationDescription&) override { return nullptr; }
  void DestroyVertexDeclarationPlatform(ezGALVertexDeclaration*) override {}
  void UpdateBufferForNextFramePlatform(const ezGALBuffer*, ezConstByteArrayPtr, ezUInt32) override {}
  void UpdateTextureForNextFramePlatform(const ezGALTexture*, const ezGALSystemMemoryDescription&, const ezGALTextureSubresource&, const ezBoundingBoxu32&) override {}
  ezEnum<ezGALAsyncResult> GetTimestampResultPlatform(ezGALTimestampHandle, ezTime&) override { return ezGALAsyncResult::Expired; }
  ezEnum<ezGALAsyncResult> GetOcclusionResultPlatform(ezGALOcclusionHandle, ezUInt64&) override { return ezGALAsyncResult::Expired; }
  ezEnum<ezGALAsyncResult> GetFenceResultPlatform(ezGALFenceHandle, ezTime) override { return ezGALAsyncResult::Pending; }
  ezResult LockBufferPlatform(const ezGALReadbackBuffer*, ezArrayPtr<const ezUInt8>&) const override { return EZ_FAILURE; }
  void UnlockBufferPlatform(const ezGALReadbackBuffer*) const override {}
  ezResult LockTexturePlatform(const ezGALReadbackTexture*, const ezArrayPtr<const ezGALTextureSubresource>&, ezDynamicArray<ezGALSystemMemoryDescription>&) const override { return EZ_FAILURE; }
  void UnlockTexturePlatform(const ezGALReadbackTexture*, const ezArrayPtr<const ezGALTextureSubresource>&) const override {}
  void BeginFramePlatform(ezArrayPtr<ezGALSwapChain*>, ezUInt64) override {}
  void EndFramePlatform(ezArrayPtr<ezGALSwapChain*>) override {}
  ezUInt64 GetCurrentFramePlatform() const override { return 0; }
  ezUInt64 GetSafeFramePlatform() const override { return 0; }
  void FillCapabilitiesPlatform() override {}
  void FlushPlatform() override {}
  void WaitIdlePlatform() override {}

private:
  ezUInt32& m_uiInitializations;
  ezUInt32& m_uiDestructions;
  ezString m_sRenderer;
};
