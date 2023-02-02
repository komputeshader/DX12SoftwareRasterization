#ifndef COMMON_HLSL
#define COMMON_HLSL

#include "TypesAndConstants.hlsli"

uint DispatchSize(uint groupSize, uint elementsCount)
{
	return (elementsCount + groupSize - 1) / groupSize;
}

float3 UnpackPosition(in uint2 packed)
{
	return f16tof32(
		uint3(
			packed.x >> 16,
			packed.x,
			packed.y >> 16));
}

float3 UnpackPosition(in VertexPosition packed)
{
	return UnpackPosition(packed.packedPosition);
}

float3 UnpackNormal(in uint packed)
{
	// 1 / (2 ^ N - 1), N = 10, see Scene.cpp normal packing
	float denom = 1.0 / 1023.0;

	return float3(
		uint((packed >> 20) & 0x3FF),
		uint((packed >> 10) & 0x3FF),
		uint(packed & 0x3FF)) * denom * 2.0 - 1.0.xxx;
}

float3 UnpackNormal(in VertexNormal packed)
{
	return UnpackNormal(packed.packedNormal);
}

float2 UnpackTexcoords(in uint packed)
{
	return f16tof32(
		uint2(
			packed >> 16,
			packed));
}

float2 UnpackTexcoords(in VertexUV packed)
{
	return UnpackTexcoords(packed.packedUV);
}

float4 UnpackColor(in uint2 packed)
{
	return f16tof32(
		uint4(
			packed.x >> 16,
			packed.x,
			packed.y >> 16,
			packed.y));
}

float4 UnpackColor(in VertexColor packed)
{
	return UnpackColor(packed.packedColor);
}

AABB TransformAABB(
	in AABB box,
	in float4x4 M)
{
	float bc[3] = { M[0][3], M[1][3], M[2][3] };
	float be[3] = { 0.0, 0.0, 0.0 };

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		[unroll]
		for (uint j = 0; j < 3; j++)
		{
			bc[i] += M[i][j] * box.center[j];
			be[i] += abs(M[i][j]) * box.extents[j];
		}
	}

	AABB result;
	result.center = float3(bc[0], bc[1], bc[2]);
	result.extents = float3(be[0], be[1], be[2]);

	return result;
}

#ifdef OPAQUE
float GetShadow(in float viewDepth, in float3 positionWS)
{
	float cascadeBias[MaxCascadesCount] =
	{
		CascadeBias[0].x,
		CascadeBias[0].y,
		CascadeBias[0].z,
		CascadeBias[0].w,
		CascadeBias[1].x,
		CascadeBias[1].y,
		CascadeBias[1].z,
		CascadeBias[1].w
	};
	float cascadeSplits[MaxCascadesCount] =
	{
		CascadeSplits[0].x,
		CascadeSplits[0].y,
		CascadeSplits[0].z,
		CascadeSplits[0].w,
		CascadeSplits[1].x,
		CascadeSplits[1].y,
		CascadeSplits[1].z,
		CascadeSplits[1].w
	};

	int cascadeIdx = CascadesCount - 1;
	for (int i = CascadesCount - 1; i >= 0; i--)
	{
		if (viewDepth <= cascadeSplits[i])
		{
			cascadeIdx = i;
		}
	}

	float4 positionLCS = mul(CascadeVP[cascadeIdx], float4(positionWS, 1.0));
	positionLCS.xy *= float2(0.5, -0.5);
	positionLCS.xy += float2(0.5, 0.5);
	float2 smuv = positionLCS.xy;

	float depthSM = ShadowMap.SampleLevel(
		PointClampSampler,
		float3(smuv, cascadeIdx),
		0.0).x;
	// TODO: account for non-reversed Z
	float shadow = (positionLCS.z > (depthSM - cascadeBias[cascadeIdx]))
		? 1.0
		: 0.0;

	return shadow;
}

// for debug and visualisation purposes
float3 GetCascadeColor(in float viewDepth, in float3 positionWS)
{
	float cascadeSplits[MaxCascadesCount] =
	{
		CascadeSplits[0].x,
		CascadeSplits[0].y,
		CascadeSplits[0].z,
		CascadeSplits[0].w,
		CascadeSplits[1].x,
		CascadeSplits[1].y,
		CascadeSplits[1].z,
		CascadeSplits[1].w
	};
	float3 cascadeColors[MaxCascadesCount] =
	{
		float3(1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, 0.0, 1.0),
		float3(1.0, 1.0, 0.0),
		float3(0.8, 0.0, 0.0),
		float3(0.0, 0.8, 0.0),
		float3(0.0, 0.0, 0.8),
		float3(0.8, 0.8, 0.0),
	};

	int cascadeIdx = CascadesCount - 1;
	for (int i = CascadesCount - 1; i >= 0; i--)
	{
		if (viewDepth <= cascadeSplits[i])
		{
			cascadeIdx = i;
		}
	}

	return cascadeColors[cascadeIdx];
}
#endif // OPAQUE

#endif // COMMON_HLSL