#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <RendererCore/Pipeline/Renderer.h>
#include <TerrainPlugin/TerrainPluginDLL.h>

class plRenderDataBatch;

/// Renders terrain patches submitted as plTerrainRenderData.
///
/// Each patch is drawn as a procedural grid using SV_VertexID. No vertex buffer is needed;
/// the vertex shader reads height data from a structured buffer (SRV) bound per patch.
/// The shader and textures come from the material assigned to the patch component.
/// Auto-registered via reflection — no manual registration required.
class PL_TERRAINPLUGIN_DLL plTerrainRenderer : public plRenderer
{
  PL_ADD_DYNAMIC_REFLECTION(plTerrainRenderer, plRenderer);
  PL_DISALLOW_COPY_AND_ASSIGN(plTerrainRenderer);

public:
  plTerrainRenderer();
  ~plTerrainRenderer();

  virtual void GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const override;
  virtual void GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const override;
  virtual void RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const override;
};
