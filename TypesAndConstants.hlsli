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

static const uint HiZThreadsX = 16;
static const uint HiZThreadsY = 16;
static const uint HiZThreadsZ = 1;

// https://developer.nvidia.com/content/understanding-structured-buffer-performance

struct VertexPosition
{
	// .x : | 16 bits - x component | 16 bits - y component |
	// .y : | 16 bits - z component | 16 bits - unused      |
	uint2 packedPosition;
};

struct VertexNormal
{
	// | 2 bits - unused | 10 bits - x | 10 bits - y | 10 bits - z |
	uint packedNormal;
};

struct VertexColor
{
	// .x : |16 bits - r component | 16 bits - g component |
	// .y : |16 bits - b component | 16 bits - a component |
	uint2 packedColor;
};

struct VertexUV
{
	// | 16 bits - u | 16 bits - v |
	uint packedUV;
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

struct IndirectCommand
{
	uint startInstanceLocation;
	DrawIndexedArguments args;
};

struct BigTriangle
{
	uint triangleIndex;
	uint instanceIndex;
	int baseVertexLocation;

	float tileOffset;
};

#endif // TYPES_AND_CONSTANTS_HLSL