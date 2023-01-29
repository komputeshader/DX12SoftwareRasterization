#pragma once

#include "DX.h"
#include "Utils.h"
#include "Shadows.h"

class ForwardRenderer;

class SoftwareRasterization
{
public:

	SoftwareRasterization() = default;
	SoftwareRasterization(const SoftwareRasterization&) = delete;
	SoftwareRasterization& operator=(const SoftwareRasterization&) = delete;
	~SoftwareRasterization() = default;

	void Resize(
		ForwardRenderer* renderer,
		UINT width,
		UINT height);
	void GUINewFrame();
	void Update();
	void Draw();

	ID3D12Resource* GetRenderTarget() const { return _renderTarget.Get(); }

	UINT GetPipelineTrianglesCount() const
	{
		return _statsResult[PipelineTriangles];
	}

	UINT GetRenderedTrianglesCount() const
	{
		return _statsResult[RenderedTriangles];
	}

private:

	void _createRenderTargetResources();
	void _createDepthBufferResources();
	void _createMDIResources();
	void _createResetBuffer();
	void _clearBigTrianglesCounter(UINT frustum);
	void _createStatsResources();
	void _clearStatistics();

	void _beginFrame();
	void _drawDepth();
	void _drawDepthBigTriangles();
	void _drawShadows();
	void _drawShadowsBigTriangles();
	void _finishDepthsRendering();
	void _drawOpaque();
	void _endFrame();

	void _drawIndexedInstanced(
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation);

	void _createTriangleDepthPSO();
	void _createBigTriangleDepthPSO();
	void _createTriangleOpaquePSO();
	void _createBigTriangleOpaquePSO();
	void _createBigTrianglesBuffers();

	// need these two for UAV writes
	Microsoft::WRL::ComPtr<ID3D12Resource> _renderTarget;
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _triangleDepthRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _triangleDepthPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _bigTriangleDepthRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _bigTriangleDepthPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _triangleOpaqueRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _triangleOpaquePSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _bigTriangleOpaqueRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _bigTriangleOpaquePSO;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_bigTriangles[Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthSceneCB;
	UINT8* _depthSceneCBData;
	UINT _depthSceneCBFrameSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> _sceneCB;
	UINT8* _sceneCBData;

	UINT _width = 0;
	UINT _height = 0;
	ForwardRenderer* _renderer;

	// MDI stuff
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> _dispatchCS;
	// first 4 bytes used as a counter
	// all 12 bytes are used as a dispatch indirect command
	// [0] - counter / group count X
	// [1] - group count Y
	// [2] - group count Z
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_bigTrianglesCounters[Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_bigTrianglesCountersUpload[Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> _counterReset;

	// statistics resources
	enum StatsIndices
	{
		PipelineTriangles,
		RenderedTriangles,
		StatsCount
	};
	Microsoft::WRL::ComPtr<ID3D12Resource> _trianglesStats;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_trianglesStatsReadback[DX::FramesCount];
	UINT _statsResult[StatsCount];

	// how much screen space area should triangle's AABB occupy
	// to be considered "big"
	INT _bigTriangleThreshold = 64;
	INT _bigTriangleTileSize = 128;
};