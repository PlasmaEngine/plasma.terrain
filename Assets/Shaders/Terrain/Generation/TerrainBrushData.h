#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

/// Brush data uploaded to the GPU once per bake and indexed by BrushCount in the push constants.
/// Shared between heightfield and voxel generation shaders.
struct PL_SHADER_STRUCT TerrainBrushData
{
  FLOAT3(Position); // should be in relative space, so could use low precision
  PACKEDHALF2(HalfExtentX, HalfExtentYBottom, PackedExtents);

  FLOAT3(InvRotRow0);
  PACKEDHALF2(InnerRadius, OuterRadius, PackedRadii);

  FLOAT3(InvRotRow1);
  PACKEDHALF2(MaterialStrength, Falloff, PackedMatStrFalloff);

  FLOAT3(InvRotRow2);
  PACKEDUINT8(ModifyMode, MaterialIndex, HalfExtentZLow, HalfExtentZHigh, PackedModeMat);

  FLOAT1(Noise);          // could be half
  FLOAT1(NoiseScale);     // could be half
  FLOAT1(HalfExtentYTop); // upper Y half-size for Carve/Add asymmetric tunnels; 0 = taper to a point at top
  FLOAT1(Priority);       // CPU-side sort key only; not read by shaders
};

#define plTerrainModifyMode_Max       0
#define plTerrainModifyMode_Min       1
#define plTerrainModifyMode_Set       2
#define plTerrainModifyMode_Carve     3
#define plTerrainModifyMode_Add       4
#define plTerrainModifyMode_OnlyPaint2D 5
#define plTerrainModifyMode_OnlyPaint3D 6
