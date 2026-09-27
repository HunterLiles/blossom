#pragma once

// Matches raster::PushConstants in src/render/raster/renderer.hpp.
struct PushConstants {
  float4x4 mvp;
  float4x4 model;
  uint texture_index;
};

[[vk::push_constant]] PushConstants push;

// Matches resources::Vertex in src/resources/mesh.hpp.
struct VertexInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(1)]] float3 normal : NORMAL;
  [[vk::location(2)]] float3 color : COLOR;
  [[vk::location(3)]] float2 uv : TEXCOORD0;
};

struct VertexOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float3 normal : NORMAL;
  [[vk::location(1)]] float3 color : COLOR;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
};
