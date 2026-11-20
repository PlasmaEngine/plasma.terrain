#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/Graphics/Camera.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Messages/TransformChangedMessage.h>
#include <Core/Physics/SurfaceResource.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Reflection/Implementation/PropertyAttributes.h>
#include <Foundation/Types/TagRegistry.h>
#include <RendererCore/Components/RenderComponent.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <RendererCore/Pipeline/View.h>
#include <TerrainPlugin/Components/TerrainHeightfieldComponent.h>
#include <TerrainPlugin/Rendering/TerrainRenderData.h>
#include <TerrainPlugin/TerrainSystem.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plTerrainHeightfieldComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_ACCESSOR_PROPERTY("Resolution", plTerrainResolution, GetResolution, SetResolution),
    PL_ACCESSOR_PROPERTY("Size", GetSize, SetSize)->AddAttributes(new plClampValueAttribute(16.0f, 256.0), new plDefaultValueAttribute(32.0f)),
    PL_RESOURCE_ACCESSOR_PROPERTY("Material", GetMaterial, SetMaterial)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Material", "Terrain-Heightfield")),
    PL_RESOURCE_ACCESSOR_PROPERTY("HeightImage", GetHeightImage, SetHeightImage)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Data_2D")),
    PL_ACCESSOR_PROPERTY("HeightImageOffset", GetHeightImageOffset, SetHeightImageOffset),
    PL_ACCESSOR_PROPERTY("HeightImageSize", GetHeightImageSize, SetHeightImageSize)->AddAttributes(new plDefaultValueAttribute(plVec2(1.0f))),
    PL_ACCESSOR_PROPERTY("HeightScale", GetHeightScale, SetHeightScale)->AddAttributes(new plDefaultValueAttribute(32.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_RESOURCE_ACCESSOR_PROPERTY("LayerMap", GetLayerImage, SetLayerImage)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Data_2D")),
    PL_ACCESSOR_PROPERTY("BaseMaterialIndex", GetMaterialIndex, SetMaterialIndex)->AddAttributes(new plClampValueAttribute(0, 31)),
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

plTerrainHeightfieldComponent::plTerrainHeightfieldComponent() = default;
plTerrainHeightfieldComponent::~plTerrainHeightfieldComponent() = default;

void plTerrainHeightfieldComponent::SetResolution(plEnum<plTerrainResolution> eResolution)
{
  if (m_eResolution != eResolution)
  {
    if (m_uiHeightfieldIndex != plInvalidIndex)
    {
      if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
      {
        pSystem->RemoveHeightfield(m_uiHeightfieldIndex);

        m_uiHeightfieldIndex = pSystem->CreateHeightfield((plUInt32)eResolution.GetValue());
        pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_uiDefaultMaterialIndex = m_uiMaterialIndex;

        auto& data = pSystem->ModifyHeightfield(m_uiHeightfieldIndex);
        data.m_fGridSpacing = m_fSize / static_cast<float>(eResolution.GetValue());
        data.m_GlobalTransform = GetOwner()->GetGlobalTransform();
      }
    }

    m_eResolution = eResolution;
    TriggerLocalBoundsUpdate();
  }
}

void plTerrainHeightfieldComponent::SetSize(float fSize)
{
  if (m_fSize != fSize)
  {
    m_fSize = fSize;

    if (m_uiHeightfieldIndex != plInvalidIndex)
    {
      if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
      {
        auto& data = pSystem->ModifyHeightfield(m_uiHeightfieldIndex);
        data.m_fGridSpacing = m_fSize / static_cast<float>(m_eResolution.GetValue());
      }
    }

    TriggerLocalBoundsUpdate();
  }
}

void plTerrainHeightfieldComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_hMaterial;
  s << m_eResolution.GetValue();
  s << m_fSize;
  s << m_bEnableCollider;
  s << m_uiMaterialIndex;

  const plUInt32 uiNumSurfaces = m_Surfaces.GetCount();
  s << uiNumSurfaces;
  for (const auto& hSurface : m_Surfaces)
    s << hSurface;
  m_Tags.Save(s);

  s << m_hHeightImage;
  s << m_vImageOffset;
  s << m_vImageSize;
  s << m_fHeightScale;

  // version 2
  s << m_hLayerImage;
}

void plTerrainHeightfieldComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());

  auto& s = inout_stream.GetStream();
  s >> m_hMaterial;
  plUInt16 uiRes = 0;
  s >> uiRes;
  m_eResolution = static_cast<plTerrainResolution::Enum>(uiRes);
  s >> m_fSize;
  s >> m_bEnableCollider;
  s >> m_uiMaterialIndex;

  plUInt32 uiNumSurfaces = 0;
  s >> uiNumSurfaces;
  m_Surfaces.SetCount(uiNumSurfaces);

  for (auto& hSurface : m_Surfaces)
    s >> hSurface;
  m_Tags.Load(s, plTagRegistry::GetGlobalRegistry());

  s >> m_hHeightImage;
  s >> m_vImageOffset;
  s >> m_vImageSize;
  s >> m_fHeightScale;

  if (uiVersion >= 2)
    s >> m_hLayerImage;
}

