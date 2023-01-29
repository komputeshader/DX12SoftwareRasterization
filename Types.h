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
	DirectX::XMFLOAT3 position;
	float pad;
};

struct VertexNormal
{
	DirectX::XMFLOAT3 normal;
	float pad;
};

struct VertexColor
{
	DirectX::XMFLOAT3 color;
	float pad;
};

struct VertexUV
{
	DirectX::XMFLOAT2 uv;
	DirectX::XMFLOAT2 pad;
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