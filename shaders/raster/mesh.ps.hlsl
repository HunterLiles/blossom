#include "raster/mesh.hlsli"

// Matches the descriptor set layout in raster::create_descriptors.
[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D textures[];

// Points toward the light: above, right, and on the camera's side (-Z).
static const float3 LIGHT_DIRECTION = normalize(float3(0.4, 1.0, -0.6));
static const float AMBIENT = 0.15;

float4 main(VertexOutput input) : SV_Target0 {
  // The index comes from push constants, so it is uniform across the draw
  // and needs no NonUniformResourceIndex.
  float4 albedo = textures[push.texture_index].Sample(texture_sampler, input.uv);

  float3 normal = normalize(input.normal);
  float diffuse = saturate(dot(normal, LIGHT_DIRECTION));
  float3 color = albedo.rgb * input.color * (AMBIENT + (1.0 - AMBIENT) * diffuse);
  return float4(color, albedo.a);
}