void plTerrainHeightfieldComponent::SetMaterial(const plMaterialResourceHandle& hMaterial)
{
  m_hMaterial = hMaterial;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->SetHeightfieldMaterial(m_uiHeightfieldIndex, hMaterial);
  }
}

void plTerrainHeightfieldComponent::SetHeightImage(const plImageDataResourceHandle& hImage)
{
  m_hHeightImage = hImage;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_hHeightImage = hImage;
  }
}

void plTerrainHeightfieldComponent::SetLayerImage(const plImageDataResourceHandle& hImage)
{
  m_hLayerImage = hImage;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_hLayerImage = hImage;
  }
}

void plTerrainHeightfieldComponent::SetHeightImageOffset(plVec2 vOffset)
{
  m_vImageOffset = vOffset;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_vImageOffset = vOffset;
  }
}

void plTerrainHeightfieldComponent::SetHeightImageSize(plVec2 vSize)
{
  m_vImageSize = vSize;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_vImageSize = vSize;
  }
}

void plTerrainHeightfieldComponent::SetHeightScale(float fScale)
{
  m_fHeightScale = fScale;
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_fHeightScale = fScale;
  }
}

void plTerrainHeightfieldComponent::Reflection_SetTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  const plTag& tag = plTagRegistry::GetGlobalRegistry().RegisterTag(szTagName);
  if (m_Tags.IsSet(tag))
    return;
  m_Tags.Set(tag);
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
      pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_Tags = m_Tags;
  }
}

void plTerrainHeightfieldComponent::Reflection_RemoveTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  if (const plTag* pTag = plTagRegistry::GetGlobalRegistry().GetTagByName(plTempHashedString(szTagName)))
  {
    if (!m_Tags.IsSet(*pTag))
      return;
    m_Tags.Remove(*pTag);
    if (m_uiHeightfieldIndex != plInvalidIndex)
    {
      if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
        pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_Tags = m_Tags;
    }
  }
}

void plTerrainHeightfieldComponent::OnActivated()
{
  SUPER::OnActivated();

  GetOwner()->EnableStaticTransformChangesNotifications();

  auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();

  m_uiHeightfieldIndex = pSystem->CreateHeightfield((plUInt32)m_eResolution.GetValue());

  auto& data = pSystem->ModifyHeightfield(m_uiHeightfieldIndex);
  data.m_uiDefaultMaterialIndex = m_uiMaterialIndex;
  data.m_fGridSpacing = m_fSize / static_cast<float>(m_eResolution.GetValue());
  data.m_GlobalTransform = GetOwner()->GetGlobalTransform();
  data.m_Tags = m_Tags;
  data.m_hHeightImage = m_hHeightImage;
  data.m_vImageOffset = m_vImageOffset;
  data.m_vImageSize = m_vImageSize;
  data.m_fHeightScale = m_fHeightScale;
  data.m_hLayerImage = m_hLayerImage;
  pSystem->SetHeightfieldMaterial(m_uiHeightfieldIndex, m_hMaterial);

  TriggerLocalBoundsUpdate();
}

