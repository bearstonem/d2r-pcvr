#pragma once
namespace framevr::transparentfog {
// Own shader, linked to the verified native transparency resolve's inputs.
// These are already depth-tested effects, not a brightness test of the scene.
inline constexpr char Shader[]=R"hlsl(
float EffectCoverage(float4 accumulated,float4 coefficients,float3 additive) {
    float3 light=max(additive,0);
    if(accumulated.a>0)
        light+=max(accumulated.rgb/accumulated.a*coefficients.rgb,0)*(1-saturate(coefficients.a));
    // Smooth only the almost-empty fringe, avoiding a hard binary halo.
    return smoothstep(0.00001,0.001,max(light.r,max(light.g,light.b)));
}
#ifndef COVERAGE_TEST
Texture2D<float4> accumulated : register(t11);
Texture2D<float4> coefficients : register(t12);
Texture2D<float4> additive : register(t13);
SamplerState nativeSampler : register(s0);
float PS(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    return EffectCoverage(accumulated.Sample(nativeSampler,uv),
        coefficients.Sample(nativeSampler,uv),additive.Sample(nativeSampler,uv).rgb);
}
#endif
)hlsl";
}
