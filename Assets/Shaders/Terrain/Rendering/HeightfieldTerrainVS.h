#pragma once

/// Vertex shader helpers for the heightfield terrain. Include this in the [VERTEXSHADER] section.
///
/// Requires: USE_WORLDPOS and USE_DATAOFFSETS defined before this file is included (they are
/// consumed by the MaterialInterpolator.h that this file includes).
/// Declares FillHeightfieldTerrainVertexOutput(uint vertexID).

#include <Shaders/Common/GlobalConstants.h>
#include <Shaders/Common/ObjectConstants.h>
#include <Shaders/Materials/MaterialInterpolator.h>
#include <Shaders/Terrain/Rendering/HeightfieldRenderConstants.h>

/// Baked heights produced by the compute shader, one float per grid vertex.
StructuredBuffer<float> TerrainHeights BIND_GROUP(BG_DRAW_CALL);

/// Per-vertex packed normals from TerrainNormalsCS: XY as 16-bit floats in a uint32.
StructuredBuffer<uint> TerrainNormals BIND_GROUP(BG_DRAW_CALL);

/// Baked material mask. Index 0xFF in slot 0 is the carve sentinel: the VS outputs NaN positions
/// for those vertices so any triangle touching them is culled, opening a hole in the heightfield.
StructuredBuffer<uint2> TerrainMask BIND_GROUP(BG_DRAW_CALL);

/// Decode a packed terrain normal (XY as f16, Z reconstructed).
float3 DecodeTerrainNormal(uint packed)
{
  float nx = f16tof32(packed & 0xFFFFu);
  float ny = f16tof32(packed >> 16u);
  float nz = sqrt(max(0.0, 1.0 - nx * nx - ny * ny));
  return float3(nx, ny, nz);
}

VS_OUT FillHeightfieldTerrainVertexOutput(uint vertexID)
{
  const uint cellsPerSide = GET_PUSH_CONSTANT(HeightfieldRenderConstants, CellsPerSide);
  const uint firstVertexIdx = GET_PUSH_CONSTANT(HeightfieldRenderConstants, FirstVertexIdx);
  const uint vertexIdxPitch = GET_PUSH_CONSTANT(HeightfieldRenderConstants, VertexIdxPitch);
  const uint instanceDataOffset = GET_PUSH_CONSTANT(HeightfieldRenderConstants, InstanceDataOffset);
  const float gridSpacing = GET_PUSH_CONSTANT(HeightfieldRenderConstants, GridSpacing);

  // Decode cell and corner from vertexID.
  // Each cell (quad) is rendered as 2 triangles = 6 vertices.
  // Triangle 0: corners (0,0),(1,0),(0,1)
  // Triangle 1: corners (1,0),(1,1),(0,1)
  static const uint2 QuadOffsets[6] =
  {
    {0, 0}, {1, 0}, {0, 1},
    {1, 0}, {1, 1}, {0, 1},
  };

  const uint cellIndex  = vertexID / 6;
  const uint vertInCell = vertexID % 6;

  const uint cellX = cellIndex % cellsPerSide;
  const uint cellY = cellIndex / cellsPerSide;

  // lx/ly: 0-based vertex coordinates within the render grid.
  const uint lx = cellX + QuadOffsets[vertInCell].x;
  const uint ly = cellY + QuadOffsets[vertInCell].y;

  const uint idx = firstVertexIdx + ly * vertexIdxPitch + lx;

  // Carve sentinel: NaN position. Spec requires triangles with NaN positions to be culled, so any triangle
  // that touches a sentinel-marked vertex disappears, opening a hole in the heightfield.
  if ((TerrainMask[idx].x & 0xFFu) == 0xFFu)
  {
    VS_OUT Discarded = (VS_OUT)0;
    const float fNaN = asfloat(0x7FC00000u);
    Discarded.Position = float4(fNaN, fNaN, fNaN, fNaN);
    return Discarded;
  }

  float h = TerrainHeights[idx];
  float3 localNormal = DecodeTerrainNormal(TerrainNormals[idx]);

  // Apply the object-to-world transform so the patch respects rotation and scale.
  const plPerInstanceData instanceData = perInstanceData[instanceDataOffset];
  const float4x4 objectToWorld = TransformToMatrix(instanceData.ObjectToWorld);
  const float3x3 objectToWorldNormal = TransformToRotation(instanceData.ObjectToWorldNormal);

  const float3 localPos = float3((float)lx * gridSpacing, (float)ly * gridSpacing, h);

  // Analytical tangent for a heightfield: lies in the XZ plane, perpendicular to the normal.
  // Derived from dP/du = (GridSpacing, 0, dH/dx), orthogonalized: float3(n.z, 0, -n.x).
  const float3 localTangent   = normalize(float3(localNormal.z, 0.0, -localNormal.x));
  const float3 localBitangent = cross(localNormal, localTangent);

  const float3 worldPos       = mul(objectToWorld, float4(localPos, 1.0f)).xyz;
  const float3 worldNormal    = normalize(mul(objectToWorldNormal, localNormal));
  const float3 worldTangent   = normalize(mul(objectToWorldNormal, localTangent));
  const float3 worldBitangent = normalize(mul(objectToWorldNormal, localBitangent));

  VS_OUT Output;
  Output.Position      = mul(GetWorldToScreenMatrix(), float4(worldPos, 1.0f));

#if defined(USE_VELOCITY)
  // Last frame's position uses last frame's camera and object transform; the heights are this frame's.
  const float4x4 lastObjectToWorld = TransformToMatrix(instanceData.LastObjectToWorld);
  const float3 lastWorldPos = mul(lastObjectToWorld, float4(localPos, 1.0f)).xyz;
  Output.ScreenPosition     = Output.Position;
  Output.LastScreenPosition = mul(GetLastWorldToScreenMatrix(), float4(lastWorldPos, 1.0f));
#endif

  Output.WorldPosition = worldPos;
  Output.Normal        = worldNormal;
  Output.Tangent       = worldTangent;
  Output.BiTangent     = worldBitangent;
  Output.TexCoord0     = float2((float)lx / (float)cellsPerSide,
                                (float)ly / (float)cellsPerSide);
  Output.DataOffsets   = uint3(instanceDataOffset, 0, 0);
  return Output;
}
