#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/World/World.h>
#include <Foundation/Algorithm/HashStream.h>
#include <Foundation/Math/Float16.h>
#include <GameEngine/Utils/ImageDataResource.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/Buffer.h>
#include <RendererFoundation/Resources/ReadbackHelper.h>
#include <Shaders/Terrain/Generation/HeightfieldBakeConstants.h>
#include <Shaders/Terrain/Generation/VoxelBakeConstants.h>
#include <TerrainPlugin/Components/TerrainBrushBaseComponent.h>
#include <TerrainPlugin/TerrainSystem.h>
#include <Texture/Image/ImageUtils.h>

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plTerrainSystem);

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plTerrainSystem, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_ENUM(plTerrainResolution, 1)
  PL_ENUM_CONSTANT(plTerrainResolution::Res32),
  PL_ENUM_CONSTANT(plTerrainResolution::Res64),
  PL_ENUM_CONSTANT(plTerrainResolution::Res128),
  PL_ENUM_CONSTANT(plTerrainResolution::Res256),
  PL_ENUM_CONSTANT(plTerrainResolution::Res512),
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on

plDeque<plTerrainSystem*> plTerrainSystem::s_TerrainSystems;

plTerrainSystem::plTerrainSystem(plWorld* pWorld)
  : SUPER(pWorld)
{
}

plTerrainSystem::~plTerrainSystem() = default;

void plTerrainSystem::Initialize()
{
  SUPER::Initialize();

  m_hTerrainBakeStep1Shader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/HeightfieldTerrainBakeStep1CS.plShader");
  m_hTerrainBakeStep2Shader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/HeightfieldTerrainBakeStep2CS.plShader");
  m_hVoxelBakeShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelBakeCS.plShader");
  m_hVoxelMeshClearShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelMeshClearCS.plShader");
  m_hVoxelSurfaceNetsPass1Shader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelSurfaceNetsPass1CS.plShader");
  m_hVoxelSurfaceNetsPass2Shader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelSurfaceNetsPass2CS.plShader");
  m_hVoxelBlurDistShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelBlurDistCS.plShader");
  m_hVoxelCleanupShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelCleanupCS.plShader");
  m_hVoxelFillDrawArgsShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Terrain/Generation/VoxelFillDrawArgsCS.plShader");

  if (s_TerrainSystems.IsEmpty())
  {
    plRenderWorld::GetRenderEvent().AddEventHandler(OnRenderEvent);
  }

  s_TerrainSystems.PushBack(this);
}

void plTerrainSystem::Deinitialize()
{
  FrameCleanup();

  s_TerrainSystems.RemoveAndSwap(this);

  if (s_TerrainSystems.IsEmpty())
  {
    plRenderWorld::GetRenderEvent().RemoveEventHandler(OnRenderEvent);
  }

  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
  for (auto& patch : m_Patches)
  {
    if (!patch.m_hBakedHeights.IsInvalidated())
    {
      pDevice->DestroyBuffer(patch.m_hBakedHeights);
    }

    if (!patch.m_hBakedNormals.IsInvalidated())
    {
      pDevice->DestroyBuffer(patch.m_hBakedNormals);
    }

    if (!patch.m_hBakedMask.IsInvalidated())
    {
      pDevice->DestroyBuffer(patch.m_hBakedMask);
    }
  }
  m_Patches.Clear();

  for (auto& vol : m_VoxelVolumes)
  {
    if (!vol.m_hBakedVoxels.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hBakedVoxels);
    if (!vol.m_hBakedVoxelDist.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hBakedVoxelDist);
    if (!vol.m_hBakedVoxelDistScratch.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hBakedVoxelDistScratch);
    if (!vol.m_hGpuMeshVertices.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hGpuMeshVertices);
    if (!vol.m_hGpuMeshIndices.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hGpuMeshIndices);
    if (!vol.m_hGpuMeshCounts.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hGpuMeshCounts);
    if (!vol.m_hGpuMeshDrawArgs.IsInvalidated())
      pDevice->DestroyBuffer(vol.m_hGpuMeshDrawArgs);
    PL_DEFAULT_DELETE(vol.m_pMeshVertexReadback);
    PL_DEFAULT_DELETE(vol.m_pMeshIndexReadback);
    PL_DEFAULT_DELETE(vol.m_pMeshCountReadback);
  }
  m_VoxelVolumes.Clear();

  SUPER::Deinitialize();
}


plUInt32 plTerrainSystem::CreateHeightfield(plUInt32 uiCellsPerSide)
{
  PL_PROFILE_SCOPE("CreateHeightfield");

  // Find a free slot first
  plUInt32 uiIndex = plInvalidIndex;
  for (plUInt32 i = 0; i < m_Patches.GetCount(); ++i)
  {
    if (!m_Patches[i].m_bInUse)
    {
      uiIndex = i;
      break;
    }
  }

  if (uiIndex == plInvalidIndex)
  {
    uiIndex = m_Patches.GetCount();
    m_Patches.ExpandAndGetRef();
  }

  plTerrainData_Heightfield& patch = m_Patches[uiIndex];
  patch.m_uiCellsPerSide = uiCellsPerSide;
  patch.m_bInUse = true;
  patch.m_bDirty = true;

  // Stored grid = render vertices (CellsPerSide+1) plus 4 extra rings on each side.
  // Extra rings let normals CS use correct central-differences at patch edges,
  // and provide enough padding for Jolt's required multiple-of-4 vertex count.
  const plUInt32 uiStoredSize = uiCellsPerSide + 9;
  // Material mask starts all-zero; brushes paint into it, NormalsCS fills the rest with the default material.
  patch.m_MaskData.SetCount(uiStoredSize * uiStoredSize, 0ull);

  // Create persistent GPU buffer (UAV for CS, SRV in VS)
  plGALBufferCreationDescription bufDesc;
  bufDesc.m_uiStructSize = sizeof(float);
  bufDesc.m_uiTotalSize = uiStoredSize * uiStoredSize * sizeof(float);
  bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource | plGALBufferUsageFlags::UnorderedAccess;
  bufDesc.m_ResourceAccess.m_bImmutable = false;

  patch.m_hBakedHeights = plGALDevice::GetDefaultDevice()->CreateBuffer(bufDesc);

  // Create persistent normal buffer (uint per vertex: XY as packed 16-bit floats)
  plGALBufferCreationDescription normDesc;
  normDesc.m_uiStructSize = sizeof(plUInt32);
  normDesc.m_uiTotalSize = uiStoredSize * uiStoredSize * sizeof(plUInt32);
  normDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource | plGALBufferUsageFlags::UnorderedAccess;
  normDesc.m_ResourceAccess.m_bImmutable = false;

  patch.m_hBakedNormals = plGALDevice::GetDefaultDevice()->CreateBuffer(normDesc);

  // Create persistent material mask buffer (uint2 per vertex: 3 indices + 3 weights packed into 8 bytes)
  plGALBufferCreationDescription maskDesc;
  maskDesc.m_uiStructSize = sizeof(plUInt64);
  maskDesc.m_uiTotalSize = uiStoredSize * uiStoredSize * sizeof(plUInt64);
  maskDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource | plGALBufferUsageFlags::UnorderedAccess;
  maskDesc.m_ResourceAccess.m_bImmutable = false;

  patch.m_hBakedMask = plGALDevice::GetDefaultDevice()->CreateBuffer(maskDesc);

  patch.m_pHeightReadback = PL_DEFAULT_NEW(plGALReadbackBufferHelper);
  patch.m_pMaskReadback = PL_DEFAULT_NEW(plGALReadbackBufferHelper);

  return uiIndex;
}

void plTerrainSystem::RemoveHeightfield(plUInt32& uiIdx)
{
  PL_LOCK(m_Mutex);
  m_QueuedHeightfieldsToDelete.PushBack(uiIdx);
}

