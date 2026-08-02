#pragma once

#include <Core/World/WorldModule.h>
#include <Foundation/Types/TagSet.h>
#include <GameEngine/Utils/ImageDataResource.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/ReadbackHelper.h>
#include <TerrainPlugin/TerrainPluginDLL.h>

// Dual C++/HLSL headers — included here for TerrainBrushData and VoxelGpuVertex/VoxelMeshCounts
#include <Shaders/Terrain/Generation/TerrainBrushData.h>
#include <Shaders/Terrain/Generation/VoxelMeshConstants.h>

struct PL_TERRAINPLUGIN_DLL plTerrainModifyMode
{
  using StorageType = plUInt8;
  enum Enum : plUInt8
  {
    Max = 0,         ///< Only raises terrain toward the brush height.
    Min = 1,         ///< Only lowers terrain toward the brush height.
    Set = 2,         ///< Forces terrain to the brush height regardless of current value.
    Carve = 3,       ///< Clears volume inside the rounded-box footprint (HalfExtentX/Y + InnerRadius corner). No falloff. Voxels: removes solid cells. Heightfield: marks vertices with sentinel material index 0xFF; render shader culls any triangle that touches a marked vertex (cave entrances / holes).
    Add = 4,         ///< Voxels: fills solid cells inside the 3D rounded-box volume. Heightfield: no geometry change; paints material using 3D SDF.
    OnlyPaint2D = 5, ///< No geometry change. Paints material using 2D SDF projection (2D brush style). Used by ezTerrainBrush2DComponent.
    OnlyPaint3D = 6, ///< No geometry change. Paints material using 3D SDF (3D brush style). Used by ezTerrainBrush3DComponent.

    Default = Max,
  };
};

/// Quad count per side of a terrain patch. The value of each enumerator equals the number of quads.
/// Vertex count per side = value + 1. Stored buffer width = value + 9 (4 extra rings on each side).
struct PL_TERRAINPLUGIN_DLL plTerrainResolution
{
  using StorageType = plUInt16;
  enum Enum : plUInt16
  {
    Res32 = 32,
    Res64 = 64,
    Res128 = 128,
    Res256 = 256,
    Res512 = 512,
    Default = Res128,
  };
};
PL_DECLARE_REFLECTABLE_TYPE(PL_TERRAINPLUGIN_DLL, plTerrainResolution);

struct plTerrainData_PointBrush
{
  bool m_bInUse = false;
  bool m_bAffectHeightfields = true;
  bool m_bAffectVoxels = true;
  plUInt8 m_uiMaterialIndex = 0;  ///< Material (layer) index to paint: 0-3.
  /// If non-empty, the brush only affects terrain objects that have at least one matching tag.
  plTagSet m_IncludeTags;
  plEnum<plTerrainModifyMode> m_eModifyMode;
  plVec3 m_vPosition = plVec3::MakeZero();
  plQuat m_qRotation = plQuat::MakeIdentity(); ///< World-space rotation of the brush.
  plVec2 m_vHalfExtents = plVec2::MakeZero();  ///< Half-size of the straight rectangular region along each brush axis.
  float m_fHalfExtentZ = 0.0f;                 ///< Half-size along the brush Z axis. Used only by Carve/Add mode for a 3D rounded-box volume.
  float m_fHalfExtentYTop = 0.0f;              ///< Upper Y half-size for asymmetric Carve/Add volumes (e.g. flat-floor tunnels). 0 = same as m_vHalfExtents.y (symmetric).
  float m_fInnerRadius = 0.0f;                 ///< Corner rounding radius of the inner (full-weight) region.
  float m_fOuterRadius = 5.0f;                 ///< Corner rounding radius; also the outer edge of the falloff zone.
  float m_fFalloff = 1.0f;                     ///< Exponent applied after smoothstep in the transition zone.
  float m_fMaterialStrength = 0.0f;            ///< Blend weight for material painting in [0, 1]. 0 = disabled (no material write).
  float m_fNoise = 0.0f;
  float m_fNoiseScale = 1.0f;
  plInt8 m_iPriority = 0; ///< Sort order. Higher = applied later = wins over lower-priority brushes. Equal priorities use mode-based ordering.
};

