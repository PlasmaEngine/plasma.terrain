#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/Messages/TransformChangedMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Reflection/Implementation/PropertyAttributes.h>
#include <Foundation/Types/TagRegistry.h>
#include <RendererCore/Components/SplineComponent.h>
#include <TerrainPlugin/Components/TerrainBrushBaseComponent.h>
#include <TerrainPlugin/TerrainSystem.h>

// clang-format off
PL_BEGIN_ABSTRACT_COMPONENT_TYPE(plTerrainBrushBaseComponent, 1)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("HalfSizeX", GetHalfSizeX, SetHalfSizeX)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("InnerRadius", GetInnerRadius, SetInnerRadius)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("OuterRadius", GetOuterRadius, SetOuterRadius)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("Falloff", GetFalloff, SetFalloff)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.1f, 10.0f)),
    PL_ACCESSOR_PROPERTY("Noise", GetNoise, SetNoise)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_ACCESSOR_PROPERTY("NoiseScale", GetNoiseScale, SetNoiseScale)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(1.0f, 100.0f)),
    PL_ACCESSOR_PROPERTY("MaterialIndex", GetMaterialIndex, SetMaterialIndex)->AddAttributes(new plClampValueAttribute(0, 31)),
    PL_ACCESSOR_PROPERTY("MaterialStrength", GetMaterialStrength, SetMaterialStrength)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_ACCESSOR_PROPERTY("AffectHeightfields", GetAffectsHeightfields, SetAffectsHeightfields)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("AffectVoxels", GetAffectsVoxels, SetAffectsVoxels)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("Priority", GetPriority, SetPriority)->AddAttributes(new plDefaultValueAttribute((plInt8)0), new plClampValueAttribute((plInt8)-128, (plInt8)127)),
    PL_SET_ACCESSOR_PROPERTY("TerrainTags", GetIncludeTags, Reflection_SetIncludeTag, Reflection_RemoveIncludeTag)->AddAttributes(new plTagSetWidgetAttribute("Terrain")),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgTransformChanged, OnMsgTransformChanged),
    PL_MESSAGE_HANDLER(plMsgSplineChanged, OnMsgSplineChanged),
  }
  PL_END_MESSAGEHANDLERS;
}
PL_END_ABSTRACT_COMPONENT_TYPE;
// clang-format on

plTerrainBrushBaseComponent::plTerrainBrushBaseComponent() = default;
plTerrainBrushBaseComponent::~plTerrainBrushBaseComponent() = default;

void plTerrainBrushBaseComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_fHalfSizeX;
  s << m_fInnerRadius;
  s << m_fOuterRadius;
  s << m_fFalloff;
  s << m_fNoise;
  s << m_fNoiseScale;
  s << m_uiMaterialIndex;
  s << m_fMaterialStrength;
  s << m_bAffectHeightfields;
  s << m_bAffectVoxels;
  s << m_iPriority;
  m_IncludeTags.Save(s);
}

void plTerrainBrushBaseComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s >> m_fHalfSizeX;
  s >> m_fInnerRadius;
  s >> m_fOuterRadius;
  s >> m_fFalloff;
  s >> m_fNoise;
  s >> m_fNoiseScale;
  s >> m_uiMaterialIndex;
  s >> m_fMaterialStrength;
  s >> m_bAffectHeightfields;
  s >> m_bAffectVoxels;
  s >> m_iPriority;
  m_IncludeTags.Load(s, plTagRegistry::GetGlobalRegistry());
}

