#pragma once

#include <RendererCore/Pipeline/Renderer.h>
#include <TerrainPlugin/TerrainPluginDLL.h>

/// Renders voxel mesh volumes directly from the GPU buffers produced by surface nets shaders.
///
/// No CPU vertex/index upload is required. The vertex shader reads VoxelGpuVertex structs from a
/// StructuredBuffer SRV and uses the index SRV to emulate indexed drawing, both indexed by SV_VertexID.
/// Draw call: DrawInstanced(PrimitiveCount * 3) using the count from the tiny VoxelMeshCounts readback.
class PL_TERRAINPLUGIN_DLL plTerrainVoxelRenderer : public plRenderer
{
  PL_ADD_DYNAMIC_REFLECTION(plTerrainVoxelRenderer, plRenderer);
  PL_DISALLOW_COPY_AND_ASSIGN(plTerrainVoxelRenderer);

public:
  plTerrainVoxelRenderer();
  ~plTerrainVoxelRenderer();

  virtual void GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const override;
  virtual void GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const override;
  void RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const override;
};
