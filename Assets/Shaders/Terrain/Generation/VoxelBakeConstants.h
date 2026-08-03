#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

/// Push constants shared by all voxel generation compute shaders:
/// VoxelBakeCS, VoxelBlurDistCS, VoxelCleanupCS, VoxelSurfaceNetsPass1CS, VoxelSurfaceNetsPass2CS.
/// Not all fields are read by every shader — each shader ignores unused constants.
BEGIN_PUSH_CONSTANTS(VoxelBakeConstants)
{
  FLOAT1(GridSpacing);     ///< World-space size of one voxel.
  UINT1(VertexIdxPitch);   ///< Inner voxel resolution (logical cells per axis, = StoredResolution). Used for vertex indexing in surface nets.
  UINT1(VoxelResolution);  ///< Total voxel buffer dimension including border layers (= VertexIdxPitch + 8). Always a multiple of 8.
  UINT1(BrushCount);       ///< VoxelBakeCS only: number of valid entries in the Brushes structured buffer.
  UINT1(InitialSolid);     ///< VoxelBakeCS only: 0 = start as air, 1 = start as solid below FillHeight.
  FLOAT1(FillHeight);      ///< VoxelBakeCS only: local-space Z threshold for the initial solid/air split (only when InitialSolid != 0).
  UINT1(EdgeAxis);         ///< VoxelSurfaceNetsPass2CS only: which edge axis to process — 0 = X, 1 = Y, 2 = Z.
  FLOAT1(SmoothFactor);    ///< VoxelBlurDistCS only: blend weight between original and averaged distance (0 = passthrough, 1 = full 3³ average).
  FLOAT1(PatchOriginX);    ///< VoxelBakeCS only: world-space X of the volume origin. Used to sample noise in absolute world space.
  FLOAT1(PatchOriginY);    ///< VoxelBakeCS only: world-space Y of the volume origin.
  FLOAT1(PatchOriginZ);    ///< VoxelBakeCS only: world-space Z of the volume origin.
}
END_PUSH_CONSTANTS(VoxelBakeConstants)
