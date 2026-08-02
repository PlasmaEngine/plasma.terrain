#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Messages/TransformChangedMessage.h>
#include <Core/Physics/SurfaceResource.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Reflection/Implementation/PropertyAttributes.h>
#include <Foundation/Types/TagRegistry.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <TerrainPlugin/Components/TerrainVoxelComponent.h>
#include <TerrainPlugin/Rendering/TerrainRenderData.h>
#include <TerrainPlugin/TerrainSystem.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plTerrainVoxelComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_ACCESSOR_PROPERTY("Resolution", plTerrainResolution, GetResolution, SetResolution),
    PL_ACCESSOR_PROPERTY("Size", GetSize, SetSize)->AddAttributes(new plClampValueAttribute(1.0f, 1024.0f), new plDefaultValueAttribute(32.0f)),
    PL_RESOURCE_ACCESSOR_PROPERTY("Material", GetMaterial, SetMaterial)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Material", "Terrain-Voxel")),
    PL_ACCESSOR_PROPERTY("BaseMaterialIndex", GetBaseMaterial, SetBaseMaterial)->AddAttributes(new plClampValueAttribute(0, 15)),
    PL_ACCESSOR_PROPERTY("FillHeight", GetFillHeight, SetFillHeight)->AddAttributes(new plDefaultValueAttribute(-100.0f), new plClampValueAttribute(-100.0f, 100.0f), new plMinValueTextAttribute("Off")),
    PL_ACCESSOR_PROPERTY("EnableCollider", GetEnableCollider, SetEnableCollider)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_SET_ACCESSOR_PROPERTY("TerrainTags", GetTags, Reflection_SetTag, Reflection_RemoveTag)->AddAttributes(new plTagSetWidgetAttribute("Terrain")),
    PL_ARRAY_ACCESSOR_PROPERTY("Surfaces", Surfaces_GetCount, Surfaces_GetValue, Surfaces_SetValue, Surfaces_Insert, Surfaces_Remove)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Surface", plDependencyFlags::Package)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgTransformChanged, OnMsgTransformChanged),
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Terrain"),
    new plBoxVisualizerAttribute("Size", 1.0f, plColorScheme::LightUI(plColorScheme::Green), nullptr, plVisualizerAnchor::NegX | plVisualizerAnchor::NegY | plVisualizerAnchor::NegZ),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plTerrainVoxelComponent::plTerrainVoxelComponent() = default;
plTerrainVoxelComponent::~plTerrainVoxelComponent() = default;

void plTerrainVoxelComponent::SetResolution(plEnum<plTerrainResolution> eResolution)
{
  if (m_eResolution == eResolution)
    return;

  m_eResolution = eResolution;

  if (m_uiVoxelIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
    {
      pSystem->RemoveVoxelVolume(m_uiVoxelIndex);
      const float fVoxelSize = m_fSize / static_cast<float>(m_eResolution.GetValue());
      m_uiVoxelIndex = pSystem->CreateVoxelVolume(m_eResolution.GetValue(), fVoxelSize);
      auto& vol = pSystem->ModifyVoxelVolume(m_uiVoxelIndex);
      vol.m_GlobalTransform = GetOwner()->GetGlobalTransform();
      vol.m_uiInitialSolid = (m_fFillHeight == -100.0f) ? 0u : 1u;
      vol.m_fFillHeight = m_fFillHeight;
    }
  }

  TriggerLocalBoundsUpdate();
}

void plTerrainVoxelComponent::SetSize(float f)
{
  if (m_fSize == f)
    return;

  m_fSize = f;

  if (m_uiVoxelIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
    {
      pSystem->ModifyVoxelVolume(m_uiVoxelIndex).m_fVoxelSize = m_fSize / static_cast<float>(m_eResolution.GetValue());
    }
  }

  TriggerLocalBoundsUpdate();
}

void plTerrainVoxelComponent::SetMaterial(const plMaterialResourceHandle& hMaterial)
{
  m_hMaterial = hMaterial;
  InvalidateCachedRenderData();
}

void plTerrainVoxelComponent::SetBaseMaterial(plUInt8 i)
{
  m_uiBaseMaterial = i;
}