void plTerrainSystem::InternalRemoveHeightfield(plUInt32& uiIdx)
{
  if (uiIdx == plInvalidIndex)
    return;

  auto& data = m_Patches[uiIdx];
  uiIdx = plInvalidIndex;

  if (!data.m_hBakedHeights.IsInvalidated())
  {
    plGALDevice::GetDefaultDevice()->DestroyBuffer(data.m_hBakedHeights);
    data.m_hBakedHeights.Invalidate();
  }

  if (!data.m_hBakedNormals.IsInvalidated())
  {
    plGALDevice::GetDefaultDevice()->DestroyBuffer(data.m_hBakedNormals);
    data.m_hBakedNormals.Invalidate();
  }

  if (!data.m_hBakedMask.IsInvalidated())
  {
    plGALDevice::GetDefaultDevice()->DestroyBuffer(data.m_hBakedMask);
    data.m_hBakedMask.Invalidate();
  }

  data.m_MaskData.Clear();
  data.m_CpuBakedHeights.Clear();
  data.m_CpuDominantMaterialIndices.Clear();

  PL_DEFAULT_DELETE(data.m_pHeightReadback);
  PL_DEFAULT_DELETE(data.m_pMaskReadback);

  data.m_bInUse = false;
  data.m_bNeedsColliderReadback = false;
  data.m_bReadbackPending = false;
  data.m_bColliderDataReady = false;
  data.m_bColliderUpToDate = false;
}

plTerrainData_Heightfield& plTerrainSystem::ModifyHeightfield(plUInt32 uiIdx)
{
  m_bHeightfieldsDirty = true;

  auto& data = m_Patches[uiIdx];
  data.m_bDirty = true;
  return data;
}

plGALBufferHandle plTerrainSystem::GetHeightBuffer(plUInt32 uiPatchIndex) const
{
  if (uiPatchIndex < m_Patches.GetCount() && m_Patches[uiPatchIndex].m_bInUse)
    return m_Patches[uiPatchIndex].m_hBakedHeights;

  return plGALBufferHandle();
}

plGALBufferHandle plTerrainSystem::GetNormalBuffer(plUInt32 uiPatchIndex) const
{
  if (uiPatchIndex < m_Patches.GetCount() && m_Patches[uiPatchIndex].m_bInUse)
    return m_Patches[uiPatchIndex].m_hBakedNormals;

  return plGALBufferHandle();
}

plGALBufferHandle plTerrainSystem::GetMaskBuffer(plUInt32 uiPatchIndex) const
{
  if (uiPatchIndex < m_Patches.GetCount() && m_Patches[uiPatchIndex].m_bInUse)
    return m_Patches[uiPatchIndex].m_hBakedMask;

  return plGALBufferHandle();
}

plUInt32 plTerrainSystem::GetCellsPerSide(plUInt32 uiPatchIndex) const
{
  return (uiPatchIndex < m_Patches.GetCount()) ? m_Patches[uiPatchIndex].m_uiCellsPerSide : 128;
}

plUInt32 plTerrainSystem::CreatePointBrush()
{
  for (plUInt32 idx = 0; idx < m_PointBrushes.GetCount(); ++idx)
  {
    if (!m_PointBrushes[idx].m_bInUse)
    {
      m_PointBrushes[idx].m_bInUse = true;
      return idx;
    }
  }

  m_PointBrushes.ExpandAndGetRef().m_bInUse = true;
  return m_PointBrushes.GetCount() - 1;
}

void plTerrainSystem::RemovePointBrush(plUInt32& uiIdx)
{
  if (uiIdx == plInvalidIndex)
    return;

  PL_ASSERT_DEV(uiIdx < m_PointBrushes.GetCount(), "Invalid brush index");

  m_bPointBrushesDirty = true;
  m_PointBrushes[uiIdx].m_bInUse = false;
  uiIdx = plInvalidIndex;
}

const plTerrainData_PointBrush& plTerrainSystem::ReadPointBrush(plUInt32 uiIdx) const
{
  return m_PointBrushes[uiIdx];
}

plTerrainData_PointBrush& plTerrainSystem::ModifyPointBrush(plUInt32 uiIdx)
{
  m_bPointBrushesDirty = true;
  return m_PointBrushes[uiIdx];
}

void plTerrainSystem::OnRenderEvent(const plRenderWorldRenderEvent& e)
{
  if (e.m_Type != plRenderWorldRenderEvent::Type::BeginRender)
    return;

  for (auto* pTerrain : s_TerrainSystems)
  {
    pTerrain->UpdateTerrain();
  }
}

void plTerrainSystem::UpdateTerrain()
{
  FrameCleanup();
  ProcessPendingHeightfieldReadbacks();

  bool bAnyDirty = false;
  bool bAnyReadbackNeeded = false;
  for (const auto& patch : m_Patches)
  {
    if (!patch.m_bInUse)
      continue;

    if (patch.m_bDirty)
    {
      bAnyDirty = true;
    }

    // A readback is needed when requested, not yet in flight, data not ready, and collider not already current.
    if (patch.m_bNeedsColliderReadback && !patch.m_bReadbackPending && !patch.m_bColliderDataReady && !patch.m_bColliderUpToDate)
    {
      bAnyReadbackNeeded = true;
    }
  }

  for (const auto& vol : m_VoxelVolumes)
  {
    if (!vol.m_bInUse)
      continue;

    if (vol.m_bDirty)
      bAnyDirty = true;

    if (vol.m_bCollisionReadbackPending)
      bAnyReadbackNeeded = true;

    if (vol.m_bNeedsCollisionReadback && vol.m_bGpuMeshReady && !vol.m_bCollisionReadbackPending)
      bAnyReadbackNeeded = true;
  }

  if (!bAnyDirty && !m_bPointBrushesDirty && !bAnyReadbackNeeded)
    return;

  PL_PROFILE_SCOPE("UpdateTerrain");

  plRenderContext* pRenderContext = plRenderContext::GetDefaultInstance();
  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();

  // Don't allow async shader loading — we need the shader ready now
  const bool bPrevAsync = pRenderContext->GetAllowAsyncShaderLoading();
  pRenderContext->SetAllowAsyncShaderLoading(false);
  PL_SCOPE_EXIT(pRenderContext->SetAllowAsyncShaderLoading(bPrevAsync));

  plGALCommandEncoder* pCommandEncoder = pDevice->BeginCommands("TerrainBake");

  pRenderContext->BeginCompute("TerrainBake");

  for (plUInt32 i = 0; i < m_Patches.GetCount(); ++i)
  {
    plTerrainData_Heightfield& patch = m_Patches[i];

    if (!patch.m_bInUse)
      continue;

    bool bShouldBake = patch.m_bDirty;

    if (m_bPointBrushesDirty)
    {
      const plUInt64 uiNewHash = ComputeHeightfieldOverlapHash(patch);
      if (uiNewHash != patch.m_uiBrushOverlapHash)
        bShouldBake = true;
      patch.m_uiBrushOverlapHash = uiNewHash;
    }

    if (bShouldBake)
    {
      // Re-bake; readback is triggered inside UpdateHeightfield if needed.
      UpdateHeightfield(i, pRenderContext, pDevice, pCommandEncoder);
    }
    else if (patch.m_bNeedsColliderReadback && !patch.m_bReadbackPending && !patch.m_bColliderDataReady && !patch.m_bColliderUpToDate)
    {
      // Terrain is already clean but a collider readback was requested (e.g. collider enabled after bake).
      // Issue a standalone readback without re-baking.
      patch.m_pHeightReadback->ReadbackBuffer(*pCommandEncoder, patch.m_hBakedHeights);
      if (patch.m_pMaskReadback != nullptr)
        patch.m_pMaskReadback->ReadbackBuffer(*pCommandEncoder, patch.m_hBakedMask);
      patch.m_bReadbackPending = true;
      patch.m_bColliderDataReady = false;
    }
  }

  // Voxel volumes
  for (plUInt32 i = 0; i < m_VoxelVolumes.GetCount(); ++i)
  {
    plTerrainData_Voxel& vol = m_VoxelVolumes[i];
    if (!vol.m_bInUse)
      continue;

    bool bShouldBake = vol.m_bDirty;
    plUInt64 uiNewHash = vol.m_uiBrushOverlapHash;
    if (m_bPointBrushesDirty)
    {
      uiNewHash = ComputeVoxelOverlapHash(vol);
      if (uiNewHash != vol.m_uiBrushOverlapHash)
        bShouldBake = true;
    }

    if (bShouldBake)
    {
      vol.m_uiBrushOverlapHash = uiNewHash;
      UpdateVoxels(i, pRenderContext, pDevice, pCommandEncoder);
    }
    else if (vol.m_bNeedsCollisionReadback && vol.m_bGpuMeshReady && !vol.m_bCollisionReadbackPending)
    {
      // Mesh is already baked; issue a standalone collision readback (counts + verts + idxs) without re-baking.
      vol.m_pMeshCountReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshCounts);
      vol.m_pMeshVertexReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshVertices);
      vol.m_pMeshIndexReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshIndices);
      vol.m_bCollisionReadbackPending = true;
    }
  }

  pRenderContext->EndCompute();
  pDevice->EndCommands(pCommandEncoder);

  ProcessPendingMeshReadbacks();
  m_bPointBrushesDirty = false;
}

