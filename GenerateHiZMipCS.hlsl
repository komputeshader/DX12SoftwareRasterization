#include "Common.hlsli"

cbuffer Constants : register(b0)
{
	uint2 OutputResolution;
	float2 InvOutputResolution;
	float2 NPOTCorrection;
};

Texture2D Input : register(t0);

RWTexture2D<uint> Output : register(u0);

// TODO: account for non-reversed Z depth buffers
SamplerState DepthSampler : register(s0);

[numthreads(HiZThreadsX, HiZThreadsY, HiZThreadsZ)]
void main(
	uint3 groupID : SV_GroupID,
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (any(dispatchThreadID.xy >= OutputResolution.xy))
	{
		return;
	}

	float2 uv =
		(float2(dispatchThreadID.xy) + float2(0.5, 0.5)) *
		InvOutputResolution * NPOTCorrection;

	float result = Input.SampleLevel(
		DepthSampler,
		uv,
		0.0).r;

	float borderX = (dispatchThreadID.x == OutputResolution.x - 1)
		? 1.0
		: 0.0;
	float borderY = (dispatchThreadID.y == OutputResolution.y - 1)
		? 1.0
		: 0.0;
	if (borderX || borderY)
	{
		result = min(
			result,
			Input.SampleLevel(
				DepthSampler,
				float2(
					lerp(uv.x, 1.0, borderX),
					lerp(uv.y, 1.0, borderY)),
				0.0).r);
	}

	Output[dispatchThreadID.xy] = asuint(result);
}