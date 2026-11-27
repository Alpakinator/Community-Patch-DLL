// fow_models_resample.hlsl - the resample pass of composite/fow_models.cpp (ticket 0394; moved out of the C++ source by
// ticket 0407). Written 2026-11-16 for this repository (GPL-3.0-or-later); our own source, nothing of the EXE.
// Compiled at build time by shell/build.cmd with the Windows SDK's fxc (vs_4_0 / ps_4_0, /O1 = D3DCompile's default level,
// as 0394 measured it) into %C5_SHELL_BUILD%\gen\FowRsVS.h and FowRsPS.h; the bytecode is in shell_composite.dll, so
// d3dcompiler_47.dll is not loaded at run time and nothing is compiled on the EXE's render thread
// (research/05-graphics-composition/fow-models-compile.md).
//
// A full-screen triangle into mip 0 of the local mask, sampling the server's mask (bilinear) through the two windows' uv
// map xf: u' = xf.xy * uv + xf.zw. Outside [0, 1] the strip's fill: `fill` (edge.z == 0) or the nearest known texel
// edge.xy inside the rim (edge.z == 1, fill 'edge').
cbuffer C : register(b0) { float4 xf; float4 fill; float4 edge; };
Texture2D t : register(t0);
SamplerState s : register(s0);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O VS(uint id : SV_VertexID) { O o; float2 uv = float2((id << 1) & 2, id & 2); o.uv = uv;
  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); return o; }
float4 PS(O i) : SV_Target { float2 u = i.uv * xf.xy + xf.zw;
  if (u.x < 0.0 || u.y < 0.0 || u.x > 1.0 || u.y > 1.0) { if (edge.z == 0.0) return fill;
    u = clamp(u, edge.xy, 1.0 - edge.xy); }
  return t.SampleLevel(s, u, 0.0); }