void plTerrainVoxelComponent::SetFillHeight(float f)
{
  if (m_fFillHeight == f)
    return;

  m_fFillHeight = f;

  if (m_uiVoxelIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
    {
      pSystem->ModifyVoxelVolume(m_uiVoxelIndex).m_fFillHeight = f;
      pSystem->ModifyVoxelVolume(m_uiVoxelIndex).m_uiInitialSolid = (m_fFillHeight == -100.0f) ? 0u : 1u;
    }
  }
}

void plTerrainVoxelComponent::SetEnableCollider(bool bEnable)
{
  m_bEnableCollider = bEnable;
}

void plTerrainVoxelComponent::Reflection_SetTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  const plTag& tag = plTagRegistry::GetGlobalRegistry().RegisterTag(szTagName);
  if (m_Tags.IsSet(tag))
    return;
  m_Tags.Set(tag);
  if (m_uiVoxelIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
      pSystem->ModifyVoxelVolume(m_uiVoxelIndex).m_Tags = m_Tags;
  }
}

void plTerrainVoxelComponent::Reflection_RemoveTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  if (const plTag* pTag = plTagRegistry::GetGlobalRegistry().GetTagByName(plTempHashedString(szTagName)))
  {
    if (!m_Tags.IsSet(*pTag))
      return;
    m_Tags.Remove(*pTag);
    if (m_uiVoxelIndex != plInvalidIndex)
    {
      if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
        pSystem->ModifyVoxelVolume(m_uiVoxelIndex).m_Tags = m_Tags;
    }
  }
}

plUInt32 plTerrainVoxelComponent::Surfaces_GetCount() const
{
  return m_Surfaces.GetCount();
}

plString plTerrainVoxelComponent::Surfaces_GetValue(plUInt32 uiIndex) const
{
  if (uiIndex >= m_Surfaces.GetCount() || !m_Surfaces[uiIndex].IsValid())
    return {};
  return m_Surfaces[uiIndex].GetResourceID();
}

void plTerrainVoxelComponent::Surfaces_SetValue(plUInt32 uiIndex, plString sValue)
{
  m_Surfaces.EnsureCount(uiIndex + 1);
  if (!sValue.IsEmpty())
    m_Surfaces[uiIndex] = plResourceManager::LoadResource<plSurfaceResource>(sValue);
  else
    m_Surfaces[uiIndex].Invalidate();
}

void plTerrainVoxelComponent::Surfaces_Insert(plUInt32 uiIndex, plString sValue)
{
  plSurfaceResourceHandle hSurface;
  if (!sValue.IsEmpty())
    hSurface = plResourceManager::LoadResource<plSurfaceResource>(sValue);
  m_Surfaces.InsertAt(uiIndex, hSurface);
}

void plTerrainVoxelComponent::Surfaces_Remove(plUInt32 uiIndex)
{
  m_Surfaces.RemoveAtAndCopy(uiIndex);
}

void plTerrainVoxelComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();
  s << m_hMaterial;
  s << m_eResolution.GetValue();
  s << m_fSize;
  s << m_uiBaseMaterial;
  s << m_fFillHeight;
  s << m_bEnableCollider;
  m_Tags.Save(s);

  const plUInt32 uiNumSurfaces = m_Surfaces.GetCount();
  s << uiNumSurfaces;
  for (const auto& hSurface : m_Surfaces)
    s << hSurface;
}

void plTerrainVoxelComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();
  s >> m_hMaterial;
  plUInt16 uiRes = 0;
  s >> uiRes;
  m_eResolution = static_cast<plTerrainResolution::Enum>(uiRes);
  s >> m_fSize;
  s >> m_uiBaseMaterial;
  s >> m_fFillHeight;
  s >> m_bEnableCollider;
  m_Tags.Load(s, plTagRegistry::GetGlobalRegistry());

  plUInt32 uiNumSurfaces = 0;
  s >> uiNumSurfaces;
  m_Surfaces.SetCount(uiNumSurfaces);
  for (auto& hSurface : m_Surfaces)
    s >> hSurface;
}

