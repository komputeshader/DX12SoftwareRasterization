#ifndef TYPES_AND_CONSTANTS_HLSL
#define TYPES_AND_CONSTANTS_HLSL

static const uint MaxCascadesCount = 8;
static const float3 SkyColor = float3(136.0, 198.0, 252.0) / 255.0;

static const float FloatMax = 3.402823466e+38;
static const float FloatMin = 1.175494351e-38;

static const uint SWRBigTriangleThreadsX = 8;
static const uint SWRBigTriangleThreadsY = 8;
static const uint SWRBigTriangleThreadsZ = 1;

// should match it's duplicates in Setttings.h
static const uint CullingThreadsX = 256;
static const uint CullingThreadsY = 1;
static const uint CullingThreadsZ = 1;

static const uint SWRTriangleThreadsX = 256;
static const uint SWRTriangleThreadsY = 1;
static const uint SWRTriangleThreadsZ = 1;

static const uint HiZThreadsX = 8;
static const uint HiZThreadsY = 8;
static const uint HiZThreadsZ = 1;

// https://developer.nvidia.com/content/understanding-structured-buffer-performance

struct VertexPosition
{
	float3 position;
	float pad;
};

struct VertexNormal
{
	float3 normal;
	float pad;
};

struct VertexColor
{
	float3 color;
	float pad;
};

struct VertexUV
{
	float2 uv;
	float2 pad;
};

struct Frustum
{
	float4 left;
	float4 right;
	float4 bottom;
	float4 top;
	float4 near;
	float4 far;

	float4 corners[8];
};

struct AABB
{
	float3 center;
	float pad0;
	float3 extents;
	float pad1;
};

struct MeshMeta
{
	AABB aabb;

	uint indexCountPerInstance;
	uint instanceCount;
	uint startIndexLocation;
	int baseVertexLocation;
	uint startInstanceLocation;

	float3 coneApex;
	float3 coneAxis;
	float coneCutoff;
};

struct Instance
{
	float4x4 worldTransform;
	uint meshID;
	float3 color;
};

struct DrawIndexedArguments
{
	uint indexCountPerInstance;
	uint instanceCount;
	uint startIndexLocation;
	int baseVertexLocation;
	uint startInstanceLocation;
};

struct DispatchArguments
{
	uint threadGroupCountX;
	uint threadGroupCountY;
	uint threadGroupCountZ;
};

struct TriangleCommand
{
	// root constants
	uint indexCountPerInstance;
	uint startIndexLocation;
	int baseVertexLocation;
	uint startInstanceLocation;

	DispatchArguments args;
};

struct BigTriangle
{
	uint triangleIndex;
	uint instanceIndex;
	int baseVertexLocation;

	float tileOffset;
};

#endif // TYPES_AND_CONSTANTS_HLSL