plUInt64 plTerrainSystem::ComputeHeightfieldOverlapHash(const plTerrainData_Heightfield& heightfield) const
{
  const plTransform invTrans = heightfield.m_GlobalTransform.GetInverse();
  const float fSize = (float)heightfield.m_uiCellsPerSide * heightfield.m_fGridSpacing;

  plHashStreamWriter64 writer;

  for (plUInt32 i = 0; i < m_PointBrushes.GetCount(); ++i)
  {
    const auto& brush = m_PointBrushes[i];
    if (!brush.m_bInUse || !brush.m_bAffectHeightfields)
      continue;

    if (!brush.m_IncludeTags.IsEmpty() && !brush.m_IncludeTags.IsAnySet(heightfield.m_Tags))
      continue;

    const plVec3 vLocalCenter = invTrans * brush.m_vPosition;
    const float fConservativeRadius = plMath::Sqrt(brush.m_vHalfExtents.x * brush.m_vHalfExtents.x + brush.m_vHalfExtents.y * brush.m_vHalfExtents.y) + brush.m_fInnerRadius + brush.m_fOuterRadius;

    const float fClampedX = plMath::Clamp(vLocalCenter.x, 0.0f, fSize);
    const float fClampedY = plMath::Clamp(vLocalCenter.y, 0.0f, fSize);
    const float fDx = vLocalCenter.x - fClampedX;
    const float fDy = vLocalCenter.y - fClampedY;

    if (fDx * fDx + fDy * fDy > fConservativeRadius * fConservativeRadius)
      continue;

    writer << i;
    writer << brush.m_vPosition.x << brush.m_vPosition.y << brush.m_vPosition.z;
    writer << brush.m_qRotation.x << brush.m_qRotation.y << brush.m_qRotation.z << brush.m_qRotation.w;
    writer << brush.m_vHalfExtents.x << brush.m_vHalfExtents.y;
    writer << brush.m_fHalfExtentZ << brush.m_fHalfExtentYTop;
    writer << brush.m_fInnerRadius << brush.m_fOuterRadius << brush.m_fFalloff;
    writer << brush.m_eModifyMode.GetValue();
    writer << brush.m_uiMaterialIndex << brush.m_fMaterialStrength;
    writer << brush.m_fNoise;
    writer << brush.m_fNoiseScale;
    writer << brush.m_iPriority;
    brush.m_IncludeTags.Save(writer);
  }

  writer << heightfield.m_uiDefaultMaterialIndex;
  heightfield.m_Tags.Save(writer);

  return writer.GetHashValue();
}

void plTerrainSystem::FindOverlappingBrushes(const plTerrainData_Heightfield& heightfield, plDynamicArray<TerrainBrushData>& brushes) const
{
  const plTransform invTrans = heightfield.m_GlobalTransform.GetInverse();
  const float fSize = (float)heightfield.m_uiCellsPerSide * heightfield.m_fGridSpacing;

  brushes.Clear();
  brushes.Reserve(m_PointBrushes.GetCount());

  const plMat3 mInvTerrainRot = heightfield.m_GlobalTransform.m_qRotation.GetAsMat3().GetTranspose();

  for (auto& brush : m_PointBrushes)
  {
    if (!brush.m_bInUse || !brush.m_bAffectHeightfields)
      continue;

    if (!brush.m_IncludeTags.IsEmpty() && !brush.m_IncludeTags.IsAnySet(heightfield.m_Tags))
      continue;

    // Skip brushes whose conservative footprint does not overlap the heightfield XY extent.
    const plVec3 vLocalCenter = invTrans * brush.m_vPosition;
    const float fConservativeRadius = plMath::Sqrt(brush.m_vHalfExtents.x * brush.m_vHalfExtents.x + brush.m_vHalfExtents.y * brush.m_vHalfExtents.y) + brush.m_fInnerRadius + brush.m_fOuterRadius;
    const float fClampedX = plMath::Clamp(vLocalCenter.x, 0.0f, fSize);
    const float fClampedY = plMath::Clamp(vLocalCenter.y, 0.0f, fSize);
    const float fDx = vLocalCenter.x - fClampedX;
    const float fDy = vLocalCenter.y - fClampedY;
    if (fDx * fDx + fDy * fDy > fConservativeRadius * fConservativeRadius)
      continue;

    TerrainBrushData& bd = brushes.ExpandAndGetRef();
    bd.Position = vLocalCenter;
    // bd.Position.z -= 0.5f; // keep the brush component slightly above the baked terrain so its icon remains visible
    bd.HalfExtentX = brush.m_vHalfExtents.x;
    bd.HalfExtentYBottom = brush.m_vHalfExtents.y;
    bd.HalfExtentYTop = brush.m_fHalfExtentYTop;
    bd.InnerRadius = brush.m_fInnerRadius;
    bd.OuterRadius = plMath::Max(brush.m_fOuterRadius, 0.0001f);
    bd.Falloff = brush.m_fFalloff;
    bd.ModifyMode = brush.m_eModifyMode.GetValue();
    bd.MaterialIndex = brush.m_uiMaterialIndex;
    bd.MaterialStrength = brush.m_fMaterialStrength;
    bd.Noise = brush.m_fNoise;
    bd.NoiseScale = brush.m_fNoiseScale;
    bd.Priority = static_cast<float>(brush.m_iPriority);
    {
      const plUInt16 uiRawZ = plFloat16(brush.m_fHalfExtentZ).GetRawData();
      bd.HalfExtentZLow = static_cast<plUInt8>(uiRawZ & 0xFFu);
      bd.HalfExtentZHigh = static_cast<plUInt8>(uiRawZ >> 8u);
    }

    // Build the inverse rotation from terrain-local space into brush-local space.
    // mLocalBrushRot = invTerrainRot * brushWorldRot  (takes brush-local -> terrain-local)
    // mInvLocalBrushRot = transpose of above          (takes terrain-local -> brush-local)
    const plMat3 mInvLocalBrushRot = (mInvTerrainRot * brush.m_qRotation.GetAsMat3()).GetTranspose();

    bd.InvRotRow0 = mInvLocalBrushRot.GetRow(0);
    bd.InvRotRow1 = mInvLocalBrushRot.GetRow(1);
    bd.InvRotRow2 = mInvLocalBrushRot.GetRow(2);
  }

  // Primary sort: priority ascending (higher priority = applied later = wins).
  // Within the same priority: Carve last, then Max < Min < Set, then by height.
  brushes.Sort([](const TerrainBrushData& a, const TerrainBrushData& b) -> bool
    {
      if (a.Priority != b.Priority)
        return a.Priority < b.Priority;
      const bool aCarve = a.ModifyMode == plTerrainModifyMode::Carve;
      const bool bCarve = b.ModifyMode == plTerrainModifyMode::Carve;
      if (aCarve != bCarve)
        return !aCarve;
      if (a.ModifyMode != b.ModifyMode)
        return a.ModifyMode < b.ModifyMode;
      if (a.ModifyMode == 1u)             // Min: descending height
        return a.Position.z > b.Position.z;
      return a.Position.z < b.Position.z; // Max / Set / Ignore: ascending height
    });
}

plGALBufferHandle plTerrainSystem::CreateBrushBuffer(plDynamicArray<TerrainBrushData>& brushes, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder) const
{
  // Always allocate at least one element so the binding is always valid.
  // BrushCount=0 prevents the shader from indexing into it.
  if (brushes.IsEmpty())
    brushes.ExpandAndGetRef();

  plGALBufferCreationDescription brushDesc;
  brushDesc.m_uiStructSize = sizeof(TerrainBrushData);
  brushDesc.m_uiTotalSize = brushes.GetCount() * sizeof(TerrainBrushData);
  brushDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  brushDesc.m_ResourceAccess.m_bImmutable = false;

  plGALBufferHandle hBrushBuffer = pDevice->CreateBuffer(brushDesc);
  pCommandEncoder->UpdateBuffer(hBrushBuffer, 0, plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(brushes.GetData()), brushes.GetCount() * sizeof(TerrainBrushData)), plGALUpdateMode::AheadOfTime);

  return hBrushBuffer;
}

