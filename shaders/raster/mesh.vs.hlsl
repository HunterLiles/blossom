#include "raster/mesh.hlsli"

VertexOutput main(VertexInput input) {
  VertexOutput output;
  output.position = mul(push.mvp, float4(input.position, 1.0));
  // Assumes uniform scale; non-uniform scale needs the inverse transpose.
  output.normal = mul((float3x3)push.model, input.normal);
  output.color = input.color;
  output.uv = input.uv;
  return output;
}
