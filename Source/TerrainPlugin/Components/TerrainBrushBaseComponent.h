#pragma once

#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Types/TagSet.h>
#include <TerrainPlugin/TerrainPluginDLL.h>
#include <TerrainPlugin/TerrainSystem.h>

struct plMsgTransformChanged;
struct plMsgSplineChanged;

/// Abstract base for all terrain brush components.
///
/// Handles creation and cleanup of TerrainSystem point brushes, including automatic spline-path support:
/// if an plSplineComponent exists on the same game object the brush stamps along the spline instead of
/// acting as a single point. Properties common to 2D and 3D brushes are declared here and inherited by
/// both plTerrainBrush2DComponent and plTerrainBrush3DComponent.
class PL_TERRAINPLUGIN_DLL plTerrainBrushBaseComponent : public plComponent
{
  PL_DECLARE_ABSTRACT_COMPONENT_TYPE(plTerrainBrushBaseComponent, plComponent);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plTerrainBrushBaseComponent

public:
  plTerrainBrushBaseComponent();
  ~plTerrainBrushBaseComponent();

  void SetHalfSizeX(float fSize);                //< [ property ]
  float GetHalfSizeX() const { return m_fHalfSizeX; } //< [ property ]

  void SetInnerRadius(float fRadius);                  //< [ property ]
  float GetInnerRadius() const { return m_fInnerRadius; } //< [ property ]

  void SetOuterRadius(float fRadius);                  //< [ property ]
  float GetOuterRadius() const { return m_fOuterRadius; } //< [ property ]

  void SetFalloff(float fFalloff);                     //< [ property ]
  float GetFalloff() const { return m_fFalloff; }      //< [ property ]

  void SetMaterialIndex(plUInt8 uiIndex);              //< [ property ]
  plUInt8 GetMaterialIndex() const { return m_uiMaterialIndex; } //< [ property ]

  /// Blend strength for material painting in [0, 1]. 0 = disabled.
  void SetMaterialStrength(float fStrength);           //< [ property ]
  float GetMaterialStrength() const { return m_fMaterialStrength; } //< [ property ]

  void SetAffectsHeightfields(bool b);                 //< [ property ]
  bool GetAffectsHeightfields() const { return m_bAffectHeightfields; } //< [ property ]

  void SetAffectsVoxels(bool b);                       //< [ property ]
  bool GetAffectsVoxels() const { return m_bAffectVoxels; } //< [ property ]

  void SetNoise(float fNoise);                         //< [ property ]
  float GetNoise() const { return m_fNoise; }          //< [ property ]

  void SetNoiseScale(float fNoise);                    //< [ property ]
  float GetNoiseScale() const { return m_fNoiseScale; } //< [ property ]

  /// Brush application order. Higher = applied later = wins. Equal priorities use mode-based ordering.
  void SetPriority(plInt8 iPriority);                  //< [ property ]
  plInt8 GetPriority() const { return m_iPriority; }   //< [ property ]

  const plTagSet& GetIncludeTags() const { return m_IncludeTags; } //< [ property ]
  void Reflection_SetIncludeTag(const char* szTagName);            //< [ property ]
  void Reflection_RemoveIncludeTag(const char* szTagName);         //< [ property ]

protected:
  void OnMsgTransformChanged(plMsgTransformChanged& msg);
  void OnMsgSplineChanged(plMsgSplineChanged& msg);

  void RefreshBrushes();
  void ClearBrushes();

  /// Fills transform, HalfSizeX, and all common properties, then calls FillBrushSpecificProperties.
  void FillBrush(plTerrainData_PointBrush& brush, const plTransform& transform, float fHalfSizeX);

  /// Subclass fills: ModifyMode, m_vHalfExtents.y, m_fHalfExtentYTop, m_fHalfExtentZ.
  virtual void FillBrushSpecificProperties(plTerrainData_PointBrush& brush, float fHalfSizeX) = 0;

  plDynamicArray<plUInt32> m_BrushIndices;

  float m_fHalfSizeX = 0.0f;
  float m_fInnerRadius = 0.0f;
  float m_fOuterRadius = 5.0f;
  float m_fFalloff = 1.0f;
  float m_fNoise = 0.0f;
  float m_fNoiseScale = 1.0f;
  float m_fMaterialStrength = 0.0f;
  plUInt8 m_uiMaterialIndex = 0;
  plInt8 m_iPriority = 0;
  bool m_bAffectHeightfields = true;
  bool m_bAffectVoxels = true;
  /// If non-empty, brush only affects terrain objects that have at least one matching tag.
  plTagSet m_IncludeTags;
};
