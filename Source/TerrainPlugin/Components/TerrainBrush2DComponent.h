#pragma once

#include <TerrainPlugin/Components/TerrainBrushBaseComponent.h>

/// Brush modes available on a 2D (heightfield-style) terrain brush.
struct PL_TERRAINPLUGIN_DLL plTerrainModifyMode2D
{
  using StorageType = plUInt8;
  enum Enum : plUInt8
  {
    Max = 0,         ///< Only raises terrain toward the brush height.
    Min = 1,         ///< Only lowers terrain toward the brush height.
    Set = 2,         ///< Forces terrain to the brush height.
    OnlyPaint2D = 5, ///< Does not modify height values; paints material using 2D SDF projection.
    Default = Max,
  };
};
PL_DECLARE_REFLECTABLE_TYPE(PL_TERRAINPLUGIN_DLL, plTerrainModifyMode2D);

using plTerrainBrush2DComponentManager = plComponentManager<class plTerrainBrush2DComponent, plBlockStorageType::Compact>;

/// 2D terrain brush: raises, lowers, or sets the heightfield surface.
///
/// The footprint is a rounded rectangle oriented by the owner object's rotation.
/// HalfSizeX/HalfSizeY control the straight-edge lengths; OuterRadius is the corner rounding radius.
/// Setting HalfSizeX=HalfSizeY=0 produces a circle; OuterRadius=0 gives a plain rectangle.
///
/// If an plSplineComponent exists on the same game object the brush stamps along the spline instead of
/// acting as a single point.
class PL_TERRAINPLUGIN_DLL plTerrainBrush2DComponent : public plTerrainBrushBaseComponent
{
  PL_DECLARE_COMPONENT_TYPE(plTerrainBrush2DComponent, plTerrainBrushBaseComponent, plTerrainBrush2DComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plTerrainBrush2DComponent

public:
  plTerrainBrush2DComponent();
  ~plTerrainBrush2DComponent();

  void SetModifyMode(plEnum<plTerrainModifyMode2D> mode); //< [ property ]
  plEnum<plTerrainModifyMode2D> GetModifyMode() const { return m_eModifyMode; } //< [ property ]

  void SetHalfSizeY(float fSize);                        //< [ property ]
  float GetHalfSizeY() const { return m_fHalfSizeY; }   //< [ property ]

  /// Moves the height the brush shapes the ground to up (positive) or down, in metres. A road's spline brush sets a
  /// little below zero so the ground stays under the road's surface.
  void SetHeightOffset(float fOffset);                        //< [ property ]
  float GetHeightOffset() const { return m_fHeightOffset; }  //< [ property ]

protected:
  virtual void FillBrushSpecificProperties(plTerrainData_PointBrush& brush, float fHalfSizeX) override;

  plEnum<plTerrainModifyMode2D> m_eModifyMode;
  float m_fHalfSizeY = 0.0f;
  float m_fHeightOffset = 0.0f;
};
