#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <TerrainPlugin/Components/TerrainBrush3DComponent.h>
#include <TerrainPlugin/TerrainSystem.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plTerrainModifyMode3D, 1)
  PL_ENUM_CONSTANT(plTerrainModifyMode3D::OnlyPaint3D),
  PL_ENUM_CONSTANT(plTerrainModifyMode3D::Carve),
  PL_ENUM_CONSTANT(plTerrainModifyMode3D::Add),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plTerrainBrush3DComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_ACCESSOR_PROPERTY("ModifyMode", plTerrainModifyMode3D, GetModifyMode, SetModifyMode),
    PL_ACCESSOR_PROPERTY("HalfSizeYBottom", GetHalfSizeYBottom, SetHalfSizeYBottom)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("HalfSizeYTop", GetHalfSizeYTop, SetHalfSizeYTop)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("HalfSizeZ", GetHalfSizeZ, SetHalfSizeZ)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Terrain"),
    new plShapeIconAlwaysVisibleAttribute(),
    new plRoundedBoxVisualizerAttribute("HalfSizeX", "HalfSizeYBottom", "HalfSizeYTop", "HalfSizeZ", "InnerRadius", plColor::Yellow, "OuterRadius", plColor::GreenYellow, plVec3(0, 0, 0)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plTerrainBrush3DComponent::plTerrainBrush3DComponent() = default;
plTerrainBrush3DComponent::~plTerrainBrush3DComponent() = default;

void plTerrainBrush3DComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_eModifyMode;
  s << m_fHalfSizeYBottom;
  s << m_fHalfSizeYTop;
  s << m_fHalfSizeZ;
}

void plTerrainBrush3DComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s >> m_eModifyMode;
  s >> m_fHalfSizeYBottom;
  s >> m_fHalfSizeYTop;
  s >> m_fHalfSizeZ;
}

void plTerrainBrush3DComponent::SetModifyMode(plEnum<plTerrainModifyMode3D> mode)
{
  if (m_eModifyMode == mode)
    return;
  m_eModifyMode = mode;
  RefreshBrushes();
}

void plTerrainBrush3DComponent::SetHalfSizeYBottom(float fSize)
{
  if (m_fHalfSizeYBottom == fSize)
    return;
  m_fHalfSizeYBottom = fSize;
  RefreshBrushes();
}

void plTerrainBrush3DComponent::SetHalfSizeYTop(float fSize)
{
  if (m_fHalfSizeYTop == fSize)
    return;
  m_fHalfSizeYTop = fSize;
  RefreshBrushes();
}

void plTerrainBrush3DComponent::SetHalfSizeZ(float fSize)
{
  if (m_fHalfSizeZ == fSize)
    return;
  m_fHalfSizeZ = fSize;
  RefreshBrushes();
}

void plTerrainBrush3DComponent::FillBrushSpecificProperties(plTerrainData_PointBrush& brush, float /*fHalfSizeX*/)
{
  brush.m_eModifyMode = static_cast<plTerrainModifyMode::Enum>(m_eModifyMode.GetValue());
  brush.m_vHalfExtents.y = m_fHalfSizeYBottom;
  brush.m_fHalfExtentYTop = m_fHalfSizeYTop;
  brush.m_fHalfExtentZ = m_fHalfSizeZ;
}