void plTerrainSystem::UpdateHeightfield(plUInt32 uiIndex, plRenderContext* pRenderContext, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder)
{
  PL_PROFILE_SCOPE("UpdateHeightfield");

  auto& patch = m_Patches[uiIndex];

  // Terrain is being re-baked; any previously built collider is now stale.
  patch.m_bColliderUpToDate = false;

  plHybridArray<TerrainBrushData, 16> brushCPUData;
  FindOverlappingBrushes(patch, brushCPUData);
  const plUInt32 uiNumBrushes = brushCPUData.GetCount();

  auto hBrushBuffer = CreateBrushBuffer(brushCPUData, pDevice, pCommandEncoder);

  // Build source height data by sampling the height image (or zeros if no image is assigned).
  const plUInt32 S = patch.m_uiCellsPerSide + 9;
  const plUInt32 V = patch.m_uiCellsPerSide + 1;
  plDynamicArray<float> srcHeights;
  srcHeights.SetCount(S * S, 0.0f);

  if (patch.m_hHeightImage.IsValid() && patch.m_fHeightScale > 0.0f)
  {
    plResourceLock<plImageDataResource> imgLock(patch.m_hHeightImage, plResourceAcquireMode::BlockTillLoaded_NeverFail);
    if (imgLock.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      const plImage& img = imgLock->GetDescriptor().m_Image;
      const plColor* pPixels = img.GetPixelPointer<plColor>();
      const plUInt32 uiImgW = img.GetWidth();
      const plUInt32 uiImgH = img.GetHeight();
      const float fInvV = (V > 1) ? 1.0f / static_cast<float>(V - 1) : 0.0f;

      for (plUInt32 sRow = 0; sRow < S; ++sRow)
      {
        for (plUInt32 sCol = 0; sCol < S; ++sCol)
        {
          // Clamp stored-grid coords to the inner vertex range, replicating border values.
          const plUInt32 iRow = static_cast<plUInt32>(plMath::Clamp(static_cast<plInt32>(sRow) - 4, 0, static_cast<plInt32>(V) - 1));
          const plUInt32 iCol = static_cast<plUInt32>(plMath::Clamp(static_cast<plInt32>(sCol) - 4, 0, static_cast<plInt32>(V) - 1));
          const float u = patch.m_vImageOffset.x + static_cast<float>(iCol) * fInvV * patch.m_vImageSize.x;
          const float v = patch.m_vImageOffset.y + static_cast<float>(iRow) * fInvV * patch.m_vImageSize.y;
          srcHeights[sRow * S + sCol] = plImageUtils::BilinearSample(pPixels, uiImgW, uiImgH, plImageAddressMode::Clamp, plVec2(u, v)).r * patch.m_fHeightScale;
        }
      }
    }
  }

  // Upload source heights to a transient buffer
  plGALBufferCreationDescription srcDesc;
  srcDesc.m_uiStructSize = sizeof(float);
  srcDesc.m_uiTotalSize = srcHeights.GetCount() * sizeof(float);
  srcDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  srcDesc.m_ResourceAccess.m_bImmutable = false;

  plGALBufferHandle hSourceBuffer = pDevice->CreateBuffer(srcDesc);

  pCommandEncoder->UpdateBuffer(hSourceBuffer, 0, plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(srcHeights.GetData()), srcHeights.GetCount() * sizeof(float)),
    plGALUpdateMode::AheadOfTime);

  // Upload source material mask to a transient buffer
  plGALBufferCreationDescription srcMaskDesc;
  srcMaskDesc.m_uiStructSize = sizeof(plUInt64);
  srcMaskDesc.m_uiTotalSize = patch.m_MaskData.GetCount() * sizeof(plUInt64);
  srcMaskDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  srcMaskDesc.m_ResourceAccess.m_bImmutable = false;

  plGALBufferHandle hSourceMask = pDevice->CreateBuffer(srcMaskDesc);
  pCommandEncoder->UpdateBuffer(hSourceMask, 0, plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(patch.m_MaskData.GetData()), patch.m_MaskData.GetCount() * sizeof(plUInt64)),
    plGALUpdateMode::AheadOfTime);

  // Dispatch covers the full stored grid including the 4-vertex border on each side.
  const plUInt32 uiStoredSize = patch.m_uiCellsPerSide + 9;
  const plUInt32 uiGroups = (uiStoredSize + 15) / 16;

  // Pass 1: bake heights and finalized material mask from source + brushes.
  // Mask normalization (default-material fill, sum-to-1, sort by weight desc) happens here too,
  // because there is no UAV-to-UAV barrier between consecutive dispatches on BakedMask, which previously
  // caused Pass 2 to read stale per-vertex masks from earlier bakes.
  {
    pRenderContext->BindShader(m_hTerrainBakeStep1Shader);

    HeightfieldBakeConstants constants;
    constants.GridSpacing = patch.m_fGridSpacing;
    constants.VertexIdxPitch = uiStoredSize;
    constants.BrushCount = uiNumBrushes;
    constants.DefaultMaterialIndex = patch.m_uiDefaultMaterialIndex;
    constants.PatchOriginX = patch.m_GlobalTransform.m_vPosition.x;
    constants.PatchOriginY = patch.m_GlobalTransform.m_vPosition.y;
    pRenderContext->SetPushConstants("HeightfieldBakeConstants", constants);

    plBindGroupBuilder& bindGroup = pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);
    bindGroup.BindBuffer("SourceHeights", hSourceBuffer);
    bindGroup.BindBuffer("BakedHeights", patch.m_hBakedHeights);
    bindGroup.BindBuffer("SourceMask", hSourceMask);
    bindGroup.BindBuffer("BakedMask", patch.m_hBakedMask);
    bindGroup.BindBuffer("Brushes", hBrushBuffer);

    pRenderContext->Dispatch(uiGroups, uiGroups, 1).AssertSuccess();
  }

  // Pass 2: derive normals from BakedHeights (SRV) into BakedNormals (UAV).
  // BakedHeights binds as SRV here (was UAV in Pass 1) so the engine inserts the required barrier.
  {
    pRenderContext->BindShader(m_hTerrainBakeStep2Shader);

    HeightfieldBakeConstants constants;
    constants.GridSpacing = patch.m_fGridSpacing;
    constants.VertexIdxPitch = uiStoredSize;
    pRenderContext->SetPushConstants("HeightfieldBakeConstants", constants);

    plBindGroupBuilder& bindGroup = pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);
    bindGroup.BindBuffer("BakedHeights", patch.m_hBakedHeights);
    bindGroup.BindBuffer("BakedNormals", patch.m_hBakedNormals);

    pRenderContext->Dispatch(uiGroups, uiGroups, 1).AssertSuccess();
  }

  // Free transient buffers after commands are recorded
  pDevice->DestroyBuffer(hSourceBuffer);
  pDevice->DestroyBuffer(hSourceMask);
  pDevice->DestroyBuffer(hBrushBuffer);

  patch.m_bDirty = false;

  if (patch.m_bNeedsColliderReadback && patch.m_pHeightReadback != nullptr)
  {
    patch.m_pHeightReadback->ReadbackBuffer(*pCommandEncoder, patch.m_hBakedHeights);
    if (patch.m_pMaskReadback != nullptr)
      patch.m_pMaskReadback->ReadbackBuffer(*pCommandEncoder, patch.m_hBakedMask);
    patch.m_bReadbackPending = true;
    patch.m_bColliderDataReady = false;
  }
}

void plTerrainSystem::SetNeedsColliderReadback(plUInt32 uiPatchIndex, bool bEnable)
{
  if (uiPatchIndex < m_Patches.GetCount())
    m_Patches[uiPatchIndex].m_bNeedsColliderReadback = bEnable;
}

bool plTerrainSystem::IsColliderDataReady(plUInt32 uiPatchIndex) const
{
  return (uiPatchIndex < m_Patches.GetCount()) && m_Patches[uiPatchIndex].m_bColliderDataReady;
}

plArrayPtr<const float> plTerrainSystem::GetCpuBakedHeights(plUInt32 uiPatchIndex) const
{
  if (uiPatchIndex < m_Patches.GetCount())
    return m_Patches[uiPatchIndex].m_CpuBakedHeights.GetArrayPtr();
  return {};
}

