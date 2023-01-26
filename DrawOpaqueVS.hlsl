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

cbuffer DrawCallConstants : register(b1)
{
	uint StartInstanceLocation;
};

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float3 color : COLOR;
	float2 uv : TEXCOORD0;
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

StructuredBuffer<Instance> Instances : register(t0);

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
	VSOutput result;

	Instance instance = Instances[StartInstanceLocation + instanceID];

	result.positionWS = mul(
		instance.worldTransform,
		float4(input.position, 1.0)).xyz;
	result.positionCS = mul(VP, float4(result.positionWS, 1.0));
	result.linearDepth = result.positionCS.w;
	result.normal = input.normal;
	result.color = float4(input.color, 1.0);
	if (ShowMeshlets)
	{
		result.color = float4(instance.color, 1.0);
	}
	result.uv = input.uv;

	return result;
}
