#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Layout of the terrain data plTerrainSystem publishes for every view of its world (see TerrainSample.h).
//
// TerrainTable (Texture2D, RGBA float): row 0 is the header, row 1 + i describes heightfield slot i.
//   Header texel 0: x = number of slot rows, y = atlas size in texels, z = layer flags (TERRAIN_LAYERS_*), w = roughness bias.
//   Slot texels: TERRAIN_TABLE_* below.
//
// TerrainAtlas (Texture2DArray, RGBA float): slice i holds the inner (CellsPerSide + 1)^2 vertices of slot i.
//   x = baked height (local units), y = idx0 | idx1 << 8 | idx2 << 16, z = w0 | w1 << 8 | w2 << 16, w = unused.
//   Both packed values stay below 2^24, so they are exact in a float.
//
// TerrainLayerTable (Texture2D, RGBA float, TERRAIN_MAX_LAYERS x 1): x, y = texture array indices of the layer,
//   z = triplanar scale (the material's TextureScale times the layer's scale), w = unused.

#define TERRAIN_TABLE_ROWS 8
#define TERRAIN_TABLE_WORLD_TO_LOCAL 0 // texels 0..2: rows of the world to local 3x4 matrix
#define TERRAIN_TABLE_LOCAL_TO_WORLD 3 // texels 3..5: rows of the local to world 3x4 matrix
#define TERRAIN_TABLE_PARAMS 6         // x = cells per side, y = grid spacing, z = atlas slice, w = 1 when published
#define TERRAIN_TABLE_BOUNDS 7         // world-space XY rectangle: min x, min y, max x, max y

#define TERRAIN_MAX_LAYERS 32

#define TERRAIN_LAYERS_PRESENT 1
#define TERRAIN_LAYERS_STOCHASTIC 2

/// Push constants of TerrainPublishCS.
BEGIN_PUSH_CONSTANTS(TerrainPublishConstants)
{
  UINT1(VertexIdxPitch); ///< Entries per row of the baked buffers, including the 4-vertex border on each side.
  UINT1(VertexCount);    ///< Inner vertices per side (CellsPerSide + 1).
  UINT1(Slice);          ///< Atlas slice written.
}
END_PUSH_CONSTANTS(TerrainPublishConstants)