void plTerrainBrushBaseComponent::OnActivated()
{
  SUPER::OnActivated();
  GetOwner()->EnableStaticTransformChangesNotifications();
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::OnDeactivated()
{
  ClearBrushes();
  SUPER::OnDeactivated();
}

void plTerrainBrushBaseComponent::OnMsgTransformChanged(plMsgTransformChanged& msg)
{
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::OnMsgSplineChanged(plMsgSplineChanged& msg)
{
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::ClearBrushes()
{
  if (m_BrushIndices.IsEmpty())
    return;

  auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();
  for (plUInt32 uiIdx : m_BrushIndices)
    pSystem->RemovePointBrush(uiIdx);
  m_BrushIndices.Clear();
}

void plTerrainBrushBaseComponent::FillBrush(plTerrainData_PointBrush& brush, const plTransform& transform, float fHalfSizeX)
{
  brush.m_vPosition = transform.m_vPosition;
  brush.m_qRotation = transform.m_qRotation;
  brush.m_vHalfExtents.x = fHalfSizeX;
  brush.m_fInnerRadius = m_fInnerRadius;
  brush.m_fOuterRadius = m_fOuterRadius;
  brush.m_fFalloff = m_fFalloff;
  brush.m_uiMaterialIndex = m_uiMaterialIndex;
  brush.m_fMaterialStrength = m_fMaterialStrength;
  brush.m_bAffectHeightfields = m_bAffectHeightfields;
  brush.m_bAffectVoxels = m_bAffectVoxels;
  brush.m_fNoise = m_fNoise;
  brush.m_fNoiseScale = m_fNoiseScale;
  brush.m_iPriority = m_iPriority;
  brush.m_IncludeTags = m_IncludeTags;
  FillBrushSpecificProperties(brush, fHalfSizeX);
}

void plTerrainBrushBaseComponent::RefreshBrushes()
{
  ClearBrushes();

  if (!IsActiveAndInitialized())
    return;

  auto* pSystem = GetWorld()->GetOrCreateModule<plTerrainSystem>();

  const plSplineComponent* pSpline = nullptr;
  if (GetOwner()->TryGetComponentOfBaseType(pSpline))
  {
    const float fTotalLength = pSpline->GetTotalLength();
    if (fTotalLength <= 0.0f)
      return;

    constexpr float fMaxStep = 10.0f;
    constexpr float fMinStep = 0.5f;
    constexpr float fTolerance = 0.1f;

    auto PlaceBrush = [&](float d0, float d1)
    {
      const float dMid = (d0 + d1) * 0.5f;
      const float fSegmentHalfLength = (d1 - d0) * 0.5f;
      const plTransform trans = pSpline->GetTransformAtDistance(dMid, plSplineComponentSpace::Global);

      const plUInt32 uiIdx = pSystem->CreatePointBrush();
      m_BrushIndices.PushBack(uiIdx);
      FillBrush(pSystem->ModifyPointBrush(uiIdx), trans, fSegmentHalfLength);
    };

    auto Subdivide = [&](auto& self, float d0, float d1) -> void
    {
      const float fLength = d1 - d0;
      const float dMid = (d0 + d1) * 0.5f;

      if (fLength <= fMinStep)
      {
        PlaceBrush(d0, d1);
        return;
      }

      if (fLength <= fMaxStep)
      {
        const plVec3 p0 = pSpline->GetTransformAtDistance(d0, plSplineComponentSpace::Global).m_vPosition;
        const plVec3 p1 = pSpline->GetTransformAtDistance(d1, plSplineComponentSpace::Global).m_vPosition;
        const plVec3 pMidActual = pSpline->GetTransformAtDistance(dMid, plSplineComponentSpace::Global).m_vPosition;

        if ((pMidActual - (p0 + p1) * 0.5f).GetLength() <= fTolerance)
        {
          PlaceBrush(d0, d1);
          return;
        }
      }

      self(self, d0, dMid);
      self(self, dMid, d1);
    };

    Subdivide(Subdivide, 0.0f, fTotalLength);
  }
  else
  {
    const plUInt32 uiIdx = pSystem->CreatePointBrush();
    m_BrushIndices.PushBack(uiIdx);
    FillBrush(pSystem->ModifyPointBrush(uiIdx), GetOwner()->GetGlobalTransform(), m_fHalfSizeX);
  }
}

void plTerrainBrushBaseComponent::SetHalfSizeX(float fSize)
{
  if (m_fHalfSizeX == fSize)
    return;
  m_fHalfSizeX = fSize;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetInnerRadius(float fRadius)
{
  if (m_fInnerRadius == fRadius)
    return;
  m_fInnerRadius = fRadius;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetOuterRadius(float fRadius)
{
  if (m_fOuterRadius == fRadius)
    return;
  m_fOuterRadius = fRadius;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetFalloff(float fFalloff)
{
  if (m_fFalloff == fFalloff)
    return;
  m_fFalloff = fFalloff;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetMaterialIndex(plUInt8 uiIndex)
{
  if (m_uiMaterialIndex == uiIndex)
    return;
  m_uiMaterialIndex = uiIndex;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetMaterialStrength(float fStrength)
{
  if (m_fMaterialStrength == fStrength)
    return;
  m_fMaterialStrength = fStrength;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetAffectsHeightfields(bool b)
{
  if (m_bAffectHeightfields == b)
    return;
  m_bAffectHeightfields = b;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetAffectsVoxels(bool b)
{
  if (m_bAffectVoxels == b)
    return;
  m_bAffectVoxels = b;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetNoise(float fNoise)
{
  if (m_fNoise == fNoise)
    return;
  m_fNoise = fNoise;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetNoiseScale(float fNoise)
{
  if (m_fNoiseScale == fNoise)
    return;
  m_fNoiseScale = fNoise;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::SetPriority(plInt8 iPriority)
{
  if (m_iPriority == iPriority)
    return;
  m_iPriority = iPriority;
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::Reflection_SetIncludeTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  const plTag& tag = plTagRegistry::GetGlobalRegistry().RegisterTag(szTagName);
  if (m_IncludeTags.IsSet(tag))
    return;
  m_IncludeTags.Set(tag);
  RefreshBrushes();
}

void plTerrainBrushBaseComponent::Reflection_RemoveIncludeTag(const char* szTagName)
{
  if (plStringUtils::IsNullOrEmpty(szTagName))
    return;
  if (const plTag* pTag = plTagRegistry::GetGlobalRegistry().GetTagByName(plTempHashedString(szTagName)))
  {
    if (!m_IncludeTags.IsSet(*pTag))
      return;
    m_IncludeTags.Remove(*pTag);
    RefreshBrushes();
  }
}