void plTerrainHeightfieldComponent::OnDeactivated()
{
  if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
  {
    pSystem->RemoveHeightfield(m_uiHeightfieldIndex);
  }

  if (auto* pRDM = GetWorld()->GetModule<plRenderDataManager>())
  {
    pRDM->DeleteInstanceData(m_InstanceDataOffset);
  }

  DestroyColliderObject();

  SUPER::OnDeactivated();
}

plResult plTerrainHeightfieldComponent::GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg)
{
  const plVec3 vMin(0.0f, 0.0f, 0.0f);
  const plVec3 vMax(m_fSize, m_fSize, m_fSize);

  ref_bounds = plBoundingBoxSphere::MakeFromBox(plBoundingBox::MakeFromMinMax(vMin, vMax));
  return PL_SUCCESS;
}

void plTerrainHeightfieldComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (m_uiHeightfieldIndex == plInvalidIndex)
    return;

  const auto* pSystem = GetWorld()->GetModule<plTerrainSystem>();
  if (pSystem == nullptr)
    return;

  const plGALBufferHandle hHeightBuffer = pSystem->GetHeightBuffer(m_uiHeightfieldIndex);
  if (hHeightBuffer.IsInvalidated())
    return;

  const plGALBufferHandle hNormalBuffer = pSystem->GetNormalBuffer(m_uiHeightfieldIndex);
  const plGALBufferHandle hMaskBuffer = pSystem->GetMaskBuffer(m_uiHeightfieldIndex);

  // Create instance data so the pixel shader can read GameObjectID for editor picking.
  auto hInstanceDataBuffer = msg.m_pRenderDataManager->GetOrCreateInstanceDataAndFill(
    *this, true, GetOwner()->GetGlobalTransform(), m_InstanceDataOffset, GetUniqueIdForRendering());

  plTerrainRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plTerrainRenderData>(GetOwner());
  pRenderData->m_hMaterial = m_hMaterial;
  pRenderData->m_hHeightBuffer = hHeightBuffer;
  pRenderData->m_hNormalBuffer = hNormalBuffer;
  pRenderData->m_hMaskBuffer = hMaskBuffer;
  pRenderData->m_uiCellsPerSide = (plUInt32)m_eResolution.GetValue();
  pRenderData->m_fGridSpacing = m_fSize / static_cast<float>(m_eResolution.GetValue());
  pRenderData->m_uiSortingKey = 0;
  pRenderData->m_uiNumInstances = 1;
  pRenderData->m_DataOffsets.m_uiInstance = m_InstanceDataOffset.m_uiOffset;
  pRenderData->m_hInstanceDataBuffer = hInstanceDataBuffer;

  msg.AddRenderData(pRenderData, plDefaultRenderDataCategories::LitOpaque, plRenderData::Caching::Never);
}

void plTerrainHeightfieldComponent::OnMsgTransformChanged(plMsgTransformChanged& msg)
{
  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();
    auto& data = pSystem->ModifyHeightfield(m_uiHeightfieldIndex);
    data.m_GlobalTransform = msg.m_NewGlobalTransform;
  }
}

void plTerrainHeightfieldComponent::SetEnableCollider(bool bEnable)
{
  m_bEnableCollider = bEnable;
}

void plTerrainHeightfieldComponent::DestroyColliderObject()
{
  if (!m_hColliderObject.IsInvalidated())
  {
    GetWorld()->DeleteObjectDelayed(m_hColliderObject);
    m_hColliderObject.Invalidate();
  }
}


void plTerrainHeightfieldComponent::SetMaterialIndex(plUInt8 uiIndex)
{
  if (m_uiMaterialIndex == uiIndex)
    return;

  m_uiMaterialIndex = uiIndex;

  if (m_uiHeightfieldIndex != plInvalidIndex)
  {
    auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();
    pSystem->ModifyHeightfield(m_uiHeightfieldIndex).m_uiDefaultMaterialIndex = uiIndex;
  }
}

