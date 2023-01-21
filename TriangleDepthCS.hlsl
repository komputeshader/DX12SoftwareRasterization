// Implicit assumptions:
// - we are dealing with triangles
// - indices represent a triangle list

#include "TypesAndConstants.hlsli"

cbuffer DepthSceneCB : register(b0)
{
	float4x4 VP;
	float2 OutputRes;
	float2 InvOutputRes;
	float BigTriangleThreshold;
	float BigTriangleTileSize;
};

cbuffer ConstantBuffer : register(b1)
{
	uint IndexCountPerInstance;
	uint StartIndexLocation;
	uint BaseVertexLocation;
	uint StartInstanceLocation;
}

StructuredBuffer<VertexPosition> Positions : register(t0);

StructuredBuffer<uint> Indices : register(t8);
StructuredBuffer<Instance> Instances : register(t9);
Texture2D HiZ : register(t10);

SamplerState DepthSampler : register(s0);

RWTexture2D<uint> Depth : register(u0);
AppendStructuredBuffer<BigTriangle> BigTriangles : register(u1);
RWStructuredBuffer<uint> Statistics : register(u2);

#include "Common.hlsli"
#include "Rasterization.hlsli"

[numthreads(SWRTriangleThreadsX, SWRTriangleThreadsY, SWRTriangleThreadsZ)]
void main(
	uint3 groupID : SV_GroupID,
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	[branch]
	if (dispatchThreadID.x * 3 >= IndexCountPerInstance)
	{
		return;
	}

	// one more triangle attempted to be rendered
	InterlockedAdd(Statistics[0], 1);

	uint v0Idx, v1Idx, v2Idx;
	GetTriangleIndices(
		StartIndexLocation + dispatchThreadID.x * 3,
		v0Idx, v1Idx, v2Idx);

	float3 v0P, v1P, v2P;
	GetTriangleVertexPositions(
		v0Idx, v1Idx, v2Idx,
		BaseVertexLocation,
		v0P, v1P, v2P);

	float4 p0CS, p1CS, p2CS;
	uint instanceID = groupID.y;
	uint instanceIndex = StartInstanceLocation + instanceID;
	GetCSPositions(
		instanceIndex,
		v0P, v1P, v2P,
		p0CS, p1CS, p2CS);

	// crude "clipping" of polygons behind the camera
	// w in CS is a view space z
	// TODO: implement proper near plane clipping
	[branch]
	if (p0CS.w <= 0.0 || p1CS.w <= 0.0 || p2CS.w <= 0.0)
	{
		return;
	}

	// 1 / z for each vertex (z in VS)
	float invW0 = 1.0 / p0CS.w;
	float invW1 = 1.0 / p1CS.w;
	float invW2 = 1.0 / p2CS.w;

	float2 p0SS, p1SS, p2SS;
	GetSSPositions(
		p0CS.xy, p1CS.xy, p2CS.xy,
		invW0, invW1, invW2,
		p0SS, p1SS, p2SS);

	float area = Area(p0SS.xy, p1SS.xy, p2SS.xy);

	// backface if negative
	[branch]
	if (area <= 0.0)
	{
		return;
	}

	float z0NDC = p0CS.z * invW0;
	float z1NDC = p1CS.z * invW1;
	float z2NDC = p2CS.z * invW2;

	float3 minP = min(
		min(float3(p0SS.xy, z0NDC), float3(p1SS.xy, z1NDC)),
		float3(p2SS.xy, z2NDC));
	float3 maxP = max(
		max(float3(p0SS.xy, z0NDC), float3(p1SS.xy, z1NDC)),
		float3(p2SS.xy, z2NDC));

	// frustum culling
	[branch]
	if (minP.x >= OutputRes.x || maxP.x < 0.0
		|| maxP.y < 0.0 || minP.y >= OutputRes.y)
	{
		return;
	}

	ClampToScreenBounds(minP.xy, maxP.xy);

	// small triangles between pixel centers
	// https://frostbite-wp-prd.s3.amazonaws.com/wp-content/uploads/2016/03/29204330/GDC_2016_Compute.pdf
	[branch]
	if (any(round(minP.xy) == round(maxP.xy)))
	{
		return;
	}

	minP.xy = SnapMinBoundToPixelCenter(minP.xy);

	float2 dimensions = maxP.xy - minP.xy;

	// Hi-Z
	//float mipLevel =
	//	ceil(log2(0.5 * max(dimensions.x, dimensions.y)));
	//float tileDepth = HiZ.SampleLevel(
	//	DepthSampler,
	//	(minP.xy + maxP.xy) * 0.5 * InvOutputRes,
	//	mipLevel).r;
	//[branch]
	//if (tileDepth > maxP.z)
	//{
	//	return;
	//}

	// one more triangle was rendered
	// not precise, though, since it still could miss any pixel centers
	InterlockedAdd(Statistics[1], 1);

	// thin triangles area vs box area
	// thread local

	[branch]
	if (dimensions.x * dimensions.y >= BigTriangleThreshold)
	{
		BigTriangle result;
		result.triangleIndex = StartIndexLocation + dispatchThreadID.x * 3;
		result.instanceIndex = instanceIndex;
		result.baseVertexLocation = BaseVertexLocation;

		float2 tilesCount = ceil(dimensions / BigTriangleTileSize);
		float totalTiles = tilesCount.x * tilesCount.y;
		for (float offset = 0.0; offset < totalTiles; offset += 1.0)
		{
			result.tileOffset = offset;

			BigTriangles.Append(result);
		}

		return;
	}

	float invArea = 1.0 / area;

	// https://www.cs.drexel.edu/~david/Classes/Papers/comp175-06-pineda.pdf
	float2 dxdy0;
	float area0;
	EdgeFunction(p1SS.xy, p2SS.xy, minP.xy, area0, dxdy0);
	float2 dxdy1;
	float area1;
	EdgeFunction(p2SS.xy, p0SS.xy, minP.xy, area1, dxdy1);
	float2 dxdy2;
	float area2;
	EdgeFunction(p0SS.xy, p1SS.xy, minP.xy, area2, dxdy2);

	//  --->----
	// |
	//  --->----
	// |
	//  --->----
	// etc.
	for (float y = minP.y; y <= maxP.y; y += 1.0)
	{
		float area0tmp = area0;
		float area1tmp = area1;
		float area2tmp = area2;
		for (float x = minP.x; x <= maxP.x; x += 1.0)
		{
			// edge tests, "frustum culling" for 3 lines in 2D
			[branch]
			if (area0tmp >= 0.0 && area1tmp >= 0.0 && area2tmp >= 0.0)
			{
				// convert to barycentric weights
				float weight0 = area0tmp * invArea;
				float weight1 = area1tmp * invArea;
				float weight2 = 1.0 - weight0 - weight1;

				precise float depth =
					weight0 * z0NDC +
					weight1 * z1NDC +
					weight2 * z2NDC;

				// TODO: account for non-reversed Z
				InterlockedMax(Depth[uint2(x, y)], asuint(depth));
			}

			// E(x + a, y + b) = E(x, y) - a * dy + b * dx
			area0tmp -= dxdy0.y;
			area1tmp -= dxdy1.y;
			area2tmp -= dxdy2.y;
		}

		area0 += dxdy0.x;
		area1 += dxdy1.x;
		area2 += dxdy2.x;
	}
}