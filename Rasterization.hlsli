#ifndef RASTERIZATION_HLSL
#define RASTERIZATION_HLSL

float Area(in float2 v0, in float2 v1, in float2 v2)
{
	float2 e0 = v1 - v0;
	float2 e1 = v2 - v0;
	return e0.x * e1.y - e1.x * e0.y;
}

void EdgeFunction(
	in float2 v0,
	in float2 v1,
	in float2 p,
	out float area,
	out float2 dxdy)
{
	float2 e0 = v1 - v0;
	float2 e1 = p - v0;
	area = e0.x * e1.y - e1.x * e0.y;
	dxdy = e0;
}

void GetTriangleIndices(
	in uint startIndexLocation,
	out uint v0Idx,
	out uint v1Idx,
	out uint v2Idx)
{
	v0Idx = Indices[startIndexLocation + 0];
	v1Idx = Indices[startIndexLocation + 1];
	v2Idx = Indices[startIndexLocation + 2];
}

void GetTriangleVertexPositions(
	in uint v0Idx, in uint v1Idx, in uint v2Idx,
	in uint baseVertexLocation,
	out float3 v0P,
	out float3 v1P,
	out float3 v2P)
{
	v0P = Positions[baseVertexLocation + v0Idx].position;
	v1P = Positions[baseVertexLocation + v1Idx].position;
	v2P = Positions[baseVertexLocation + v2Idx].position;
}

#ifdef OPAQUE

void GetTriangleVertexNormals(
	in uint v0Idx, in uint v1Idx, in uint v2Idx,
	in uint baseVertexLocation,
	out float3 v0N,
	out float3 v1N,
	out float3 v2N)
{
	v0N = Normals[baseVertexLocation + v0Idx].normal;
	v1N = Normals[baseVertexLocation + v1Idx].normal;
	v2N = Normals[baseVertexLocation + v2Idx].normal;
}

void GetTriangleVertexColors(
	in uint v0Idx, in uint v1Idx, in uint v2Idx,
	in uint baseVertexLocation,
	out float3 v0C,
	out float3 v1C,
	out float3 v2C)
{
	v0C = Colors[baseVertexLocation + v0Idx].color;
	v1C = Colors[baseVertexLocation + v1Idx].color;
	v2C = Colors[baseVertexLocation + v2Idx].color;
}

void GetTriangleVertexUVs(
	in uint v0Idx, in uint v1Idx, in uint v2Idx,
	in uint baseVertexLocation,
	out float2 v0UV,
	out float2 v1UV,
	out float2 v2UV)
{
	v0UV = UVs[baseVertexLocation + v0Idx].uv;
	v1UV = UVs[baseVertexLocation + v1Idx].uv;
	v2UV = UVs[baseVertexLocation + v2Idx].uv;
}

#endif // OPAQUE

void GetCSPositions(
	in Instance instance,
	in float3 p0,
	in float3 p1,
	in float3 p2,
	out float3 p0WS,
	out float3 p1WS,
	out float3 p2WS,
	out float4 p0CS,
	out float4 p1CS,
	out float4 p2CS)
{
	// MS -> WS
	p0WS = mul(instance.worldTransform, float4(p0, 1.0)).xyz;
	p1WS = mul(instance.worldTransform, float4(p1, 1.0)).xyz;
	p2WS = mul(instance.worldTransform, float4(p2, 1.0)).xyz;

	// WS -> VS -> CS
	p0CS = mul(VP, float4(p0WS, 1.0));
	p1CS = mul(VP, float4(p1WS, 1.0));
	p2CS = mul(VP, float4(p2WS, 1.0));
}

void GetSSPositions(
	in float2 p0CS, in float2 p1CS, in float2 p2CS,
	in float invW0, in float invW1, in float invW2,
	out float2 p0SS,
	out float2 p1SS,
	out float2 p2SS)
{
	// CS -> NDC
	float2 p0NDC = p0CS * invW0;
	float2 p1NDC = p1CS * invW1;
	float2 p2NDC = p2CS * invW2;

	// NDC -> DX [0,1]
	p0SS = p0NDC * float2(0.5, -0.5) + float2(0.5, 0.5);
	p1SS = p1NDC * float2(0.5, -0.5) + float2(0.5, 0.5);
	p2SS = p2NDC * float2(0.5, -0.5) + float2(0.5, 0.5);

	// DX [0,1] -> SS
	p0SS *= OutputRes;
	p1SS *= OutputRes;
	p2SS *= OutputRes;
}

void ClampToScreenBounds(
	inout float2 minP,
	inout float2 maxP)
{
	minP = clamp(minP, float2(0.0, 0.0), OutputRes);
	maxP = clamp(maxP, float2(0.0, 0.0), OutputRes);
}

float2 SnapMinBoundToPixelCenter(in float2 minP)
{
	return ceil(minP - float2(0.5, 0.5)) + float2(0.5, 0.5);
}

#endif // RASTERIZATION_HLSL