struct plTerrainData_Heightfield
{
  bool m_bInUse = false;
  plTransform m_GlobalTransform = plTransform::MakeIdentity();
  plGALBufferHandle m_hBakedHeights;  ///< Persistent, bound as UAV in heights CS and SRV in VS.
  plGALBufferHandle m_hBakedNormals;  ///< Persistent, bound as UAV in normals CS and SRV in VS.
  plGALBufferHandle m_hBakedMask;     ///< Persistent material mask buffer (uint2 per vertex). UAV in mask CS, SRV in VS.
  plImageDataResourceHandle m_hHeightImage; ///< Optional greyscale image used as baseline height source; sampled each bake.
  plVec2 m_vImageOffset = plVec2::MakeZero(); ///< UV offset into m_hHeightImage; selects the top-left corner of the sampled rect.
  plVec2 m_vImageSize = plVec2(1.0f);         ///< UV extent of the sampled rect within m_hHeightImage. Values < 1 select a sub-region.
  float m_fHeightScale = 0.0f;              ///< Multiplier applied to the [0, 1] greyscale sample to produce a world-space height.
  /// CPU-side source material mask; (CellsPerSide+9)^2 packed uint64 entries.
  /// Each entry: bits [7:0]=idx0, [15:8]=idx1, [23:16]=idx2, [31:24]=w0, [39:32]=w1, [47:40]=w2.
  /// Starts all-zero; brushes paint into it via the bake CS.
  plDynamicArray<plUInt64> m_MaskData;
  plUInt32 m_uiCellsPerSide = 128; ///< Number of rendered quads per side (= resolution enum value).
  float m_fGridSpacing = 1.0f;
  /// Material index used to fill brush-unpainted weight in the normals CS.
  plUInt8 m_uiDefaultMaterialIndex = 0;
  bool m_bDirty = true;
  plTagSet m_Tags; ///< Identity tags for this heightfield; matched against brush include-tag filters.
  /// Hash of the indices of brushes that spatially overlap this heightfield.
  /// Updated each time brush state is re-evaluated; compared against the previous value to skip
  /// re-bakes when the set of relevant brushes (and whether any are present) has not changed.
  plUInt64 m_uiBrushOverlapHash = 0;

  /// Set by plTerrainHeightfieldComponent when collision is enabled and the world is simulating.
  bool m_bNeedsColliderReadback = false;
  /// True while a GPU readback of m_hBakedHeights is in flight.
  bool m_bReadbackPending = false;
  /// Set to true once a readback has completed and m_CpuBakedHeights is fresh.
  bool m_bColliderDataReady = false;
  /// Set to true after the component consumes readback data; cleared when terrain is re-baked.
  bool m_bColliderUpToDate = false;
  /// Baked heights for physics (uiJoltN*uiJoltN, where uiJoltN = ceil4(CellsPerSide+1)) copied from GPU after readback.
  plDynamicArray<float> m_CpuBakedHeights;
  /// Dominant material indices for physics (same uiJoltN*uiJoltN grid) from the last readback.
  plDynamicArray<plUInt8> m_CpuDominantMaterialIndices;
  plGALReadbackBufferHelper* m_pHeightReadback = nullptr;
  plGALReadbackBufferHelper* m_pMaskReadback = nullptr;
};

struct plTerrainData_Voxel
{
  bool m_bInUse = false;
  bool m_bDirty = true;
  plTransform m_GlobalTransform = plTransform::MakeIdentity();
  plUInt32 m_uiResolution = 32;       ///< Logical voxels per side as requested by the component; used for bounds and UVs.
  plUInt32 m_uiStoredResolution = 32; ///< m_uiResolution rounded up to the next multiple of 8; equals the inner triangulated cell count per axis.
  plUInt32 m_uiVoxelResolution = 40;  ///< Total voxel buffer dimension: (m_uiStoredResolution + 2) rounded up to the next multiple of 8. Includes at least 1 border voxel on each side.
  float m_fVoxelSize = 1.0f;          ///< World-space size of one voxel.

  /// GPU buffer: M³/8 packed uint32s where M = m_uiVoxelResolution. UAV in bake CS, SRV in mesh build CS.
  plGALBufferHandle m_hBakedVoxels;

  /// GPU buffer: M³ floats (unpacked). Per-voxel signed distance written by VoxelBakeCS, read by VoxelSurfaceNetsPass1CS
  /// to place vertices along edges via SDF interpolation. Negative inside solid, positive in air.
  plGALBufferHandle m_hBakedVoxelDist;

  /// Scratch buffer, same layout as m_hBakedVoxelDist. VoxelBlurDistCS writes blurred distances here;
  /// the surface nets passes then read from this buffer instead of the raw bake output.
  plGALBufferHandle m_hBakedVoxelDistScratch;

  /// GPU mesh output buffers — written by surface nets shaders.
  plGALBufferHandle m_hGpuMeshVertices;        ///< RWStructuredBuffer<VoxelGpuVertex>, (N+1)³ entries where N = m_uiStoredResolution, pre-indexed by cell. Cells 0..N cover [0, N*GridSpacing] flush on both sides; cells 0 and N use border voxels for corner samples.

