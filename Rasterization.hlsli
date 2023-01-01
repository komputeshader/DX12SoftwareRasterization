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

void GetTriangleVertices(
	in uint startIndexLocation,
	in uint baseVertexLocation,
	out Vertex v0,
	out Vertex v1,
	out Vertex v2)
{
	uint v0Idx = Indices[startIndexLocation + 0];
	uint v1Idx = Indices[startIndexLocation + 1];
	uint v2Idx = Indices[startIndexLocation + 2];

	v0 = Vertices[baseVertexLocation + v0Idx];
	v1 = Vertices[baseVertexLocation + v1Idx];
	v2 = Vertices[baseVertexLocation + v2Idx];
}

void GetCSPositions(
	in uint instanceIndex,
	inout float3 p0,
	inout float3 p1,
	inout float3 p2,
	out float4 p0CS,
	out float4 p1CS,
	out float4 p2CS)
{
	// instancing
	Instance instance = Instances[instanceIndex];

	// MS -> WS -> VS -> CS
	p0 = mul(instance.worldTransform, float4(p0, 1.0)).xyz;
	p1 = mul(instance.worldTransform, float4(p1, 1.0)).xyz;
	p2 = mul(instance.worldTransform, float4(p2, 1.0)).xyz;

	p0CS = mul(VP, float4(p0, 1.0));
	p1CS = mul(VP, float4(p1, 1.0));
	p2CS = mul(VP, float4(p2, 1.0));
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
