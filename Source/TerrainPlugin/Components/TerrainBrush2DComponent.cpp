#include <TerrainPlugin/TerrainPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <TerrainPlugin/Components/TerrainBrush2DComponent.h>
#include <TerrainPlugin/TerrainSystem.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plTerrainModifyMode2D, 1)
  PL_ENUM_CONSTANT(plTerrainModifyMode2D::OnlyPaint2D),
  PL_ENUM_CONSTANT(plTerrainModifyMode2D::Max),
  PL_ENUM_CONSTANT(plTerrainModifyMode2D::Min),
  PL_ENUM_CONSTANT(plTerrainModifyMode2D::Set),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plTerrainBrush2DComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_ACCESSOR_PROPERTY("ModifyMode", plTerrainModifyMode2D, GetModifyMode, SetModifyMode),
    PL_ACCESSOR_PROPERTY("HalfSizeY", GetHalfSizeY, SetHalfSizeY)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, plVariant())),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Terrain"),
    new plShapeIconAlwaysVisibleAttribute(),
    new plRoundedRectVisualizerAttribute("HalfSizeX", "HalfSizeY", "InnerRadius", plColor::Yellow, "OuterRadius", plColor::GreenYellow, plVec3(0, 0, 0)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plTerrainBrush2DComponent::plTerrainBrush2DComponent() = default;
plTerrainBrush2DComponent::~plTerrainBrush2DComponent() = default;

void plTerrainBrush2DComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_eModifyMode;
  s << m_fHalfSizeY;
}

void plTerrainBrush2DComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s >> m_eModifyMode;
  s >> m_fHalfSizeY;
}

void plTerrainBrush2DComponent::SetModifyMode(plEnum<plTerrainModifyMode2D> mode)
{
  if (m_eModifyMode == mode)
    return;
  m_eModifyMode = mode;
  RefreshBrushes();
}

void plTerrainBrush2DComponent::SetHalfSizeY(float fSize)
{
  if (m_fHalfSizeY == fSize)
    return;
  m_fHalfSizeY = fSize;
  RefreshBrushes();
}

void plTerrainBrush2DComponent::FillBrushSpecificProperties(plTerrainData_PointBrush& brush, float /*fHalfSizeX*/)
{
  brush.m_eModifyMode = static_cast<plTerrainModifyMode::Enum>(m_eModifyMode.GetValue());
  brush.m_vHalfExtents.y = m_fHalfSizeY;
  brush.m_fHalfExtentYTop = 0.0f;
  brush.m_fHalfExtentZ = 0.0f;
}
