#pragma once

#include <TerrainPlugin/Components/TerrainBrushBaseComponent.h>

/// Brush modes available on a 3D (volumetric) terrain brush.
struct PL_TERRAINPLUGIN_DLL plTerrainModifyMode3D
{
  using StorageType = plUInt8;
  enum Enum : plUInt8
  {
    Carve = 3,       ///< Removes solid voxels inside the 3D rounded-box volume.
    Add = 4,         ///< Fills voxels inside the 3D rounded-box volume with solid. On heightfields, paints material using 3D SDF.
    OnlyPaint3D = 6, ///< No geometry change. Paints material on existing solid voxels using 3D SDF. On heightfields, paints material using 3D SDF.
    Default = Carve,
  };
};
PL_DECLARE_REFLECTABLE_TYPE(PL_TERRAINPLUGIN_DLL, plTerrainModifyMode3D);

using plTerrainBrush3DComponentManager = plComponentManager<class plTerrainBrush3DComponent, plBlockStorageType::Compact>;

/// 3D (volumetric) terrain brush: carves or fills solid voxel volumes.
///
/// The footprint is a 3D rounded box oriented by the owner object's rotation.
/// HalfSizeX / HalfSizeYBottom / HalfSizeYTop / HalfSizeZ control the box extents.
/// Setting HalfSizeYTop=0 keeps it equal to HalfSizeYBottom (symmetric).
/// Setting HalfSizeYTop=0 and HalfSizeYBottom to a positive value with a non-zero InnerRadius
/// produces an arch cross-section suitable for natural tunnels with flat floors.
///
/// If an plSplineComponent exists on the same game object the brush stamps along the spline instead of
/// acting as a single point.
class PL_TERRAINPLUGIN_DLL plTerrainBrush3DComponent : public plTerrainBrushBaseComponent
{
  PL_DECLARE_COMPONENT_TYPE(plTerrainBrush3DComponent, plTerrainBrushBaseComponent, plTerrainBrush3DComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plTerrainBrush3DComponent

public:
  plTerrainBrush3DComponent();
  ~plTerrainBrush3DComponent();

  void SetModifyMode(plEnum<plTerrainModifyMode3D> mode); //< [ property ]
  plEnum<plTerrainModifyMode3D> GetModifyMode() const { return m_eModifyMode; } //< [ property ]

  void SetHalfSizeYBottom(float fSize);                  //< [ property ]
  float GetHalfSizeYBottom() const { return m_fHalfSizeYBottom; } //< [ property ]

  /// Upper Y half-size. 0 = same as HalfSizeYBottom (symmetric box).
  void SetHalfSizeYTop(float fSize);                     //< [ property ]
  float GetHalfSizeYTop() const { return m_fHalfSizeYTop; } //< [ property ]

  void SetHalfSizeZ(float fSize);                        //< [ property ]
  float GetHalfSizeZ() const { return m_fHalfSizeZ; }    //< [ property ]

protected:
  virtual void FillBrushSpecificProperties(plTerrainData_PointBrush& brush, float fHalfSizeX) override;

  plEnum<plTerrainModifyMode3D> m_eModifyMode;
  float m_fHalfSizeYBottom = 0.0f;
  float m_fHalfSizeYTop = 0.0f;
  float m_fHalfSizeZ = 0.0f;
};
