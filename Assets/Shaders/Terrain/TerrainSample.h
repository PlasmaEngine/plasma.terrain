#pragma once

// Samples the plasma.terrain heightfields of the current world at a world-space position. Include it in a pixel shader
// after MaterialPixelShader.h. It needs nothing bound beyond the frame-scope textures plTerrainSystem binds for every
// view; in a world without heightfields every sample returns Valid == false.
//
// SampleTerrain finds the heightfield under (or over) a position: its height, geometric normal and strongest layers.
// SampleTerrainSurface then shades those layers with the terrain material's textures. Call it outside of branches that
// differ per pixel, since it samples with derivatives.

#include <Shaders/Common/Common.h>
#include <Shaders/Terrain/Rendering/TerrainLayerSampling.h>
#include <Shaders/Terrain/Rendering/TerrainPublishConstants.h>

Texture2D<float4> TerrainTable BIND_GROUP(BG_FRAME);
Texture2DArray<float4> TerrainAtlas BIND_GROUP(BG_FRAME);
Texture2D<float4> TerrainLayerTable BIND_GROUP(BG_FRAME);
Texture2DArray TerrainLayerBase BIND_GROUP(BG_FRAME);
Texture2DArray TerrainLayerNormal BIND_GROUP(BG_FRAME);
Texture2DArray TerrainLayerORM BIND_GROUP(BG_FRAME);

struct plTerrainSample
{
  bool Valid;
  float3 Position;     // the terrain surface point along the heightfield's up axis through the queried position
  float3 Normal;       // world-space geometric normal of the terrain there
  float3 Up;           // world-space up axis of the heightfield
  float Distance;      // how far the queried position is above the terrain along Up; negative below it
  uint3 Layers;        // the three strongest layers, strongest first
  float3 LayerWeights; // their weights, summing to 1
};

struct plTerrainSurface
{
  float3 BaseColor;
  float3 Normal; // world space
  float Roughness;
  float Metallic;
  float Occlusion;
};

plTerrainSample TerrainSampleDefault()
{
  plTerrainSample s;
  s.Valid = false;
  s.Position = float3(0, 0, 0);
  s.Normal = float3(0, 0, 1);
  s.Up = float3(0, 0, 1);
  s.Distance = 1.0e6;
  s.Layers = uint3(0, 0, 0);
  s.LayerWeights = float3(1, 0, 0);
  return s;
}

float4 TerrainTableTexel(uint uiRow, uint uiTexel)
{
  return TerrainTable.Load(int3(uiTexel, uiRow, 0));
}

static const uint TERRAIN_SAMPLE_CANDIDATES = 12;

void TerrainAddLayerCandidate(uint idx, float weight, inout uint ids[TERRAIN_SAMPLE_CANDIDATES], inout float weights[TERRAIN_SAMPLE_CANDIDATES], inout uint count)
{
  if (weight <= 0.0)
    return;

  idx = min(idx, TERRAIN_MAX_LAYERS - 1u);

  [unroll] for (uint i = 0u; i < TERRAIN_SAMPLE_CANDIDATES; ++i)
  {
    if (i < count && ids[i] == idx)
    {
      weights[i] += weight;
      return;
    }
  }

  ids[count] = idx;
  weights[count] = weight;
  ++count;
}

void TerrainAddLayerTexel(float4 texel, float sampleWeight, inout uint ids[TERRAIN_SAMPLE_CANDIDATES], inout float weights[TERRAIN_SAMPLE_CANDIDATES], inout uint count)
{
  const uint packedIdx = (uint)texel.y;
  const uint packedW = (uint)texel.z;

  [unroll] for (uint k = 0u; k < 3u; ++k)
  {
    const uint idx = (packedIdx >> (8u * k)) & 0xFFu;
    const float w = (float)((packedW >> (8u * k)) & 0xFFu) * (1.0 / 255.0);
    TerrainAddLayerCandidate(idx, w * sampleWeight, ids, weights, count);
  }
}