  plGALBufferHandle m_hGpuMeshIndices;         ///< RWStructuredBuffer<uint>, (N+1)³*18 entries worst-case.
  plGALBufferHandle m_hGpuMeshCounts;          ///< RWStructuredBuffer<VoxelMeshCounts>, 1 entry.
  /// Indirect draw arguments buffer (ByteAddressBuffer + DrawIndirect): 4 uints {VertexCount, InstanceCount=1, StartVertex=0, StartInstance=0}.
  /// Filled by VoxelFillDrawArgsCS from m_hGpuMeshCounts after each rebuild. Consumed by DrawInstancedIndirect so the GPU-side primitive
  /// count is always in sync with the GPU mesh buffers — independent of the async CPU readback.
  plGALBufferHandle m_hGpuMeshDrawArgs;

  /// CPU copies after GPU mesh readback.
  plDynamicArray<VoxelGpuVertex, plAlignedAllocatorWrapper> m_CpuMeshVertices;
  plDynamicArray<plUInt32> m_CpuMeshIndices;
  plUInt32 m_uiCpuMeshVertexCount = 0;
  plUInt32 m_uiCpuMeshPrimitiveCount = 0;

  /// 0 = all cells start as air, 1 = all cells start as solid. Applied before brushes each bake.
  plUInt32 m_uiInitialSolid = 0;
  /// World-space Z height below which voxels start as solid (when m_uiInitialSolid != 0).
  float m_fFillHeight = 0.0f;
  bool m_bMeshDataReady = false;          ///< Set when collision readback (vertex+index+counts) is complete and physics needs rebuild.
  bool m_bGpuMeshReady = false;           ///< CPU-side flag: set true after the first bake's dispatches were submitted. Indirect rendering can begin — the args buffer is GPU-filled by the same submission.
  bool m_bNeedsCollisionReadback = false; ///< Component sets this to request a full vertex+index readback for collision.
  bool m_bCollisionReadbackPending = false;
  plGALReadbackBufferHelper* m_pMeshVertexReadback = nullptr;
  plGALReadbackBufferHelper* m_pMeshIndexReadback = nullptr;
  plGALReadbackBufferHelper* m_pMeshCountReadback = nullptr;
  plTagSet m_Tags; ///< Identity tags for this voxel volume; matched against brush include-tag filters.
  plUInt64 m_uiBrushOverlapHash = 0;
};

/// Manages GPU resources and compute shader dispatch for all terrain patches in a world.
///
/// Each plTerrainHeightfieldComponent registers itself here on activation and receives a patch index.
/// When heights are modified, the component marks the patch dirty. During the BeginRender event
/// the compute shader re-bakes the height buffer for all dirty patches before the main pipeline renders.
class PL_TERRAINPLUGIN_DLL plTerrainSystem : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plTerrainSystem, plWorldModule);

  using SUPER = plWorldModule;