plArrayPtr<const plUInt8> plTerrainSystem::GetCpuBakedMaterialIndices(plUInt32 uiPatchIndex) const
{
  if (uiPatchIndex < m_Patches.GetCount())
    return m_Patches[uiPatchIndex].m_CpuDominantMaterialIndices.GetArrayPtr();
  return {};
}

void plTerrainSystem::ClearColliderDataReady(plUInt32 uiPatchIndex)
{
  if (uiPatchIndex < m_Patches.GetCount())
  {
    m_Patches[uiPatchIndex].m_bColliderDataReady = false;
    m_Patches[uiPatchIndex].m_bColliderUpToDate = true;
  }
}

void plTerrainSystem::ProcessPendingHeightfieldReadbacks()
{
  for (auto& patch : m_Patches)
  {
    if (!patch.m_bInUse || !patch.m_bReadbackPending || patch.m_pHeightReadback == nullptr)
      continue;

    const plEnum<plGALAsyncResult> heightResult = patch.m_pHeightReadback->GetReadbackResult(plTime::MakeZero());
    if (heightResult == plGALAsyncResult::Expired)
    {
      // GPU result was not consumed in time; will re-issue on next dirty bake.
      patch.m_bReadbackPending = false;
      continue;
    }
    if (heightResult != plGALAsyncResult::Ready)
      continue;

    // Also check mask readback if present; if not yet ready, wait.
    if (patch.m_pMaskReadback != nullptr)
    {
      const plEnum<plGALAsyncResult> maskResult = patch.m_pMaskReadback->GetReadbackResult(plTime::MakeZero());
      if (maskResult == plGALAsyncResult::Expired)
      {
        patch.m_bReadbackPending = false;
        continue;
      }
      if (maskResult != plGALAsyncResult::Ready)
        continue;
    }

    PL_PROFILE_SCOPE("HeightfieldReadback");

    const plUInt32 C = patch.m_uiCellsPerSide;
    const plUInt32 S = C + 9; // stored stride including 4-vertex border on each side
    // Jolt requires a vertex count that is a multiple of 4; pad render vertex count (C+1) up to the next multiple.
    const plUInt32 uiJoltN = ((C + 1) + 3u) & ~3u;

    // Process height data.
    {
      plArrayPtr<const plUInt8> rawMemory;
      auto lock = patch.m_pHeightReadback->LockBuffer(rawMemory);
      if (!lock)
        continue;

      const float* pStoredHeights = reinterpret_cast<const float*>(rawMemory.GetPtr());
      patch.m_CpuBakedHeights.SetCountUninitialized(uiJoltN * uiJoltN);
      for (plUInt32 row = 0; row < uiJoltN; ++row)
      {
        plMemoryUtils::Copy(patch.m_CpuBakedHeights.GetData() + row * uiJoltN, pStoredHeights + (4 + row) * S + 4, uiJoltN);
      }
    }

    // Process mask data: extract dominant material index (slot 0 of sorted mask) per physics vertex.
    if (patch.m_pMaskReadback != nullptr)
    {
      plArrayPtr<const plUInt8> rawMemory;
      auto lock = patch.m_pMaskReadback->LockBuffer(rawMemory);
      if (lock)
      {
        const plUInt32* pStoredMask = reinterpret_cast<const plUInt32*>(rawMemory.GetPtr());
        // Each vertex is 2 uints (uint2); the dominant index is bits [7:0] of the first uint.
        patch.m_CpuDominantMaterialIndices.SetCountUninitialized(uiJoltN * uiJoltN);
        for (plUInt32 row = 0; row < uiJoltN; ++row)
        {
          for (plUInt32 col = 0; col < uiJoltN; ++col)
          {
            const plUInt32 storedIdx = (4 + row) * S + (4 + col);
            // uint2 is stored as 2 consecutive uint32s; first uint's low byte = dominant material index.
            const plUInt8 dominantIdx = static_cast<plUInt8>(pStoredMask[storedIdx * 2] & 0xFFu);
            patch.m_CpuDominantMaterialIndices[row * uiJoltN + col] = dominantIdx;
          }
        }
      }
    }

    patch.m_bReadbackPending = false;
    patch.m_bColliderDataReady = true;
  }
}

plUInt32 plTerrainSystem::CreateVoxelVolume(plUInt32 uiResolution, float fVoxelSize)
{
  PL_PROFILE_SCOPE("CreateVoxelVolume");

  plUInt32 uiIndex = plInvalidIndex;
  for (plUInt32 i = 0; i < m_VoxelVolumes.GetCount(); ++i)
  {
    if (!m_VoxelVolumes[i].m_bInUse)
    {
      uiIndex = i;
      break;
    }
  }
  if (uiIndex == plInvalidIndex)
  {
    uiIndex = m_VoxelVolumes.GetCount();
    m_VoxelVolumes.ExpandAndGetRef();
  }

  plTerrainData_Voxel& vol = m_VoxelVolumes[uiIndex];
  vol.m_uiResolution = uiResolution;
  vol.m_uiStoredResolution = (uiResolution + 7u) & ~7u;
  vol.m_uiVoxelResolution = (vol.m_uiStoredResolution + 2u + 7u) & ~7u; // add 1-voxel border each side, round total to multiple of 8
  vol.m_fVoxelSize = fVoxelSize;
  vol.m_bInUse = true;
  vol.m_bDirty = true;
  vol.m_bMeshDataReady = false;
  vol.m_bGpuMeshReady = false;
  vol.m_bNeedsCollisionReadback = false;
  vol.m_bCollisionReadbackPending = false;
  vol.m_uiBrushOverlapHash = 0;
  vol.m_uiCpuMeshVertexCount = 0;
  vol.m_uiCpuMeshPrimitiveCount = 0;

  const plUInt32 uiStoredN = vol.m_uiStoredResolution;
  const plUInt32 uiVoxelN = vol.m_uiVoxelResolution; // = uiStoredN + 8; always divisible by 8
  const plUInt32 uiPackedCount = uiVoxelN * uiVoxelN * uiVoxelN / 8;

  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();

  // Voxel buffer (UAV in bake CS, SRV in mesh build CS)
  {
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(plUInt32);
    bufDesc.m_uiTotalSize = uiPackedCount * sizeof(plUInt32);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource | plGALBufferUsageFlags::UnorderedAccess;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    vol.m_hBakedVoxels = pDevice->CreateBuffer(bufDesc);
  }

  // Per-voxel SDF buffer (UAV in bake CS, SRV in Pass1). One float per voxel, unpacked.
  {
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(float);
    bufDesc.m_uiTotalSize = uiVoxelN * uiVoxelN * uiVoxelN * sizeof(float);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource | plGALBufferUsageFlags::UnorderedAccess;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    vol.m_hBakedVoxelDist = pDevice->CreateBuffer(bufDesc);
    vol.m_hBakedVoxelDistScratch = pDevice->CreateBuffer(bufDesc);
  }

  // GPU mesh output buffers (written by surface nets shaders, read back to CPU on demand).
  // Vertices: one pre-indexed slot per cell; cells = (N+1)³ (cells 0..N cover the volume bounds
  // [0, N*GridSpacing] flush on both sides; cells 0 and N use border voxels for corner samples).
  // Indices: worst-case 3 axes × (N+1)² edges per layer × (N+1) layers × 6 indices per quad.
  const plUInt32 uiCells = uiStoredN + 1u;
  const plUInt32 uiMaxCells = uiCells * uiCells * uiCells;
  const plUInt32 uiMaxVertices = uiMaxCells;
  const plUInt32 uiMaxIndices = uiMaxCells * 18u;

  {
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(VoxelGpuVertex);
    bufDesc.m_uiTotalSize = uiMaxVertices * sizeof(VoxelGpuVertex);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::UnorderedAccess | plGALBufferUsageFlags::ShaderResource;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    vol.m_hGpuMeshVertices = pDevice->CreateBuffer(bufDesc);
  }
  {
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(plUInt32);
    bufDesc.m_uiTotalSize = uiMaxIndices * sizeof(plUInt32);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::UnorderedAccess | plGALBufferUsageFlags::ShaderResource;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    vol.m_hGpuMeshIndices = pDevice->CreateBuffer(bufDesc);
  }
  {
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(VoxelMeshCounts);
    bufDesc.m_uiTotalSize = sizeof(VoxelMeshCounts);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::UnorderedAccess | plGALBufferUsageFlags::ShaderResource;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    vol.m_hGpuMeshCounts = pDevice->CreateBuffer(bufDesc);
  }
  {
    // Indirect draw args: 4 uints (VertexCount, InstanceCount, StartVertex, StartInstance).
    // ByteAddressBuffer because DX11 forbids combining DrawIndirect with StructuredBuffer.
    plGALBufferCreationDescription bufDesc;
    bufDesc.m_uiStructSize = sizeof(plUInt32);
    bufDesc.m_uiTotalSize = 4 * sizeof(plUInt32);
    bufDesc.m_BufferFlags = plGALBufferUsageFlags::ByteAddressBuffer | plGALBufferUsageFlags::UnorderedAccess | plGALBufferUsageFlags::DrawIndirect;
    bufDesc.m_ResourceAccess.m_bImmutable = false;
    const plUInt32 zero[4] = {0, 1, 0, 0};
    vol.m_hGpuMeshDrawArgs = pDevice->CreateBuffer(bufDesc, plMakeArrayPtr(zero).ToByteArray());
  }

  vol.m_CpuMeshVertices.SetCount(uiMaxVertices);
  vol.m_CpuMeshIndices.SetCount(uiMaxIndices);

  vol.m_pMeshVertexReadback = PL_DEFAULT_NEW(plGALReadbackBufferHelper);
  vol.m_pMeshIndexReadback = PL_DEFAULT_NEW(plGALReadbackBufferHelper);
  vol.m_pMeshCountReadback = PL_DEFAULT_NEW(plGALReadbackBufferHelper);

  return uiIndex;
}

