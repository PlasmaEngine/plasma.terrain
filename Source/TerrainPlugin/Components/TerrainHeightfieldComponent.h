#pragma once

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/ResourceManager/ResourceHandle.h>
#include <Foundation/Types/TagSet.h>
#include <GameEngine/Utils/ImageDataResource.h>
#include <RendererCore/Components/RenderComponent.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <TerrainPlugin/TerrainPluginDLL.h>
#include <TerrainPlugin/TerrainSystem.h>

struct plMsgExtractRenderData;
struct plMsgTransformChanged;
struct plResourceEvent;

using plMaterialResourceHandle = plTypedResourceHandle<class plMaterialResource>;

class PL_TERRAINPLUGIN_DLL plTerrainPatchComponentManager : public plComponentManager<class plTerrainHeightfieldComponent, plBlockStorageType::Compact>
{
  using SUPER = plComponentManager<plTerrainHeightfieldComponent, plBlockStorageType::Compact>;

public:
  plTerrainPatchComponentManager(plWorld* pWorld);
  ~plTerrainPatchComponentManager();

  virtual void Initialize() override;

private:
  void Update(const plWorldModule::UpdateContext& context);
  void ResourceEventHandler(const plResourceEvent& e);
};

/// Renders a single terrain patch as a procedural grid.
///
/// The vertex shader generates grid geometry from SV_VertexID; no vertex buffer is needed.
/// Heights are stored in a GPU structured buffer baked by a compute shader via plTerrainSystem.
/// Call InvalidateHeights() after modifying height data to trigger a CS re-bake.
class PL_TERRAINPLUGIN_DLL plTerrainHeightfieldComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plTerrainHeightfieldComponent, plRenderComponent, plTerrainPatchComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plRenderComponent

protected:
  virtual plResult GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg) override;
  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;

  void OnMsgTransformChanged(plMsgTransformChanged& msg);

  //////////////////////////////////////////////////////////////////////////
  // plTerrainHeightfieldComponent

public:
  plTerrainHeightfieldComponent();
  ~plTerrainHeightfieldComponent();

  void SetResolution(plEnum<plTerrainResolution> eResolution);
  plEnum<plTerrainResolution> GetResolution() const { return m_eResolution; }

  void SetSize(float fSize);
  float GetSize() const { return m_fSize; }

  void SetMaterial(const plMaterialResourceHandle& hMaterial);                       // [ property ]
  const plMaterialResourceHandle& GetMaterial() const { return m_hMaterial; }        // [ property ]

  void SetHeightImage(const plImageDataResourceHandle& hImage);                      // [ property ]
  const plImageDataResourceHandle& GetHeightImage() const { return m_hHeightImage; } // [ property ]

  /// UV offset into the height image; selects the top-left corner of the sampled rectangle.
  plVec2 GetHeightImageOffset() const { return m_vImageOffset; } // [ property ]
  void SetHeightImageOffset(plVec2 vOffset);                     // [ property ]

  /// UV size of the rectangle sampled from the height image. Values < 1 select a sub-region.
  plVec2 GetHeightImageSize() const { return m_vImageSize; } // [ property ]
  void SetHeightImageSize(plVec2 vSize);                     // [ property ]

  /// Multiplier applied to the [0, 1] greyscale sample to produce a world-space height.
  float GetHeightScale() const { return m_fHeightScale; }        // [ property ]
  void SetHeightScale(float fScale);                             // [ property ]

  /// Painted layers under the brushes, sampled over the same rectangle as the height image. Each of the R, G and B
  /// channels is one layer of the vertex as 16 bits: the layer index in the high byte, its weight in the low byte.
  /// Layer index 255 in R makes the vertex a hole. Texels are read nearest, never interpolated.
  void SetLayerImage(const plImageDataResourceHandle& hImage);                     // [ property ]
  const plImageDataResourceHandle& GetLayerImage() const { return m_hLayerImage; } // [ property ]

  void SetEnableCollider(bool bEnable);                          // [ property ]
  bool GetEnableCollider() const { return m_bEnableCollider; }   // [ property ]

  plUInt8 GetMaterialIndex() const { return m_uiMaterialIndex; } // [ property ]
  void SetMaterialIndex(plUInt8 uiIndex);                        // [ property ]

  const plTagSet& GetTags() const { return m_Tags; }             // [ property ]
  void Reflection_SetTag(const char* szTagName);                 // [ property ]
  void Reflection_RemoveTag(const char* szTagName);              // [ property ]

  /// Per-material-index physics surface handles. Entry i is the surface used when dominant material index == i.
  plUInt32 Surfaces_GetCount() const;
  plString Surfaces_GetValue(plUInt32 uiIndex) const;
  void Surfaces_SetValue(plUInt32 uiIndex, plString sValue);
  void Surfaces_Insert(plUInt32 uiIndex, plString sValue);
  void Surfaces_Remove(plUInt32 uiIndex);

  /// Called by the component manager each frame to check for ready readback data and update the physics collider.
  void UpdateCollider();

private:
  void DestroyColliderObject();

  plUInt32 m_uiHeightfieldIndex = plInvalidIndex;
  mutable plInstanceDataOffset m_InstanceDataOffset;
  plGameObjectHandle m_hColliderObject;

  plUInt8 m_uiMaterialIndex = 0;
  plEnum<plTerrainResolution> m_eResolution;
  float m_fSize = 32.0f;
  plMaterialResourceHandle m_hMaterial;
  bool m_bEnableCollider = true;
  plTagSet m_Tags; ///< Identity tags matched against brush include-tag filters.
  /// Physics surface handles indexed by terrain material index. Used to fill HeightfieldColliderData::m_Surfaces.
  plDynamicArray<plSurfaceResourceHandle> m_Surfaces;

  plImageDataResourceHandle m_hHeightImage;
  plVec2 m_vImageOffset = plVec2::MakeZero();
  plVec2 m_vImageSize = plVec2(1.0f);
  float m_fHeightScale = 32.0f;
  plImageDataResourceHandle m_hLayerImage;
  bool m_bHeightImageDirty = false; ///< Set when the image resource reloads; cleared after marking the heightfield dirty.
};
