#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Foundation/Types/TagSet.h>
#include <RendererCore/Components/RenderComponent.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <TerrainPlugin/TerrainPluginDLL.h>
#include <TerrainPlugin/TerrainSystem.h>

struct plMsgExtractRenderData;
struct plMsgTransformChanged;

using plMaterialResourceHandle = plTypedResourceHandle<class plMaterialResource>;
using plSurfaceResourceHandle = plTypedResourceHandle<class plSurfaceResource>;

class PL_TERRAINPLUGIN_DLL plTerrainVoxelComponentManager : public plComponentManager<class plTerrainVoxelComponent, plBlockStorageType::Compact>
{
  using SUPER = plComponentManager<plTerrainVoxelComponent, plBlockStorageType::Compact>;

public:
  plTerrainVoxelComponentManager(plWorld* pWorld)
    : SUPER(pWorld)
  {
  }

  virtual void Initialize() override;

private:
  void Update(const plWorldModule::UpdateContext& context);
};

/// Renders a voxel volume built from terrain brushes.
///
/// Brushes with ModifyMode != Ignore fill voxels within their sphere (OuterRadius) to solid.
/// Voxels are baked on the GPU and triangulated on the GPU. The resulting mesh is rendered
/// directly from the GPU-side buffers (no CPU download needed for rendering).
/// A full vertex/index readback can be requested via SetNeedsCollisionReadback for physics use.
/// Use any compatible material for rendering.
class PL_TERRAINPLUGIN_DLL plTerrainVoxelComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plTerrainVoxelComponent, plRenderComponent, plTerrainVoxelComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plRenderComponent

protected:
  virtual plResult GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg) override;
  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;
  void OnMsgTransformChanged(plMsgTransformChanged& msg);

  //////////////////////////////////////////////////////////////////////////
  // plTerrainVoxelComponent

public:
  plTerrainVoxelComponent();
  ~plTerrainVoxelComponent();

  void SetResolution(plEnum<plTerrainResolution> eResolution); // [ property ]
  plEnum<plTerrainResolution> GetResolution() const { return m_eResolution; }

  void SetSize(float f);                                       // [ property ]
  float GetSize() const { return m_fSize; }

  void SetMaterial(const plMaterialResourceHandle& hMaterial); // [ property ]
  const plMaterialResourceHandle& GetMaterial() const { return m_hMaterial; }

  void SetBaseMaterial(plUInt8 i);                             // [ property ]
  plUInt8 GetBaseMaterial() const { return m_uiBaseMaterial; }

  void SetFillHeight(float f);                                 // [ property ]
  float GetFillHeight() const { return m_fFillHeight; }

  void SetEnableCollider(bool bEnable);                        // [ property ]
  bool GetEnableCollider() const { return m_bEnableCollider; } // [ property ]

  const plTagSet& GetTags() const { return m_Tags; }           // [ property ]
  void Reflection_SetTag(const char* szTagName);               // [ property ]
  void Reflection_RemoveTag(const char* szTagName);            // [ property ]

  /// Per-material-index physics surface handles. Entry i is the surface used when the vertex material index == i and MaterialStrength > 0.5.
  plUInt32 Surfaces_GetCount() const;
  plString Surfaces_GetValue(plUInt32 uiIndex) const;
  void Surfaces_SetValue(plUInt32 uiIndex, plString sValue);
  void Surfaces_Insert(plUInt32 uiIndex, plString sValue);
  void Surfaces_Remove(plUInt32 uiIndex);

  /// Called by the component manager each frame; updates GPU primitive count when counts readback completes.
  void UpdateVoxelMesh();

private:
  plUInt32 m_uiVoxelIndex = plInvalidIndex;
  plEnum<plTerrainResolution> m_eResolution;
  bool m_bEnableCollider = true;
  plUInt8 m_uiBaseMaterial = 0;
  float m_fSize = 32.0f;
  float m_fFillHeight = -100.0f;
  plMaterialResourceHandle m_hMaterial;
  mutable plInstanceDataOffset m_InstanceDataOffset;
  plGameObjectHandle m_hColliderObject;
  plTagSet m_Tags; ///< Identity tags matched against brush include-tag filters.

  /// Physics surface handles indexed by voxel material index.
  plDynamicArray<plSurfaceResourceHandle> m_Surfaces;
};
