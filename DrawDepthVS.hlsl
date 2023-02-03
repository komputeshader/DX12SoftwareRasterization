#include "Common.hlsli"

cbuffer DepthSceneCB : register(b0)
{
	float4x4 VP;
};

cbuffer DrawCallConstants : register(b1)
{
	uint StartInstanceLocation;
};

struct VSInput
{
	float3 position : POSITION;
};

struct VSOutput
{
	float4 positionCS : SV_POSITION;
};

StructuredBuffer<Instance> Instances : register(t0);

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
	VSOutput result;

	float4 positionWS = mul(
		Instances[StartInstanceLocation + instanceID].worldTransform,
		float4(input.position, 1.0));
	result.positionCS = mul(VP, positionWS);

	return result;
}