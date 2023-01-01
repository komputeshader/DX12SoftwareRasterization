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

typedef OpaqueVertex Vertex;

StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<uint> Indices : register(t1);
StructuredBuffer<Instance> Instances : register(t2);
StructuredBuffer<BigTriangle> BigTriangles : register(t3);
Texture2D Depth : register(t4);
Texture2DArray ShadowMap : register(t5);

SamplerState PointClampSampler : register(s0);

RWTexture2D<float4> RenderTarget : register(u0);

groupshared float2 MinP;
groupshared float2 MaxP;
groupshared float2 P0SS;
groupshared float2 P1SS;
groupshared float2 P2SS;
groupshared Vertex V0;
groupshared Vertex V1;
groupshared Vertex V2;
groupshared float Z0NDC;
groupshared float Z1NDC;
groupshared float Z2NDC;
groupshared float InvW0;
groupshared float InvW1;
groupshared float InvW2;
groupshared float InvArea;
groupshared float Area0;
groupshared float Area1;
groupshared float Area2;
groupshared float2 Dxdy0;
groupshared float2 Dxdy1;
groupshared float2 Dxdy2;

#include "Common.hlsli"
#include "Rasterization.hlsli"

[numthreads(
	SWRBigTriangleThreadsX,
	SWRBigTriangleThreadsY,
	SWRBigTriangleThreadsZ)]
void main(
	uint3 groupID : SV_GroupID,
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (groupIndex == 0)
	{
		BigTriangle t = BigTriangles[groupID.x];

		// no tests checks for this triangle, since it had passed them already

		Vertex v0, v1, v2;
		GetTriangleVertices(
			t.triangleIndex,
			t.baseVertexLocation,
			v0, v1, v2);

		float4 p0CS, p1CS, p2CS;
		GetCSPositions(
			t.instanceIndex,
			v0.position, v1.position, v2.position,
			p0CS, p1CS, p2CS);

		float invW0 = 1.0 / p0CS.w;
		float invW1 = 1.0 / p1CS.w;
		float invW2 = 1.0 / p2CS.w;

		float2 p0SS, p1SS, p2SS;
		GetSSPositions(
			p0CS.xy, p1CS.xy, p2CS.xy,
			invW0, invW1, invW2,
			p0SS, p1SS, p2SS);

		float area = Area(p0SS.xy, p1SS.xy, p2SS.xy);

		float z0NDC = p0CS.z * invW0;
		float z1NDC = p1CS.z * invW1;
		float z2NDC = p2CS.z * invW2;

		float3 minP = min(
			min(float3(p0SS.xy, z0NDC), float3(p1SS.xy, z1NDC)),
			float3(p2SS.xy, z2NDC));
		float3 maxP = max(
			max(float3(p0SS.xy, z0NDC), float3(p1SS.xy, z1NDC)),
			float3(p2SS.xy, z2NDC));

		ClampToScreenBounds(minP.xy, maxP.xy);
		MinP = SnapMinBoundToPixelCenter(minP.xy);
		MaxP = maxP.xy;

		P0SS = p0SS;
		P1SS = p1SS;
		P2SS = p2SS;
		Z0NDC = z0NDC;
		Z1NDC = z1NDC;
		Z2NDC = z2NDC;
		V0 = v0;
		V1 = v1;
		V2 = v2;
		InvW0 = invW0;
		InvW1 = invW1;
		InvW2 = invW2;
		InvArea = 1.0 / area;
		// https://www.cs.drexel.edu/~david/Classes/Papers/comp175-06-pineda.pdf
		EdgeFunction(p1SS.xy, p2SS.xy, MinP, Area0, Dxdy0);
		EdgeFunction(p2SS.xy, p0SS.xy, MinP, Area1, Dxdy1);
		EdgeFunction(p0SS.xy, p1SS.xy, MinP, Area2, Dxdy2);
	}

	GroupMemoryBarrierWithGroupSync();

	float2 dimensions = MaxP - MinP;

	uint yTiles = 0;
	for (float y = MinP.y + groupThreadID.y;
		y <= MaxP.y;
		y += SWRBigTriangleThreadsY, yTiles++)
	{
		uint xTiles = 0;
		for (float x = MinP.x + groupThreadID.x;
			x <= MaxP.x;
			x += SWRBigTriangleThreadsX, xTiles++)
		{
			uint xOffset = groupThreadID.x + xTiles * SWRBigTriangleThreadsX;
			uint yOffset = groupThreadID.y + yTiles * SWRBigTriangleThreadsY;

			// E(x + a, y + b) = E(x, y) - a * dy + b * dx
			float area0 =
				Area0 -
				xOffset * Dxdy0.y +
				yOffset * Dxdy0.x;
			float area1 =
				Area1 -
				xOffset * Dxdy1.y +
				yOffset * Dxdy1.x;
			float area2 =
				Area2 -
				xOffset * Dxdy2.y +
				yOffset * Dxdy2.x;
			// edge tests, "frustum culling" for 3 lines in 2D
			[branch]
			if (area0 >= 0.0 && area1 >= 0.0 && area2 >= 0.0)
			{
				// convert to barycentric weights
				float weight0 = area0 * InvArea;
				float weight1 = area1 * InvArea;
				float weight2 = 1.0 - weight0 - weight1;

				precise float depth =
					weight0 * Z0NDC +
					weight1 * Z1NDC +
					weight2 * Z2NDC;
				// early z test
				[branch]
				if (Depth[uint2(x, y)].r == depth)
				{
					// for perspective-correct interpolation
					float denom = 1.0 / (
						weight0 * InvW0 +
						weight1 * InvW1 +
						weight2 * InvW2);

					float3 N = denom * (
						weight0 * V0.normal * InvW0 +
						weight1 * V1.normal * InvW1 +
						weight2 * V2.normal * InvW2);
					N = normalize(N);
					float3 color = denom * (
						weight0 * V0.color * InvW0 +
						weight1 * V1.color * InvW1 +
						weight2 * V2.color * InvW2);
					float3 positionWS = denom * (
						weight0 * V0.position * InvW0 +
						weight1 * V1.position * InvW1 +
						weight2 * V2.position * InvW2);

					float NdotL = saturate(dot(SunDirection, N));
					float viewDepth = denom;
					float shadow = GetShadow(viewDepth, positionWS);
					float3 ambient = 0.2 * SkyColor;

					RenderTarget[uint2(x, y)] = float4(
						color * (NdotL * shadow + ambient),
						1.0);
				}
			}
		}
	}
}