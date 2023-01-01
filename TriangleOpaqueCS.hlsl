// Implicit assumptions:
// - we are dealing with triangles
// - indices represent a triangle list

#include "TypesAndConstants.hlsli"

cbuffer SceneCB : register(b0)
{
	float4x4 VP;
	float4x4 CascadeVP[MaxCascadesCount];
	float3 SunDirection;
	uint CascadesCount;
	float2 OutputRes;
	float2 InvOutputRes;
	float BigTriangleThreshold;
	uint3 pad;
	float4 CascadeBias[MaxCascadesCount / 4];
	float4 CascadeSplits[MaxCascadesCount / 4];
};

cbuffer ConstantBuffer : register(b1)
{
	uint IndexCountPerInstance;
	uint StartIndexLocation;
	uint BaseVertexLocation;
	uint StartInstanceLocation;
}

typedef OpaqueVertex Vertex;

StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<uint> Indices : register(t1);
StructuredBuffer<Instance> Instances : register(t2);
Texture2D Depth : register(t3);
Texture2DArray ShadowMap : register(t4);

SamplerState PointClampSampler : register(s0);
SamplerState DepthSampler : register(s1);

RWTexture2D<float4> RenderTarget : register(u0);
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

	Vertex v0, v1, v2;
	GetTriangleVertices(
		StartIndexLocation + dispatchThreadID.x * 3,
		BaseVertexLocation,
		v0, v1, v2);

	float4 p0CS, p1CS, p2CS;
	uint instanceID = groupID.y;
	uint instanceIndex = StartInstanceLocation + instanceID;
	GetCSPositions(
		instanceIndex,
		v0.position, v1.position, v2.position,
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
		|| maxP.y < 0.0 || minP.y >= OutputRes.y
		/*|| minP.z > 1.0 || maxP.z < 0.0*/)
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

	// one more triangle was rendered
	// not precise, though, since it still could miss any pixel centers
	InterlockedAdd(Statistics[1], 1);

	// Hi Z
	float mipLevel =
		ceil(log2(0.5 * max(dimensions.x, dimensions.y)));
	float tileDepth = Depth.SampleLevel(
		DepthSampler,
		(minP.xy + maxP.xy) * 0.5 * InvOutputRes,
		mipLevel).r;
	[branch]
	if (tileDepth > maxP.z)
	{
		return;
	}

	[branch]
	if (dimensions.x * dimensions.y >= BigTriangleThreshold)
	{
		BigTriangle result;
		result.triangleIndex = StartIndexLocation + dispatchThreadID.x * 3;
		result.instanceIndex = instanceIndex;
		result.baseVertexLocation = BaseVertexLocation;
		result.pad = 0.0;

		BigTriangles.Append(result);

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

				uint2 pixelCoord = uint2(x, y);

				// early z test
				[branch]
				if (Depth[pixelCoord].r == depth)
				{
					// for perspective-correct interpolation
					float denom = 1.0 / (
						weight0 * invW0 +
						weight1 * invW1 +
						weight2 * invW2);

					float3 N = denom * (
						weight0 * v0.normal * invW0 +
						weight1 * v1.normal * invW1 +
						weight2 * v2.normal * invW2);
					N = normalize(N);
					float3 color = denom * (
						weight0 * v0.color * invW0 +
						weight1 * v1.color * invW1 +
						weight2 * v2.color * invW2);
					float3 positionWS = denom * (
						weight0 * v0.position * invW0 +
						weight1 * v1.position * invW1 +
						weight2 * v2.position * invW2);

					float NdotL = saturate(dot(SunDirection, N));
					float viewDepth = denom;
					float shadow = GetShadow(viewDepth, positionWS);
					float3 ambient = 0.2 * SkyColor;

					RenderTarget[pixelCoord] = float4(
						color * (NdotL * shadow + ambient),
						1.0);
				}
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