bool TerrainIsHole(float4 texel)
{
  return (((uint)texel.y) & 0xFFu) == 0xFFu;
}

plTerrainSample SampleTerrain(float3 worldPos)
{
  plTerrainSample s = TerrainSampleDefault();

  const uint slotCount = (uint)TerrainTableTexel(0, 0).x;

  [loop] for (uint slot = 0u; slot < slotCount; ++slot)
  {
    const uint row = slot + 1u;

    // Unused slots have an all-zero row and fail the params check below.
    const float4 bounds = TerrainTableTexel(row, TERRAIN_TABLE_BOUNDS);
    if (any(worldPos.xy < bounds.xy) || any(worldPos.xy > bounds.zw))
      continue;

    const float4 params = TerrainTableTexel(row, TERRAIN_TABLE_PARAMS);
    if (params.w < 0.5)
      continue;

    const float4 w0 = TerrainTableTexel(row, TERRAIN_TABLE_WORLD_TO_LOCAL + 0);
    const float4 w1 = TerrainTableTexel(row, TERRAIN_TABLE_WORLD_TO_LOCAL + 1);
    const float4 w2 = TerrainTableTexel(row, TERRAIN_TABLE_WORLD_TO_LOCAL + 2);
    const float4 worldPos4 = float4(worldPos, 1.0);
    const float3 localPos = float3(dot(w0, worldPos4), dot(w1, worldPos4), dot(w2, worldPos4));

    const uint cells = (uint)params.x;
    const float spacing = params.y;
    const uint slice = (uint)params.z;

    const float2 grid = localPos.xy / spacing;
    if (any(grid < 0.0) || any(grid > (float)cells))
      continue;

    const uint2 p0 = min((uint2)floor(grid), uint2(cells - 1u, cells - 1u));
    const float2 f = grid - (float2)p0;

    const float4 t00 = TerrainAtlas.Load(int4(p0.x, p0.y, slice, 0));
    const float4 t10 = TerrainAtlas.Load(int4(p0.x + 1u, p0.y, slice, 0));
    const float4 t01 = TerrainAtlas.Load(int4(p0.x, p0.y + 1u, slice, 0));
    const float4 t11 = TerrainAtlas.Load(int4(p0.x + 1u, p0.y + 1u, slice, 0));

    // The terrain draws no triangle that touches a hole vertex.
    if (TerrainIsHole(t00) || TerrainIsHole(t10) || TerrainIsHole(t01) || TerrainIsHole(t11))
      continue;

    const float height = lerp(lerp(t00.x, t10.x, f.x), lerp(t01.x, t11.x, f.x), f.y);
    const float dhdx = lerp(t10.x - t00.x, t11.x - t01.x, f.y) / spacing;
    const float dhdy = lerp(t01.x - t00.x, t11.x - t10.x, f.x) / spacing;
    const float3 localNormal = normalize(float3(-dhdx, -dhdy, 1.0));

    const float4 l0 = TerrainTableTexel(row, TERRAIN_TABLE_LOCAL_TO_WORLD + 0);
    const float4 l1 = TerrainTableTexel(row, TERRAIN_TABLE_LOCAL_TO_WORLD + 1);
    const float4 l2 = TerrainTableTexel(row, TERRAIN_TABLE_LOCAL_TO_WORLD + 2);
    const float4 surfaceLocal = float4(localPos.xy, height, 1.0);

    s.Position = float3(dot(l0, surfaceLocal), dot(l1, surfaceLocal), dot(l2, surfaceLocal));
    s.Normal = normalize(float3(dot(l0.xyz, localNormal), dot(l1.xyz, localNormal), dot(l2.xyz, localNormal)));
    s.Up = normalize(float3(l0.z, l1.z, l2.z));
    s.Distance = dot(worldPos - s.Position, s.Up);

    uint ids[TERRAIN_SAMPLE_CANDIDATES];
    float weights[TERRAIN_SAMPLE_CANDIDATES];
    [unroll] for (uint i = 0u; i < TERRAIN_SAMPLE_CANDIDATES; ++i)
    {
      ids[i] = 0u;
      weights[i] = 0.0;
    }
    uint count = 0u;

    TerrainAddLayerTexel(t00, (1.0 - f.x) * (1.0 - f.y), ids, weights, count);
    TerrainAddLayerTexel(t10, f.x * (1.0 - f.y), ids, weights, count);
    TerrainAddLayerTexel(t01, (1.0 - f.x) * f.y, ids, weights, count);
    TerrainAddLayerTexel(t11, f.x * f.y, ids, weights, count);

    uint3 top = uint3(0, 0, 0);
    float3 topW = float3(0, 0, 0);
    [unroll] for (uint c = 0u; c < TERRAIN_SAMPLE_CANDIDATES; ++c)
    {
      if (c >= count)
        continue;

      const float w = weights[c];
      if (w > topW.x)
      {
        topW = float3(w, topW.x, topW.y);
        top = uint3(ids[c], top.x, top.y);
      }
      else if (w > topW.y)
      {
        topW = float3(topW.x, w, topW.y);
        top = uint3(top.x, ids[c], top.y);
      }
      else if (w > topW.z)
      {
        topW.z = w;
        top.z = ids[c];
      }
    }

    const float total = topW.x + topW.y + topW.z;
    s.Layers = top;
    s.LayerWeights = total > 0.001 ? topW / total : float3(1, 0, 0);
    s.Valid = true;
    return s;
  }

  return s;
}

