#include "TypesAndConstants.hlsli"

cbuffer SceneCB : register(b0)
{
	float4x4 VP;
	float4x4 CascadeVP[MaxCascadesCount];
	float3 SunDirection;
	uint CascadesCount;
	float4 CascadeBias[MaxCascadesCount / 4];
	float4 CascadeSplits[MaxCascadesCount / 4];
	uint ShowCascades;
	uint ShowMeshlets;
};

struct VSOutput
{
	float4 positionCS : SV_POSITION;
	float3 positionWS : POSITIONWS;
	float linearDepth : LDEPTH;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

Texture2DArray ShadowMap : register(t1);

SamplerState PointClampSampler : register(s0);

#include "Common.hlsli"

[earlydepthstencil]
float4 main(VSOutput input) : SV_TARGET
{
	float NdotL = saturate(dot(SunDirection, normalize(input.normal)));
	float shadow = GetShadow(input.linearDepth, input.positionWS);
	float3 ambient = 0.2 * SkyColor;

	float3 result = input.color.rgb * (NdotL * shadow + ambient);
	if (ShowCascades)
	{
		result = GetCascadeColor(input.linearDepth, input.positionWS);
		result *= (NdotL * shadow + ambient);
	}

	return float4(result, 1.0);
}