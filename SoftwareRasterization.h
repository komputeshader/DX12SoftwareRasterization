#pragma once

#include "DX.h"
#include "Utils.h"
#include "Shadows.h"

class SoftwareRasterization
{
public:

	SoftwareRasterization() = default;
	SoftwareRasterization(const SoftwareRasterization&) = delete;
	SoftwareRasterization& operator=(const SoftwareRasterization&) = delete;
	~SoftwareRasterization() = default;

	void Resize(
		UINT width,
		UINT height);

	void GUINewFrame();
	void Update();
	void Draw();

	ID3D12Resource* GetRenderTarget() const { return _renderTarget.Get(); }
	auto* GetCulledCommands(UINT frame)
	{
		assert(frame >= 0);
		assert(frame < DX::FramesCount);
		return _culledCommands[frame];
	}

	UINT GetCulledCommandsCounterOffset() const
	{
		return _culledCommandsCounterOffset;
	}

	UINT GetPipelineTrianglesCount() const
	{
		return _statsResult[PipelineTriangles];
	}

	UINT GetRenderedTrianglesCount() const
	{
		return _statsResult[RenderedTriangles];
	}

private:

	// WIP
	enum RSSlots
	{
		SceneCB,
		RootConstants
	};

	void _createRenderTargetResources();
	void _createDepthBufferResources();
	void _createBigTrianglesMDIResources();
	void _createCullingMDIResources();
	void _createResetBuffer();
	void _clearBigTrianglesCounter();
	void _createBigTrianglesDispatch();
	void _createStatsResources();
	void _clearStatistics();

	void _beginFrame();
	void _drawDepth();
	void _drawShadows();
	void _drawOpaque();
	void _endFrame();

	void _drawIndexedInstanced(
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation);

	void _createHiZPSO();
	void _createTriangleDepthPSO();
	void _createBigTriangleDepthPSO();
	void _createTriangleOpaquePSO();
	void _createBigTriangleOpaquePSO();

	// need these two for UAV writes
	Microsoft::WRL::ComPtr<ID3D12Resource> _renderTarget;
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> _prevDepthBuffer;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _triangleDepthRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _triangleDepthPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _bigTriangleDepthRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _bigTriangleDepthPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _triangleOpaqueRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _triangleOpaquePSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _bigTriangleOpaqueRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _bigTriangleOpaquePSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _HiZRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _HiZPSO;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bigTriangles;
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthSceneCB;
	UINT8* _depthSceneCBData;
	UINT _depthSceneCBFrameSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> _sceneCB;
	UINT8* _sceneCBData;

	UINT _width = 0;
	UINT _height = 0;

	// MDI stuff
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> _triangleDepthCS;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> _triangleOpaqueCS;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> _bigTrianglesCS;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bigTrianglesDispatch;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bigTrianglesDispatchUpload;
	Microsoft::WRL::ComPtr<ID3D12Resource> _counterReset;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_culledCommands[DX::FramesCount][Settings::FrustumsCount];
	UINT _culledCommandsCounterOffset = 0;
	UINT _bigTrianglesCounterOffset = 0;

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
};