void plTerrainSystem::RemoveVoxelVolume(plUInt32 uiIndex)
{
  PL_LOCK(m_Mutex);
  m_QueuedVoxelVolumesToDelete.PushBack(uiIndex);
}

plTerrainData_Voxel& plTerrainSystem::ModifyVoxelVolume(plUInt32 uiIndex)
{
  auto& vol = m_VoxelVolumes[uiIndex];
  vol.m_bDirty = true;
  return vol;
}

bool plTerrainSystem::IsVoxelMeshDataReady(plUInt32 uiIndex) const
{
  return (uiIndex < m_VoxelVolumes.GetCount()) && m_VoxelVolumes[uiIndex].m_bMeshDataReady;
}

void plTerrainSystem::ClearVoxelMeshDataReady(plUInt32 uiIndex)
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    m_VoxelVolumes[uiIndex].m_bMeshDataReady = false;
  }
}

plArrayPtr<const VoxelGpuVertex> plTerrainSystem::GetCpuMeshVertices(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_CpuMeshVertices.GetArrayPtr();
  }

  return {};
}

plArrayPtr<const plUInt32> plTerrainSystem::GetCpuMeshIndices(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_CpuMeshIndices.GetArrayPtr();
  }

  return {};
}

plUInt32 plTerrainSystem::GetCpuMeshVertexCount(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_uiCpuMeshVertexCount;
  }

  return 0;
}

plUInt32 plTerrainSystem::GetCpuMeshPrimitiveCount(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_uiCpuMeshPrimitiveCount;
  }

  return 0;
}

bool plTerrainSystem::IsGpuMeshReady(plUInt32 uiIndex) const
{
  return (uiIndex < m_VoxelVolumes.GetCount()) && m_VoxelVolumes[uiIndex].m_bGpuMeshReady;
}

plGALBufferHandle plTerrainSystem::GetGpuMeshVertexBuffer(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_hGpuMeshVertices;
  }

  return {};
}

plGALBufferHandle plTerrainSystem::GetGpuMeshDrawArgsBuffer(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_hGpuMeshDrawArgs;
  }
  return {};
}

plGALBufferHandle plTerrainSystem::GetGpuMeshIndexBuffer(plUInt32 uiIndex) const
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    return m_VoxelVolumes[uiIndex].m_hGpuMeshIndices;
  }

  return {};
}

void plTerrainSystem::SetNeedsCollisionReadback(plUInt32 uiIndex, bool bEnable)
{
  if (uiIndex < m_VoxelVolumes.GetCount())
  {
    m_VoxelVolumes[uiIndex].m_bNeedsCollisionReadback = bEnable;
  }
}

plUInt64 plTerrainSystem::ComputeVoxelOverlapHash(const plTerrainData_Voxel& vol) const
{
  const plTransform invTrans = vol.m_GlobalTransform.GetInverse();
  const float fSize = (float)vol.m_uiResolution * vol.m_fVoxelSize;

  plHashStreamWriter64 writer;

  for (plUInt32 i = 0; i < m_PointBrushes.GetCount(); ++i)
  {
    const auto& brush = m_PointBrushes[i];
    if (!brush.m_bInUse || !brush.m_bAffectVoxels)
      continue;

    if (!brush.m_IncludeTags.IsEmpty() && !brush.m_IncludeTags.IsAnySet(vol.m_Tags))
      continue;

    const plVec3 vLocalCenter = invTrans * brush.m_vPosition;
    const float fConservativeRadius = plMath::Sqrt(brush.m_vHalfExtents.x * brush.m_vHalfExtents.x + brush.m_vHalfExtents.y * brush.m_vHalfExtents.y) + brush.m_fInnerRadius + brush.m_fOuterRadius;

    // 2D XY footprint test against [0, fSize]^2
    const float fDx = vLocalCenter.x - plMath::Clamp(vLocalCenter.x, 0.0f, fSize);
    const float fDy = vLocalCenter.y - plMath::Clamp(vLocalCenter.y, 0.0f, fSize);
    if (fDx * fDx + fDy * fDy > fConservativeRadius * fConservativeRadius)
      continue;

    writer << i;
    writer << brush.m_vPosition.x << brush.m_vPosition.y << brush.m_vPosition.z;
    writer << brush.m_qRotation.x << brush.m_qRotation.y << brush.m_qRotation.z << brush.m_qRotation.w;
    writer << brush.m_vHalfExtents.x << brush.m_vHalfExtents.y;
    writer << brush.m_fHalfExtentZ << brush.m_fHalfExtentYTop;
    writer << brush.m_fInnerRadius << brush.m_fOuterRadius << brush.m_fFalloff;
    writer << brush.m_eModifyMode.GetValue();
    writer << brush.m_uiMaterialIndex << brush.m_fMaterialStrength;
    writer << brush.m_fNoise << brush.m_fNoiseScale;
    writer << brush.m_iPriority;
    brush.m_IncludeTags.Save(writer);
  }

  writer << vol.m_fFillHeight;
  vol.m_Tags.Save(writer);

  return writer.GetHashValue();
}