/// Shades the sampled layers with the terrain material. projectionNormal picks the triplanar projection: the terrain
/// normal matches the terrain's own look exactly, the mesh normal keeps the textures straight on steep mesh faces.
plTerrainSurface SampleTerrainSurface(plTerrainSample s, float3 worldPos, float3 projectionNormal)
{
  plTerrainSurface surface;

  const float4 header = TerrainTableTexel(0, 0);
  const uint flags = (uint)header.z;

  [branch]
  if ((flags & TERRAIN_LAYERS_PRESENT) == 0u)
  {
    surface.BaseColor = float3(0.5, 0.5, 0.5);
    surface.Normal = s.Normal;
    surface.Roughness = 0.8;
    surface.Metallic = 0.0;
    surface.Occlusion = 1.0;
    return surface;
  }

  const bool useStochastic = (flags & TERRAIN_LAYERS_STOCHASTIC) != 0u;
  const float3 tpWeights = TriplanarWeights(projectionNormal);

  float3 baseColor = float3(0, 0, 0);
  float3 normal = float3(0, 0, 0);
  float3 orm = float3(0, 0, 0);

  [unroll] for (uint k = 0u; k < 3u; ++k)
  {
    const float4 layer = TerrainLayerTable.Load(int3(s.Layers[k], 0, 0));
    const uint2 texIndices = (uint2)layer.xy;
    const float scale = layer.z;
    const float w = s.LayerWeights[k];

    baseColor += TriplanarSampleColorArray(TerrainLayerBase, LinearSampler, worldPos, projectionNormal, tpWeights, scale, texIndices, useStochastic).rgb * w;
    normal += TriplanarSampleNormalArray(TerrainLayerNormal, LinearSampler, worldPos, projectionNormal, tpWeights, scale, texIndices, useStochastic) * w;
    orm += TriplanarSampleColorArray(TerrainLayerORM, LinearSampler, worldPos, projectionNormal, tpWeights, scale, texIndices, useStochastic).rgb * w;
  }

  surface.BaseColor = baseColor;
  surface.Normal = normalize(normal);
  surface.Roughness = saturate(orm.g + header.w);
  surface.Metallic = orm.b;
  // The terrain material does not apply its occlusion channel either.
  surface.Occlusion = 1.0;
  return surface;
}
