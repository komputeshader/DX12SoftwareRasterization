#pragma once

#include "Common.h"

static const float SkyColor[] =
{
	136.0f / 255.0f,
	198.0f / 255.0f,
	252.0f / 255.0f,
	1.0f
};

static const DirectX::XMFLOAT4X4 Identity4x4 =
{
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

// https://developer.nvidia.com/content/understanding-structured-buffer-performance

struct VertexPosition
{
	// .x : |16 bits - x component | 16 bits - y component |
	// .y : |16 bits - z component | 16 bits - unused      |
	DirectX::XMUINT2 packedPosition;
};

struct VertexNormal
{
	// | 2 bits - unused | 10 bits - x | 10 bits - y | 10 bits - z |
	UINT packedNormal;
};

struct VertexColor
{
	// .x : |16 bits - r component | 16 bits - g component |
	// .y : |16 bits - b component | 16 bits - a component |
	DirectX::XMUINT2 packedColor;
};

struct VertexUV
{
	// | 16 bits - u | 16 bits - v |
	UINT packedUV;
};

class AABB
{
public:

	DirectX::XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
	float pad0;
	DirectX::XMFLOAT3 extents = { 0.0f, 0.0f, 0.0f };
	float pad1;

	float GetDiagonalLength() const
	{
		return 2.0f * sqrtf(
			extents.x * extents.x +
			extents.y * extents.y +
			extents.z * extents.z);
	}
};

struct MeshMeta
{
	AABB AABB;

	UINT indexCountPerInstance;
	UINT instanceCount;
	UINT startIndexLocation;
	INT baseVertexLocation;
	UINT startInstanceLocation;

	DirectX::XMFLOAT3 coneApex;
	DirectX::XMFLOAT3 coneAxis;
	float coneCutoff;
};

struct Frustum
{
	DirectX::XMFLOAT4 l;
	DirectX::XMFLOAT4 r;
	DirectX::XMFLOAT4 b;
	DirectX::XMFLOAT4 t;
	DirectX::XMFLOAT4 n;
	DirectX::XMFLOAT4 f;

	DirectX::XMFLOAT4 cornersWS[8];
};

struct Instance
{
	DirectX::XMFLOAT4X4 worldTransform;
	UINT meshID;
	DirectX::XMFLOAT3 color;
};

struct IndirectCommand
{
	UINT startInstanceLocation;
	D3D12_DRAW_INDEXED_ARGUMENTS arguments;
};

struct DepthSceneCB
{
	DirectX::XMFLOAT4X4 VP;
	float pad[48];
};
static_assert(
	(sizeof(DepthSceneCB) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

class ShaderHelper
{
public:

	byte* data;
	UINT size;

	~ShaderHelper()
	{
		delete[] data;
	}
};

enum ScenesIndices
{
	Buddha,
	Plant,
	ScenesCount
};