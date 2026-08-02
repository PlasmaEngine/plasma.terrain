#include <TerrainPlugin/TerrainPluginPCH.h>

#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Pipeline/RenderDataBatch.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/DynamicBuffer.h>
#include <Shaders/Terrain/Rendering/HeightfieldRenderConstants.h>
#include <TerrainPlugin/Rendering/TerrainRenderData.h>
#include <TerrainPlugin/Rendering/TerrainRenderer.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plTerrainRenderData, 1, plRTTIDefaultAllocator<plTerrainRenderData>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plTerrainRenderer, 1, plRTTIDefaultAllocator<plTerrainRenderer>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plTerrainRenderer::plTerrainRenderer() = default;
plTerrainRenderer::~plTerrainRenderer() = default;

void plTerrainRenderer::GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& inout_types) const
{
  inout_types.PushBack(plGetStaticRTTI<plTerrainRenderData>());
}

void plTerrainRenderer::GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& inout_categories) const
{
  inout_categories.PushBack(plDefaultRenderDataCategories::LitOpaque);
  inout_categories.PushBack(plDefaultRenderDataCategories::LitForwardOpaque);
}

void plTerrainRenderer::RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const
{
  plRenderContext* pContext = renderViewContext.m_pRenderContext;

  for (auto it = batch.GetIterator<plTerrainRenderData>(0, batch.GetDataCount()); it.IsValid(); ++it)
  {
    const plTerrainRenderData* pRenderData = it;

    if (pRenderData->m_hHeightBuffer.IsInvalidated() || !pRenderData->m_hMaterial.IsValid())
      continue;

    // Shader and textures come from the material asset assigned to the component.
    pContext->BindMaterial(pRenderData->m_hMaterial);

    // Bind height and normal buffers as SRVs — the shader reads them by vertex index
    plBindGroupBuilder& bindGroup = pContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);
    bindGroup.BindBuffer("TerrainHeights", pRenderData->m_hHeightBuffer);
    bindGroup.BindBuffer("TerrainNormals", pRenderData->m_hNormalBuffer);
    bindGroup.BindBuffer("TerrainMask", pRenderData->m_hMaskBuffer);

    // Bind instance data buffer so the pixel shader can read GameObjectID for picking.
    plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
    if (auto* pInstanceDataBuffer = pDevice->GetDynamicBuffer(pRenderData->m_hInstanceDataBuffer))
    {
      bindGroup.BindBuffer("perInstanceData", pInstanceDataBuffer->GetBufferForRendering());
    }

    const plUInt32 uiStoredSize = pRenderData->m_uiCellsPerSide + 9;

    HeightfieldRenderConstants constants;
    constants.GridSpacing = pRenderData->m_fGridSpacing;
    constants.FirstVertexIdx = 4 * uiStoredSize + 4; // skip 4 border rows and 4 border cols
    constants.VertexIdxPitch = uiStoredSize;
    constants.CellsPerSide = pRenderData->m_uiCellsPerSide;
    constants.InstanceDataOffset = pRenderData->m_DataOffsets.m_uiInstance;
    pContext->SetPushConstants("HeightfieldRenderConstants", constants);

    // No vertex buffer — SV_VertexID drives everything.
    const plUInt32 uiPrimitiveCount = pRenderData->m_uiCellsPerSide * pRenderData->m_uiCellsPerSide * 2;
    pContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, uiPrimitiveCount);

    pContext->DrawMeshBuffer().IgnoreResult();
  }
}

PL_STATICLINK_FILE(TerrainPlugin, TerrainPlugin_Rendering_TerrainRenderer);