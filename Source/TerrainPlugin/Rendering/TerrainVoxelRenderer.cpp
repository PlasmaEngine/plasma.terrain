#include <TerrainPlugin/TerrainPluginPCH.h>

#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Pipeline/RenderDataBatch.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/DynamicBuffer.h>
#include <Shaders/Terrain/Rendering/VoxelMeshRenderConstants.h>
#include <TerrainPlugin/Rendering/TerrainRenderData.h>
#include <TerrainPlugin/Rendering/TerrainVoxelRenderer.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plTerrainVoxelGpuRenderData, 1, plRTTIDefaultAllocator<plTerrainVoxelGpuRenderData>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plTerrainVoxelRenderer, 1, plRTTIDefaultAllocator<plTerrainVoxelRenderer>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plTerrainVoxelRenderer::plTerrainVoxelRenderer() = default;
plTerrainVoxelRenderer::~plTerrainVoxelRenderer() = default;

void plTerrainVoxelRenderer::GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& inout_types) const
{
  inout_types.PushBack(plGetStaticRTTI<plTerrainVoxelGpuRenderData>());
}

void plTerrainVoxelRenderer::GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& inout_categories) const
{
  inout_categories.PushBack(plDefaultRenderDataCategories::LitOpaque);
  inout_categories.PushBack(plDefaultRenderDataCategories::LitForwardOpaque);
}

void plTerrainVoxelRenderer::RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const
{
  plRenderContext* pContext = renderViewContext.m_pRenderContext;

  const bool bAsync = pContext->GetAllowAsyncShaderLoading();
  pContext->SetAllowAsyncShaderLoading(false);
  PL_SCOPE_EXIT(pContext->SetAllowAsyncShaderLoading(bAsync));

  for (auto it = batch.GetIterator<plTerrainVoxelGpuRenderData>(0, batch.GetDataCount()); it.IsValid(); ++it)
  {
    const plTerrainVoxelGpuRenderData* pRenderData = it;

    if (pRenderData->m_hGpuMeshVertices.IsInvalidated() || pRenderData->m_hGpuMeshIndices.IsInvalidated() || pRenderData->m_hGpuMeshDrawArgs.IsInvalidated() || !pRenderData->m_hMaterial.IsValid())
      continue;

    plResourceLock<plMaterialResource> pMaterial(pRenderData->m_hMaterial, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
    if (pMaterial.GetAcquireResult() != plResourceAcquireResult::Final)
      continue;

    // Material provides the shader (VoxelMeshMaterial.plShader) and any texture bindings.
    pContext->BindMaterial(pRenderData->m_hMaterial);

    // Bind GPU vertex + index buffers as SRVs.
    // The VS reads: index = VoxelIndices[SV_VertexID], then vertex = VoxelVertices[index].
    plBindGroupBuilder& bindGroup = pContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);
    bindGroup.BindBuffer("VoxelVertices", pRenderData->m_hGpuMeshVertices);
    bindGroup.BindBuffer("VoxelIndices", pRenderData->m_hGpuMeshIndices);

    // Bind instance data buffer so the shader can read the world transform and GameObjectID.
    plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
    if (auto* pInstanceDataBuffer = pDevice->GetDynamicBuffer(pRenderData->m_hInstanceDataBuffer))
    {
      bindGroup.BindBuffer("perInstanceData", pInstanceDataBuffer->GetBufferForRendering());
    }

    VoxelMeshRenderConstants constants;
    constants.InstanceDataOffset = pRenderData->m_DataOffsets.m_uiInstance;
    constants.BaseMaterialIndex = pRenderData->m_uiBaseMaterialIndex;
    pContext->SetPushConstants("VoxelMeshRenderConstants", constants);

    // Non-indexed draw: SV_VertexID drives index + vertex lookup.
    // Vertex count is read from the GPU-side indirect args buffer (filled in the same submission as the
    // mesh), so the count never lags the buffer contents — avoids the corrupted-triangle flash that the
    // CPU-readback path used to produce on the frame after each rebuild.
    pContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, 1);
    if (pContext->ApplyContextStates().Succeeded())
    {
      pContext->GetCommandEncoder()->DrawInstancedIndirect(pRenderData->m_hGpuMeshDrawArgs, 0).IgnoreResult();
    }
  }
}

PL_STATICLINK_FILE(TerrainPlugin, TerrainPlugin_Rendering_TerrainVoxelRenderer);