plUInt32 plTerrainHeightfieldComponent::Surfaces_GetCount() const
{
  return m_Surfaces.GetCount();
}

plString plTerrainHeightfieldComponent::Surfaces_GetValue(plUInt32 uiIndex) const
{
  if (uiIndex >= m_Surfaces.GetCount() || !m_Surfaces[uiIndex].IsValid())
    return {};
  return m_Surfaces[uiIndex].GetResourceID();
}

void plTerrainHeightfieldComponent::Surfaces_SetValue(plUInt32 uiIndex, plString sValue)
{
  m_Surfaces.EnsureCount(uiIndex + 1);
  if (!sValue.IsEmpty())
    m_Surfaces[uiIndex] = plResourceManager::LoadResource<plSurfaceResource>(sValue);
  else
    m_Surfaces[uiIndex].Invalidate();
}

void plTerrainHeightfieldComponent::Surfaces_Insert(plUInt32 uiIndex, plString sValue)
{
  plSurfaceResourceHandle hSurface;
  if (!sValue.IsEmpty())
    hSurface = plResourceManager::LoadResource<plSurfaceResource>(sValue);
  m_Surfaces.InsertAt(uiIndex, hSurface);
}

void plTerrainHeightfieldComponent::Surfaces_Remove(plUInt32 uiIndex)
{
  m_Surfaces.RemoveAtAndCopy(uiIndex);
}

void plTerrainHeightfieldComponent::UpdateCollider()
{
  if (m_uiHeightfieldIndex == plInvalidIndex)
    return;

  auto* pSystem = GetWorld()->GetModule<plTerrainSystem>();
  if (pSystem == nullptr)
    return;

  const bool bShouldCollide = m_bEnableCollider && GetWorld()->GetWorldSimulationEnabled();

  pSystem->SetNeedsColliderReadback(m_uiHeightfieldIndex, bShouldCollide);

  if (!bShouldCollide)
  {
    DestroyColliderObject();
    return;
  }

  if (!pSystem->IsColliderDataReady(m_uiHeightfieldIndex))
    return;

  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  if (pPhysics == nullptr)
  {
    pSystem->ClearColliderDataReady(m_uiHeightfieldIndex);
    return;
  }

  const plArrayPtr<const float> bakedHeights = pSystem->GetCpuBakedHeights(m_uiHeightfieldIndex);
  const plArrayPtr<const plUInt8> dominantIndices = pSystem->GetCpuBakedMaterialIndices(m_uiHeightfieldIndex);
  const plUInt32 C = pSystem->GetCellsPerSide(m_uiHeightfieldIndex);
  // Jolt requires vertex count multiple of 4; pad render vertex count (C+1) up to next multiple.
  const plUInt32 N = ((C + 1) + 3u) & ~3u;
  const float fGridSpacing = m_fSize / static_cast<float>(C);
  // Physical extent: N-1 quads at fGridSpacing each (may be slightly larger than visual patch).
  const float fJoltHalfExtent = static_cast<float>(N - 1) * fGridSpacing * 0.5f;

  // Sentinel index 0xFF marks "discard" vertices written by Carve brushes. The corresponding Jolt sample
  // becomes cNoCollisionValue (FLT_MAX) so any quad touching it has no collision (cave / hole entrances).
  const bool bHasDominantIndices = dominantIndices.GetCount() == bakedHeights.GetCount();
  const float fJoltNoCollision = std::numeric_limits<float>::max();

  plPhysicsWorldModuleInterface::HeightfieldColliderData data;
  data.m_vHalfExtents = plVec2(fJoltHalfExtent, fJoltHalfExtent);
  data.m_uiResolution = N;

  data.m_Heights.SetCountUninitialized(N * N);
  for (plUInt32 i = 0; i < N * N; ++i)
  {
    if (bHasDominantIndices && dominantIndices[i] == 0xFFu)
      data.m_Heights[i] = fJoltNoCollision;
    else
      data.m_Heights[i] = bakedHeights[i];
  }

  // Fill per-quad material indices from the dominant material index of the top-left vertex of each quad.
  if (!m_Surfaces.IsEmpty())
  {
    if (!dominantIndices.IsEmpty())
    {
      const plUInt32 uiNumQuads = (N - 1) * (N - 1);
      data.m_MaterialIndices.SetCountUninitialized(uiNumQuads);
      for (plUInt32 row = 0; row < N - 1; ++row)
      {
        // Jolt flips height rows (srcRow = N-1-row) so material indices must mirror the same flip.
        const plUInt32 srcRow = N - 1 - row;
        for (plUInt32 col = 0; col < N - 1; ++col)
        {
          const plUInt8 matIdx = dominantIndices[srcRow * N + col];
          data.m_MaterialIndices[row * (N - 1) + col] = (matIdx < m_Surfaces.GetCount()) ? matIdx : 0;
        }
      }

      data.m_Surfaces = m_Surfaces;
    }
  }

  if (m_hColliderObject.IsInvalidated())
  {
    plGameObjectDesc objDesc;
    objDesc.m_hParent = GetOwner()->GetHandle();
    objDesc.m_sName.Assign("TerrainCollider");
    objDesc.m_LocalPosition.x = fJoltHalfExtent;
    objDesc.m_LocalPosition.y = fJoltHalfExtent;
    objDesc.m_LocalPosition.z = 0.0f;
    plGameObject* pNewObject = nullptr;
    m_hColliderObject = GetWorld()->CreateObject(objDesc, pNewObject);
  }

  plGameObject* pColliderObject = nullptr;
  if (GetWorld()->TryGetObject(m_hColliderObject, pColliderObject))
  {
    plVec3 newLocalPos;
    newLocalPos.x = fJoltHalfExtent;
    newLocalPos.y = fJoltHalfExtent;
    newLocalPos.z = 0.0f;
    pColliderObject->SetLocalPosition(newLocalPos);

    pPhysics->CreateHeightfieldCollider(pColliderObject, {}, data);
  }

  pSystem->ClearColliderDataReady(m_uiHeightfieldIndex);
}