void plTerrainVoxelComponent::OnActivated()
{
  SUPER::OnActivated();

  GetOwner()->EnableStaticTransformChangesNotifications();

  auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();
  const float fVoxelSize = m_fSize / static_cast<float>(m_eResolution.GetValue());
  m_uiVoxelIndex = pSystem->CreateVoxelVolume(m_eResolution.GetValue(), fVoxelSize);
  auto& vol = pSystem->ModifyVoxelVolume(m_uiVoxelIndex);
  vol.m_GlobalTransform = GetOwner()->GetGlobalTransform();
  vol.m_uiInitialSolid = (m_fFillHeight == -100.0f) ? 0u : 1u;
  vol.m_fFillHeight = m_fFillHeight;
  vol.m_Tags = m_Tags;

  TriggerLocalBoundsUpdate();
}

void plTerrainVoxelComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();
  pSystem->SetNeedsCollisionReadback(m_uiVoxelIndex, m_bEnableCollider);
}

void plTerrainVoxelComponent::OnDeactivated()
{
  if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
  {
    pSystem->RemoveVoxelVolume(m_uiVoxelIndex);
  }

  if (!m_hColliderObject.IsInvalidated())
  {
    GetWorld()->DeleteObjectDelayed(m_hColliderObject);
    m_hColliderObject.Invalidate();
  }

  if (auto* pRDM = GetWorld()->GetModule<plRenderDataManager>())
  {
    pRDM->DeleteInstanceData(m_InstanceDataOffset);
  }

  m_uiVoxelIndex = plInvalidIndex;

  SUPER::OnDeactivated();
}

plResult plTerrainVoxelComponent::GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg)
{
  ref_bounds = plBoundingBoxSphere::MakeFromBox(plBoundingBox::MakeFromMinMax(plVec3::MakeZero(), plVec3(m_fSize)));
  return PL_SUCCESS;
}

void plTerrainVoxelComponent::OnMsgTransformChanged(plMsgTransformChanged& msg)
{
  if (m_uiVoxelIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
    {
      auto& vol = pSystem->ModifyVoxelVolume(m_uiVoxelIndex);
      vol.m_GlobalTransform = msg.m_NewGlobalTransform;
    }
  }
}

void plTerrainVoxelComponent::UpdateVoxelMesh()
{
  if (m_uiVoxelIndex == plInvalidIndex)
    return;

  auto* pSystem = GetWorld()->GetModule<plTerrainSystem>();
  if (pSystem == nullptr)
    return;

  // Rendering path is fully GPU-driven via indirect draw — nothing to poll here.
  // Collision path: triggered on demand via SetNeedsCollisionReadback, not every frame.
  if (pSystem->IsVoxelMeshDataReady(m_uiVoxelIndex))
  {
    auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
    if (pPhysics != nullptr)
    {
      const plArrayPtr<const VoxelGpuVertex> cpuVerts = pSystem->GetCpuMeshVertices(m_uiVoxelIndex);
      const plArrayPtr<const plUInt32> cpuIdxs = pSystem->GetCpuMeshIndices(m_uiVoxelIndex);
      const plUInt32 uiVertCount = pSystem->GetCpuMeshVertexCount(m_uiVoxelIndex);
      const plUInt32 uiTriCount = pSystem->GetCpuMeshPrimitiveCount(m_uiVoxelIndex);

      if (uiVertCount > 0 && uiTriCount > 0)
      {
        plGameObject* pColliderObject = nullptr;
        if (!GetWorld()->TryGetObject(m_hColliderObject, pColliderObject))
        {
          plGameObjectDesc objDesc;
          objDesc.m_hParent = GetOwner()->GetHandle();
          objDesc.m_sName.Assign("VoxelCollider");
          m_hColliderObject = GetWorld()->CreateObject(objDesc, pColliderObject);
        }

        plPhysicsWorldModuleInterface::TriangleMeshColliderData data;

        data.m_Vertices.SetCountUninitialized(uiVertCount);
        for (plUInt32 i = 0; i < uiVertCount; ++i)
        {
          data.m_Vertices[i] = cpuVerts[i].Position;
        }

        const plUInt32 uiIdxCount = uiTriCount * 3;
        data.m_Indices.SetCountUninitialized(uiIdxCount);
        plMemoryUtils::Copy(data.m_Indices.GetData(), cpuIdxs.GetPtr(), uiIdxCount);

        if (!m_Surfaces.IsEmpty())
        {
          data.m_Surfaces = m_Surfaces;
          const plUInt8 uiFallback = m_uiBaseMaterial;
          data.m_TriangleMaterials.SetCountUninitialized(uiTriCount);

          for (plUInt32 tri = 0; tri < uiTriCount; ++tri)
          {
            plUInt8 votes[3];
            for (plUInt32 v = 0; v < 3; ++v)
            {
              const plUInt32 idx = data.m_Indices[tri * 3 + v];
              const VoxelGpuVertex& vert = cpuVerts[idx];
              const plUInt8 raw = (vert.Material != 0xFFFFFFFFu) ? static_cast<plUInt8>(vert.Material) : 0;
              votes[v] = (vert.MaterialStrength > 0.5f && raw < m_Surfaces.GetCount()) ? raw : uiFallback;
            }
            // Majority vote; ties go to votes[0]
            plUInt8 chosen = votes[0];
            if (votes[1] == votes[2] && votes[1] != votes[0])
              chosen = votes[1];
            data.m_TriangleMaterials[tri] = chosen;
          }
        }

        pPhysics->CreateTriangleMeshCollider(pColliderObject, data);
      }
      else if (!m_hColliderObject.IsInvalidated())
      {
        plGameObject* pColliderObject = nullptr;
        if (GetWorld()->TryGetObject(m_hColliderObject, pColliderObject))
        {
          pPhysics->RemoveTriangleMeshCollider(pColliderObject);
        }
      }
    }

    pSystem->ClearVoxelMeshDataReady(m_uiVoxelIndex);
  }
}

void plTerrainVoxelComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (!m_hMaterial.IsValid() || m_uiVoxelIndex == plInvalidIndex)
    return;

  const auto* pSystem = GetWorld()->GetModule<plTerrainSystem>();
  if (pSystem == nullptr || !pSystem->IsGpuMeshReady(m_uiVoxelIndex))
    return;

  const plGALBufferHandle hVerts = pSystem->GetGpuMeshVertexBuffer(m_uiVoxelIndex);
  const plGALBufferHandle hIdxs = pSystem->GetGpuMeshIndexBuffer(m_uiVoxelIndex);
  if (hVerts.IsInvalidated() || hIdxs.IsInvalidated())
    return;

  const bool bDynamic = GetOwner()->IsDynamic();
  auto hInstanceDataBuffer = msg.m_pRenderDataManager->GetOrCreateInstanceDataAndFill(
    *this, bDynamic, GetOwner()->GetGlobalTransform(), m_InstanceDataOffset,
    GetUniqueIdForRendering(), plColor::White, plVec4(0, 1, 0, 1));

  plTerrainVoxelGpuRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plTerrainVoxelGpuRenderData>(GetOwner());
  pRenderData->m_uiNumInstances = 1;
  pRenderData->m_DataOffsets.m_uiInstance = m_InstanceDataOffset.m_uiOffset;
  pRenderData->m_hInstanceDataBuffer = hInstanceDataBuffer;
  pRenderData->m_hMaterial = m_hMaterial;
  pRenderData->m_hGpuMeshVertices = hVerts;
  pRenderData->m_hGpuMeshIndices = hIdxs;
  pRenderData->m_hGpuMeshDrawArgs = pSystem->GetGpuMeshDrawArgsBuffer(m_uiVoxelIndex);
  pRenderData->m_uiBaseMaterialIndex = m_uiBaseMaterial;
  pRenderData->m_uiSortingKey = 0;

  msg.AddRenderData(pRenderData, plDefaultRenderDataCategories::LitOpaque, plRenderData::Caching::Never);
}

//////////////////////////////////////////////////////////////////////////

void plTerrainVoxelComponentManager::Initialize()
{
  SUPER::Initialize();

  auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plTerrainVoxelComponentManager::Update, this);
  desc.m_Phase = plWorldUpdatePhase::PostAsync;
  desc.m_bOnlyUpdateWhenSimulating = false;
  RegisterUpdateFunction(desc);
}

void plTerrainVoxelComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  for (auto it = m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    plTerrainVoxelComponent* pComp = it;
    if (pComp->IsActive())
    {
      pComp->UpdateVoxelMesh();
    }
  }
}