public:
  plTerrainSystem(plWorld* pWorld);
  ~plTerrainSystem();

  plUInt32 CreateHeightfield(plUInt32 uiCellsPerSide);
  void RemoveHeightfield(plUInt32& uiPatchIndex);
  plTerrainData_Heightfield& ModifyHeightfield(plUInt32 uiIdx);

  /// Returns the baked GPU height buffer handle (SRV in VS, UAV in heights CS).
  plGALBufferHandle GetHeightBuffer(plUInt32 uiPatchIndex) const;

  /// Returns the baked GPU normal buffer handle (SRV in VS, UAV in normals CS).
  plGALBufferHandle GetNormalBuffer(plUInt32 uiPatchIndex) const;

  /// Returns the baked GPU layer mask buffer handle (SRV in VS, UAV in mask CS).
  plGALBufferHandle GetMaskBuffer(plUInt32 uiPatchIndex) const;

  /// Returns the number of quads per side (= resolution enum value) for the given patch.
  plUInt32 GetCellsPerSide(plUInt32 uiPatchIndex) const;

  /// Enables or disables GPU readback for physics collision on this patch.
  /// Does NOT mark the patch dirty; call this every frame to keep the flag current.
  void SetNeedsColliderReadback(plUInt32 uiPatchIndex, bool bEnable);

  /// True if a readback completed since the last ClearColliderDataReady() call.
  bool IsColliderDataReady(plUInt32 uiPatchIndex) const;

  /// Returns the inner N*N CPU-side baked heights from the last completed readback.
  plArrayPtr<const float> GetCpuBakedHeights(plUInt32 uiPatchIndex) const;

  /// Returns the inner N*N dominant material indices (slot 0 after weight-sort) from the last completed readback.
  plArrayPtr<const plUInt8> GetCpuBakedMaterialIndices(plUInt32 uiPatchIndex) const;

  /// Clears the m_bColliderDataReady flag after the caller has consumed the data.
  void ClearColliderDataReady(plUInt32 uiPatchIndex);

  plUInt32 CreatePointBrush();
  void RemovePointBrush(plUInt32& uiIdx);

  const plTerrainData_PointBrush& ReadPointBrush(plUInt32 uiIdx) const;
  plTerrainData_PointBrush& ModifyPointBrush(plUInt32 uiIdx);

  /// Allocates GPU buffer and readback helper for a new voxel volume. Returns a slot index.
  plUInt32 CreateVoxelVolume(plUInt32 uiResolution, float fVoxelSize);
  void RemoveVoxelVolume(plUInt32 uiIndex);
  plTerrainData_Voxel& ModifyVoxelVolume(plUInt32 uiIndex);

  bool IsVoxelMeshDataReady(plUInt32 uiIndex) const;
  void ClearVoxelMeshDataReady(plUInt32 uiIndex);

  plArrayPtr<const VoxelGpuVertex> GetCpuMeshVertices(plUInt32 uiIndex) const;
  plArrayPtr<const plUInt32> GetCpuMeshIndices(plUInt32 uiIndex) const;
  plUInt32 GetCpuMeshVertexCount(plUInt32 uiIndex) const;
  plUInt32 GetCpuMeshPrimitiveCount(plUInt32 uiIndex) const;

  /// True once the counts-only readback completed; GPU-direct rendering is then possible.
  bool IsGpuMeshReady(plUInt32 uiIndex) const;

  /// Returns the GPU vertex buffer (StructuredBuffer SRV) for the given voxel volume.
  plGALBufferHandle GetGpuMeshVertexBuffer(plUInt32 uiIndex) const;

  /// Returns the GPU index buffer (StructuredBuffer SRV) for the given voxel volume.
  plGALBufferHandle GetGpuMeshIndexBuffer(plUInt32 uiIndex) const;

  /// Returns the indirect draw arguments buffer for the given voxel volume.
  plGALBufferHandle GetGpuMeshDrawArgsBuffer(plUInt32 uiIndex) const;

  /// When enabled, the next bake will also read back vertex + index data for collision mesh building.
  void SetNeedsCollisionReadback(plUInt32 uiIndex, bool bEnable);

protected:
  virtual void Initialize() override;
  virtual void Deinitialize() override;

private:
  static void OnRenderEvent(const plRenderWorldRenderEvent& e);

  void InternalRemoveHeightfield(plUInt32& uiPatchIndex);

  void UpdateTerrain();
  void UpdateHeightfield(plUInt32 uiIndex, plRenderContext* pRenderContext, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder);
  void ProcessPendingHeightfieldReadbacks();
  void FrameCleanup();

  /// Returns a hash over the indices of all brushes whose footprint overlaps the heightfield's XY extent.
  /// Returns 0 when no brush overlaps. Used to detect changes in the set of relevant brushes per patch.
  plUInt64 ComputeHeightfieldOverlapHash(const plTerrainData_Heightfield& heightfield) const;

  void FindOverlappingBrushes(const plTerrainData_Heightfield& heightfield, plDynamicArray<TerrainBrushData>& brushes) const;
  plGALBufferHandle CreateBrushBuffer(plDynamicArray<TerrainBrushData>& brushes, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder) const;

  bool m_bPointBrushesDirty = true;
  bool m_bHeightfieldsDirty = true;

  plMutex m_Mutex;
  plDeque<plTerrainData_PointBrush> m_PointBrushes;
  plHybridArray<plUInt32, 16> m_QueuedHeightfieldsToDelete;

  plDynamicArray<plTerrainData_Heightfield> m_Patches;
  plShaderResourceHandle m_hTerrainBakeStep1Shader;
  plShaderResourceHandle m_hTerrainBakeStep2Shader;

  void UpdateVoxels(plUInt32 uiIndex, plRenderContext* pRenderContext, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder);
  void ProcessPendingMeshReadbacks();
  plUInt64 ComputeVoxelOverlapHash(const plTerrainData_Voxel& vol) const;

  plDynamicArray<plTerrainData_Voxel> m_VoxelVolumes;
  plHybridArray<plUInt32, 8> m_QueuedVoxelVolumesToDelete;
  plShaderResourceHandle m_hVoxelBakeShader;
  plShaderResourceHandle m_hVoxelMeshClearShader;
  plShaderResourceHandle m_hVoxelSurfaceNetsPass1Shader;
  plShaderResourceHandle m_hVoxelSurfaceNetsPass2Shader;
  plShaderResourceHandle m_hVoxelBlurDistShader;
  plShaderResourceHandle m_hVoxelCleanupShader;
  plShaderResourceHandle m_hVoxelFillDrawArgsShader;

  static plDeque<plTerrainSystem*> s_TerrainSystems;
};
