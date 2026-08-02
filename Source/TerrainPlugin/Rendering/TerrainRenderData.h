#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererFoundation/RendererFoundationDLL.h>
#include <TerrainPlugin/TerrainPluginDLL.h>

using plMaterialResourceHandle = plTypedResourceHandle<class plMaterialResource>;

/// Render data submitted by plTerrainHeightfieldComponent each frame.
///
/// Carries the handles and parameters needed by plTerrainRenderer to issue the draw call.
/// The vertex shader generates terrain geometry from SV_VertexID using the height buffer as input.
class PL_TERRAINPLUGIN_DLL plTerrainRenderData : public plInstanceableRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plTerrainRenderData, plInstanceableRenderData);

public:
  /// Material asset that provides the terrain shader and texture/parameter bindings.
  plMaterialResourceHandle m_hMaterial;

  /// Persistent GPU buffer containing baked height values (float per vertex).
  /// Bound as SRV (StructuredBuffer<float>) in the vertex shader.
  plGALBufferHandle m_hHeightBuffer;

  /// Persistent GPU buffer containing packed XY normal components (uint per vertex).
  /// Bound as SRV (StructuredBuffer<uint>) in the vertex shader. Z is reconstructed from XY.
  plGALBufferHandle m_hNormalBuffer;

  /// Persistent GPU layer mask buffer (uint per vertex, RGBA8 packed: R=layer1, G=layer2, B=layer3).
  /// Bound as SRV (StructuredBuffer<uint>) in the vertex shader.
  plGALBufferHandle m_hMaskBuffer;

  /// Number of rendered quads per side (= plTerrainResolution enum value, e.g. 128).
  plUInt32 m_uiCellsPerSide = 128;

  /// World-space distance between adjacent vertices at full resolution.
  float m_fGridSpacing = 1.0f;
};

/// Render data for GPU-direct voxel mesh rendering.
///
/// Carries the GPU-side vertex and index buffer handles (StructuredBuffer SRVs written by surface nets shaders)
/// and a primitive count from the tiny VoxelMeshCounts readback. No CPU vertex/index data is needed.
class PL_TERRAINPLUGIN_DLL plTerrainVoxelGpuRenderData : public plInstanceableRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plTerrainVoxelGpuRenderData, plInstanceableRenderData);

public:
  plMaterialResourceHandle m_hMaterial;

  /// GPU vertex buffer (StructuredBuffer<VoxelGpuVertex>) written by surface nets shaders.
  plGALBufferHandle m_hGpuMeshVertices;

  /// GPU index buffer (StructuredBuffer<uint>) written by surface nets shaders.
  plGALBufferHandle m_hGpuMeshIndices;

  /// Indirect draw arguments (4 uints, ByteAddressBuffer, DrawIndirect-flagged). Written GPU-side
  /// in the same submission as the mesh, so the consumed primitive count always matches the buffer
  /// contents — no readback latency window. The primitive count never appears CPU-side in the
  /// rendering path.
  plGALBufferHandle m_hGpuMeshDrawArgs;

  plUInt32 m_uiBaseMaterialIndex = 0;
};