//////////////////////////////////////////////////////////////////////////

plTerrainPatchComponentManager::plTerrainPatchComponentManager(plWorld* pWorld)
  : SUPER(pWorld)
{
  plResourceManager::GetResourceEvents().AddEventHandler(plMakeDelegate(&plTerrainPatchComponentManager::ResourceEventHandler, this));
}

plTerrainPatchComponentManager::~plTerrainPatchComponentManager()
{
  plResourceManager::GetResourceEvents().RemoveEventHandler(plMakeDelegate(&plTerrainPatchComponentManager::ResourceEventHandler, this));
}

void plTerrainPatchComponentManager::ResourceEventHandler(const plResourceEvent& e)
{
  if (e.m_Type == plResourceEvent::Type::ResourceContentUpdated && e.m_pResource->GetDynamicRTTI()->IsDerivedFrom<plImageDataResource>())
  {
    plImageDataResourceHandle hResource((plImageDataResource*)e.m_pResource);
    for (auto it = GetComponents(); it.IsValid(); it.Next())
    {
      if (it->m_hHeightImage == hResource || it->m_hLayerImage == hResource)
        it->m_bHeightImageDirty = true;
    }
  }
}

void plTerrainPatchComponentManager::Initialize()
{
  SUPER::Initialize();

  auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plTerrainPatchComponentManager::Update, this);
  desc.m_Phase = plWorldUpdatePhase::PostAsync;
  desc.m_bOnlyUpdateWhenSimulating = false;
  RegisterUpdateFunction(desc);
}

void plTerrainPatchComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  for (auto it = m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    plTerrainHeightfieldComponent* pComp = it;
    if (pComp->IsActive())
    {
      if (pComp->m_bHeightImageDirty && pComp->m_uiHeightfieldIndex != plInvalidIndex)
      {
        if (auto* pSystem = GetWorld()->GetModule<plTerrainSystem>())
          pSystem->ModifyHeightfield(pComp->m_uiHeightfieldIndex); // marks patch dirty for rebake
        pComp->m_bHeightImageDirty = false;
      }

      pComp->UpdateCollider();
    }
  }
}