void plTerrainSystem::UpdateVoxels(plUInt32 uiIndex, plRenderContext* pRenderContext, plGALDevice* pDevice, plGALCommandEncoder* pCommandEncoder)
{
  PL_PROFILE_SCOPE("UpdateVoxels");

  auto& vol = m_VoxelVolumes[uiIndex];

  const plTransform invTrans = vol.m_GlobalTransform.GetInverse();
  const float fSize = (float)vol.m_uiResolution * vol.m_fVoxelSize;

  const plMat3 mInvVolumeRot = vol.m_GlobalTransform.m_qRotation.GetAsMat3().GetTranspose();

  plHybridArray<TerrainBrushData, 16> brushCPUData;
  for (const auto& brush : m_PointBrushes)
  {
    if (!brush.m_bInUse || !brush.m_bAffectVoxels)
      continue;

    if (!brush.m_IncludeTags.IsEmpty() && !brush.m_IncludeTags.IsAnySet(vol.m_Tags))
      continue;

    if (brush.m_eModifyMode == plTerrainModifyMode::OnlyPaint2D)
      continue;

    const plVec3 vLocalCenter = invTrans * brush.m_vPosition;
    const float fConservativeRadius = plMath::Sqrt(brush.m_vHalfExtents.x * brush.m_vHalfExtents.x + brush.m_vHalfExtents.y * brush.m_vHalfExtents.y + brush.m_fHalfExtentZ * brush.m_fHalfExtentZ) + brush.m_fInnerRadius + brush.m_fOuterRadius;

    // 2D XY footprint test — conservative sphere radius bounds the 3D brush volume
    const float fDx = vLocalCenter.x - plMath::Clamp(vLocalCenter.x, 0.0f, fSize);
    const float fDy = vLocalCenter.y - plMath::Clamp(vLocalCenter.y, 0.0f, fSize);
    if (fDx * fDx + fDy * fDy > fConservativeRadius * fConservativeRadius)
      continue;

    TerrainBrushData& bd = brushCPUData.ExpandAndGetRef();
    bd.Position = vLocalCenter;
    bd.HalfExtentX = brush.m_vHalfExtents.x;
    bd.HalfExtentYBottom = brush.m_vHalfExtents.y;
    bd.HalfExtentYTop = brush.m_fHalfExtentYTop;
    bd.InnerRadius = brush.m_fInnerRadius;
    bd.OuterRadius = plMath::Max(brush.m_fOuterRadius, 0.0001f);
    bd.Falloff = brush.m_fFalloff;
    bd.ModifyMode = brush.m_eModifyMode.GetValue();
    bd.MaterialIndex = brush.m_uiMaterialIndex;
    bd.MaterialStrength = brush.m_fMaterialStrength;
    bd.Noise = brush.m_fNoise;
    bd.NoiseScale = brush.m_fNoiseScale;
    bd.Priority = static_cast<float>(brush.m_iPriority);
    {
      const plUInt16 uiRawZ = plFloat16(brush.m_fHalfExtentZ).GetRawData();
      bd.HalfExtentZLow = static_cast<plUInt8>(uiRawZ & 0xFFu);
      bd.HalfExtentZHigh = static_cast<plUInt8>(uiRawZ >> 8u);
    }

    const plMat3 mInvLocalBrushRot = (mInvVolumeRot * brush.m_qRotation.GetAsMat3()).GetTranspose();
    bd.InvRotRow0 = mInvLocalBrushRot.GetRow(0);
    bd.InvRotRow1 = mInvLocalBrushRot.GetRow(1);
    bd.InvRotRow2 = mInvLocalBrushRot.GetRow(2);
  }

  // Primary sort: priority ascending. Within same priority: same order as heightfields.
  brushCPUData.Sort([](const TerrainBrushData& a, const TerrainBrushData& b) -> bool
    {
      if (a.Priority != b.Priority)
        return a.Priority < b.Priority;
      if (a.ModifyMode != b.ModifyMode)
        return a.ModifyMode < b.ModifyMode;
      if (a.ModifyMode == 1u)
        return a.Position.z > b.Position.z;
      return a.Position.z < b.Position.z; });

  const plUInt32 uiNumBrushes = brushCPUData.GetCount();
  auto hBrushBuffer = CreateBrushBuffer(brushCPUData, pDevice, pCommandEncoder);

  pRenderContext->BindShader(m_hVoxelBakeShader);

  VoxelBakeConstants constants = {};
  const plUInt32 uiSN = vol.m_uiStoredResolution;
  const plUInt32 uiVN = vol.m_uiVoxelResolution;
  constants.GridSpacing = vol.m_fVoxelSize;
  constants.VertexIdxPitch = uiSN;
  constants.VoxelResolution = uiVN;
  constants.BrushCount = uiNumBrushes;
  constants.InitialSolid = vol.m_uiInitialSolid;
  // Convert world-space fill height to local voxel space (assumes no significant rotation).
  constants.FillHeight = (vol.m_fFillHeight - vol.m_GlobalTransform.m_vPosition.z) / vol.m_GlobalTransform.m_vScale.z;
  constants.PatchOriginX = vol.m_GlobalTransform.m_vPosition.x;
  constants.PatchOriginY = vol.m_GlobalTransform.m_vPosition.y;
  constants.PatchOriginZ = vol.m_GlobalTransform.m_vPosition.z;

  pRenderContext->SetPushConstants("VoxelBakeConstants", constants);

  plBindGroupBuilder& bindGroup = pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);
  bindGroup.BindBuffer("BakedVoxels", vol.m_hBakedVoxels);
  bindGroup.BindBuffer("BakedVoxelDist", vol.m_hBakedVoxelDist);
  bindGroup.BindBuffer("Brushes", hBrushBuffer);

  const plUInt32 uiGroupsX = (uiVN / 8u + 3u) / 4u; // X covers packed width (uiVN/8); uiVN always divisible by 8
  const plUInt32 uiGroups = (uiVN + 3u) / 4u;
  pRenderContext->Dispatch(uiGroupsX, uiGroups, uiGroups).AssertSuccess();

  pDevice->DestroyBuffer(hBrushBuffer);

  vol.m_bDirty = false;

  // SDF blur pass: 3x3x3 box blur of the baked distances. Resyncs nibbles from blurred sign.
  // Rounds off thin spikes and corner-only connections; runs first so the cleanup pass below
  // operates on already-smoothed connectivity.
  {
    pRenderContext->BindShader(m_hVoxelBlurDistShader);

    VoxelBakeConstants c = {};
    c.VoxelResolution = uiVN;
    c.SmoothFactor = 0.3f; // blur weight: 0 = passthrough, 1 = full 3³ average
    pRenderContext->SetPushConstants("VoxelBakeConstants", c);

    bindGroup.BindBuffer("BakedVoxelDistIn", vol.m_hBakedVoxelDist);
    bindGroup.BindBuffer("BakedVoxelDistOut", vol.m_hBakedVoxelDistScratch);
    bindGroup.BindBuffer("BakedVoxels", vol.m_hBakedVoxels);

    pRenderContext->Dispatch(uiGroupsX, uiGroups, uiGroups).AssertSuccess();
  }

  // Topology cleanup: drop voxels whose only neighbors are weak corner/edge attachments,
  // which would otherwise triangulate to spikes. Run several iterations because each pass
  // can reduce a kept voxel's neighbor count below the threshold, exposing further weak
  // voxels. Each iteration reads its dist input as a stable snapshot (SRV) and writes the
  // culled result to the other dist buffer, while patching BakedVoxels in place. Runs after
  // blur so the smoothing pass cannot resurrect culled voxels.
  const plUInt32 uiCleanupIterations = 3;
  plGALBufferHandle hCleanupSrc = vol.m_hBakedVoxelDistScratch;
  plGALBufferHandle hCleanupDst = vol.m_hBakedVoxelDist;
  {
    pRenderContext->BindShader(m_hVoxelCleanupShader);

    VoxelBakeConstants c = {};
    c.GridSpacing = vol.m_fVoxelSize;
    c.VoxelResolution = uiVN;
    pRenderContext->SetPushConstants("VoxelBakeConstants", c);

    for (plUInt32 it = 0; it < uiCleanupIterations; ++it)
    {
      bindGroup.BindBuffer("BakedVoxelDistIn", hCleanupSrc);
      bindGroup.BindBuffer("BakedVoxelDistOut", hCleanupDst);
      bindGroup.BindBuffer("BakedVoxels", vol.m_hBakedVoxels);

      pRenderContext->Dispatch(uiGroupsX, uiGroups, uiGroups).AssertSuccess();

      plMath::Swap(hCleanupSrc, hCleanupDst);
    }
  }
  // After the swap on the final iteration hCleanupSrc points at the buffer that holds the
  // most recent cleanup output — surface nets reads from there.
  const plGALBufferHandle hFinalVoxelDist = hCleanupSrc;

  // Reset PrimitiveCount before Pass2's atomic-adds. Uses the same InterlockedExchange path Pass2
  // increments through, so the addressing matches exactly — a plain struct assignment did not
  // actually clear the field, causing each rebuild to accumulate onto the previous frame's count.
  {
    pRenderContext->BindShader(m_hVoxelMeshClearShader);
    bindGroup.BindBuffer("OutCounts", vol.m_hGpuMeshCounts);
    pRenderContext->Dispatch(1, 1, 1).AssertSuccess();
  }

  // Surface Nets Pass 1: compute one vertex per active cell, pre-indexed at (N+1)³ slots.
  {
    VoxelBakeConstants c = {};
    c.GridSpacing = vol.m_fVoxelSize;
    c.VertexIdxPitch = vol.m_uiStoredResolution;
    c.VoxelResolution = vol.m_uiVoxelResolution;
    pRenderContext->SetPushConstants("VoxelBakeConstants", c);
    pRenderContext->BindShader(m_hVoxelSurfaceNetsPass1Shader);
    bindGroup.BindBuffer("BakedVoxels", vol.m_hBakedVoxels);
    bindGroup.BindBuffer("BakedVoxelDist", hFinalVoxelDist);
    bindGroup.BindBuffer("OutVertices", vol.m_hGpuMeshVertices);
    const plUInt32 uiPass1Groups = (vol.m_uiStoredResolution + 1u + 3u) / 4u;
    pRenderContext->Dispatch(uiPass1Groups, uiPass1Groups, uiPass1Groups).AssertSuccess();
  }

  // Surface Nets Pass 2: emit quads for each sign-change edge (3 axis dispatches).
  // Rebinding OutVertices as SRV in Pass2's shader declaration flushes the Pass1 UAV write.
  {
    pRenderContext->BindShader(m_hVoxelSurfaceNetsPass2Shader);
    const plUInt32 uiPass2Groups = (vol.m_uiStoredResolution + 1u + 3u) / 4u;
    for (plUInt32 axis = 0; axis < 3; ++axis)
    {
      VoxelBakeConstants c = {};
      c.GridSpacing = vol.m_fVoxelSize;
      c.VertexIdxPitch = vol.m_uiStoredResolution;
      c.VoxelResolution = vol.m_uiVoxelResolution;
      c.EdgeAxis = axis;
      pRenderContext->SetPushConstants("VoxelBakeConstants", c);
      bindGroup.BindBuffer("BakedVoxels", vol.m_hBakedVoxels);
      bindGroup.BindBuffer("BakedVoxelDist", hFinalVoxelDist);
      bindGroup.BindBuffer("OutVertices", vol.m_hGpuMeshVertices);
      bindGroup.BindBuffer("OutIndices", vol.m_hGpuMeshIndices);
      bindGroup.BindBuffer("OutCounts", vol.m_hGpuMeshCounts);
      pRenderContext->Dispatch(uiPass2Groups, uiPass2Groups, uiPass2Groups).AssertSuccess();
    }
  }

  // Fill the indirect draw args buffer from the just-written PrimitiveCount. Same pass barrier guarantees
  // the renderer sees args matching the current vertex/index buffer contents — no readback latency window.
  {
    pRenderContext->BindShader(m_hVoxelFillDrawArgsShader);
    bindGroup.BindBuffer("InCounts", vol.m_hGpuMeshCounts);
    bindGroup.BindBuffer("OutDrawArgs", vol.m_hGpuMeshDrawArgs);
    pRenderContext->Dispatch(1, 1, 1).AssertSuccess();
  }

  // Rendering uses indirect draw with GPU-filled args, so it doesn't need any readback. Only the
  // collision flow needs CPU-side data — and only then do we pay for the counts/vertex/index readback.
  if (vol.m_bNeedsCollisionReadback && !vol.m_bCollisionReadbackPending)
  {
    vol.m_pMeshCountReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshCounts);
    vol.m_pMeshVertexReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshVertices);
    vol.m_pMeshIndexReadback->ReadbackBuffer(*pCommandEncoder, vol.m_hGpuMeshIndices);
    vol.m_bCollisionReadbackPending = true;
  }
  vol.m_bMeshDataReady = false;
  vol.m_bGpuMeshReady = true;
}

