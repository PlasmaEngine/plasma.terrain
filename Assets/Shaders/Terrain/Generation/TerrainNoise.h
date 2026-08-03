#pragma once

/// 2D hash producing a pseudo-random value in [0, 1) from an integer-lattice point.
/// Cheap sin-based hash. Quality is sufficient for terrain brush noise; replace if banding shows up.
float TerrainHash2D(float2 p)
{
  return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

/// 2D value noise on a unit lattice, output in [0, 1].
float ValueNoise2D(float2 p)
{
  float2 i = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0 - 2.0 * f);

  float a = TerrainHash2D(i + float2(0.0, 0.0));
  float b = TerrainHash2D(i + float2(1.0, 0.0));
  float c = TerrainHash2D(i + float2(0.0, 1.0));
  float d = TerrainHash2D(i + float2(1.0, 1.0));

  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

/// 2-octave fbm, output remapped to [-1, 1].
float FBM2D(float2 p)
{
  float n = ValueNoise2D(p) * 0.6667 + ValueNoise2D(p * 2.03) * 0.3333;
  return n * 2.0 - 1.0;
}

/// 3D hash producing a pseudo-random value in [0, 1).
float TerrainHash3D(float3 p)
{
  return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453);
}

/// 3D value noise on a unit lattice, output in [0, 1].
float ValueNoise3D(float3 p)
{
  float3 i = floor(p);
  float3 f = frac(p);
  float3 u = f * f * (3.0 - 2.0 * f);

  float a = TerrainHash3D(i + float3(0.0, 0.0, 0.0));
  float b = TerrainHash3D(i + float3(1.0, 0.0, 0.0));
  float c = TerrainHash3D(i + float3(0.0, 1.0, 0.0));
  float d = TerrainHash3D(i + float3(1.0, 1.0, 0.0));
  float e = TerrainHash3D(i + float3(0.0, 0.0, 1.0));
  float g = TerrainHash3D(i + float3(1.0, 0.0, 1.0));
  float h = TerrainHash3D(i + float3(0.0, 1.0, 1.0));
  float k = TerrainHash3D(i + float3(1.0, 1.0, 1.0));

  float ab = lerp(a, b, u.x);
  float cd = lerp(c, d, u.x);
  float eg = lerp(e, g, u.x);
  float hk = lerp(h, k, u.x);
  return lerp(lerp(ab, cd, u.y), lerp(eg, hk, u.y), u.z);
}

/// 2-octave 3D fbm, output remapped to [-1, 1].
float FBM3D(float3 p)
{
  float n = ValueNoise3D(p) * 0.6667 + ValueNoise3D(p * 2.03) * 0.3333;
  return n * 2.0 - 1.0;
}
