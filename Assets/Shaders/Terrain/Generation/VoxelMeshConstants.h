#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

/// Vertex written by the surface nets shaders into the mesh vertex buffer.
/// Layout must be identical in both HLSL (StructuredBuffer element) and C++ (after readback).
/// Total size: 56 bytes. Material == 0xFFFFFFFF indicates an inactive (no edge crossing) cell slot.
struct PL_SHADER_STRUCT VoxelGpuVertex
{
  FLOAT3(Position);
  UINT1(Material); ///< Dominant material index (0-15). Value 0xFFFFFFFF = inactive cell sentinel.

  FLOAT3(Normal);
  FLOAT1(MaterialStrength); ///< Blend weight for the per-vertex override material (0 = main material, 1 = full override).

  FLOAT3(Tangent);
  FLOAT1(BitangentSign);

  FLOAT2(TexCoord);
  FLOAT2(Pad1);
};

/// Atomic counter output by the surface nets face shader.
/// VertexCount is not stored here; the vertex buffer is pre-indexed at (N-1)³ entries.
/// Padded to 16 bytes so sizeof matches alignas(16) PL_SHADER_STRUCT in C++.
struct PL_SHADER_STRUCT VoxelMeshCounts
{
  UINT1(PrimitiveCount); ///< Triangle count (2 per surface quad).
  UINT1(Pad0);
  UINT1(Pad1);
  UINT1(Pad2);
};