void plTerrainSystem::ProcessPendingMeshReadbacks()
{
  for (auto& vol : m_VoxelVolumes)
  {
    if (!vol.m_bInUse)
      continue;

    // Collision path: only when explicitly requested. Counts, vertex and index readbacks are kicked
    // together; finalize when all three have landed.
    if (vol.m_bCollisionReadbackPending)
    {
      const plEnum<plGALAsyncResult> countResult = vol.m_pMeshCountReadback->GetReadbackResult(plTime::MakeZero());
      const plEnum<plGALAsyncResult> vertResult = vol.m_pMeshVertexReadback->GetReadbackResult(plTime::MakeZero());
      const plEnum<plGALAsyncResult> idxResult = vol.m_pMeshIndexReadback->GetReadbackResult(plTime::MakeZero());

      if (countResult == plGALAsyncResult::Expired || vertResult == plGALAsyncResult::Expired || idxResult == plGALAsyncResult::Expired)
      {
        vol.m_bCollisionReadbackPending = false;
        vol.m_bNeedsCollisionReadback = false;
      }
      else if (countResult == plGALAsyncResult::Ready && vertResult == plGALAsyncResult::Ready && idxResult == plGALAsyncResult::Ready)
      {
        PL_PROFILE_SCOPE("VoxelCollisionReadback");

        // Pull primitive count first — index copy size depends on it.
        {
          plArrayPtr<const plUInt8> rawMemory;
          auto lock = vol.m_pMeshCountReadback->LockBuffer(rawMemory);
          if (lock)
          {
            const VoxelMeshCounts* pCounts = reinterpret_cast<const VoxelMeshCounts*>(rawMemory.GetPtr());
            const plUInt32 uiCells = vol.m_uiStoredResolution + 1u;
            vol.m_uiCpuMeshVertexCount = uiCells * uiCells * uiCells;
            vol.m_uiCpuMeshPrimitiveCount = pCounts->PrimitiveCount;
          }
        }

        // Copy vertex data.
        {
          plArrayPtr<const plUInt8> rawMemory;
          auto lock = vol.m_pMeshVertexReadback->LockBuffer(rawMemory);
          if (lock)
          {
            vol.m_CpuMeshVertices.SetCount(vol.m_uiCpuMeshVertexCount);
            plMemoryUtils::Copy(vol.m_CpuMeshVertices.GetData(), reinterpret_cast<const VoxelGpuVertex*>(rawMemory.GetPtr()), vol.m_uiCpuMeshVertexCount);
          }
        }

        // Copy index data.
        {
          plArrayPtr<const plUInt8> rawMemory;
          auto lock = vol.m_pMeshIndexReadback->LockBuffer(rawMemory);
          if (lock)
          {
            const plUInt32 uiIndexCount = vol.m_uiCpuMeshPrimitiveCount * 3;
            vol.m_CpuMeshIndices.SetCountUninitialized(uiIndexCount);
            plMemoryUtils::Copy(vol.m_CpuMeshIndices.GetData(), reinterpret_cast<const plUInt32*>(rawMemory.GetPtr()), uiIndexCount);
          }
        }

        vol.m_bCollisionReadbackPending = false;
        vol.m_bNeedsCollisionReadback = false;
        vol.m_bMeshDataReady = true;
      }
    }
  }
}

void plTerrainSystem::FrameCleanup()
{
  PL_LOCK(m_Mutex);

  for (plUInt32 idx : m_QueuedHeightfieldsToDelete)
  {
    RemoveHeightfield(idx);
  }
  m_QueuedHeightfieldsToDelete.Clear();

  for (plUInt32 idx : m_QueuedVoxelVolumesToDelete)
  {
    if (idx >= m_VoxelVolumes.GetCount())
      continue;
    auto& vol = m_VoxelVolumes[idx];
    plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
    if (!vol.m_hBakedVoxels.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hBakedVoxels);
      vol.m_hBakedVoxels.Invalidate();
    }
    if (!vol.m_hBakedVoxelDist.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hBakedVoxelDist);
      vol.m_hBakedVoxelDist.Invalidate();
    }
    if (!vol.m_hBakedVoxelDistScratch.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hBakedVoxelDistScratch);
      vol.m_hBakedVoxelDistScratch.Invalidate();
    }
    if (!vol.m_hGpuMeshVertices.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hGpuMeshVertices);
      vol.m_hGpuMeshVertices.Invalidate();
    }
    if (!vol.m_hGpuMeshIndices.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hGpuMeshIndices);
      vol.m_hGpuMeshIndices.Invalidate();
    }
    if (!vol.m_hGpuMeshCounts.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hGpuMeshCounts);
      vol.m_hGpuMeshCounts.Invalidate();
    }
    if (!vol.m_hGpuMeshDrawArgs.IsInvalidated())
    {
      pDevice->DestroyBuffer(vol.m_hGpuMeshDrawArgs);
      vol.m_hGpuMeshDrawArgs.Invalidate();
    }
    PL_DEFAULT_DELETE(vol.m_pMeshVertexReadback);
    PL_DEFAULT_DELETE(vol.m_pMeshIndexReadback);
    PL_DEFAULT_DELETE(vol.m_pMeshCountReadback);
    vol.m_CpuMeshVertices.Clear();
    vol.m_CpuMeshIndices.Clear();
    vol.m_bInUse = false;
    vol.m_bMeshDataReady = false;
    vol.m_bGpuMeshReady = false;
    vol.m_bCollisionReadbackPending = false;
    vol.m_bNeedsCollisionReadback = false;
  }
  m_QueuedVoxelVolumesToDelete.Clear();
}