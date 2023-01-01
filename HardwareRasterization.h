#pragma once

#include "Timer.h"
#include "Profiler.h"
#include "Settings.h"
#include "Shadows.h"

class HardwareRasterization
{
public:

	HardwareRasterization() = default;
	HardwareRasterization(const HardwareRasterization&) = delete;
	HardwareRasterization& operator=(const HardwareRasterization&) = delete;
	~HardwareRasterization() = default;

	void Resize(
		UINT width,
		UINT height);
	void Draw(
		const D3D12_VERTEX_BUFFER_VIEW& depthVertexBufferView,
		const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
		const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
		ID3D12Resource* renderTarget,
		CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle);
	void Update();

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

private:

	void _loadAssets();
	void _createDepthBufferResources();

	void _createHWRRS();
	void _createDepthPassPSO();
	void _createOpaquePassPSO();
	void _createMDIStuff();

	void _beginFrame(
		const D3D12_INDEX_BUFFER_VIEW& indexBufferView);
	void _drawDepth(
		const D3D12_VERTEX_BUFFER_VIEW& depthVertexBufferView);
	void _drawShadows();
	void _drawOpaque(
		const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
		ID3D12Resource* renderTarget,
		CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle);
	void _endFrame();

	CD3DX12_VIEWPORT _viewport;
	CD3DX12_RECT _scissorRect;

	Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _HWRRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _opaquePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _depthPSO;
	DXGI_FORMAT _depthFormat = DXGI_FORMAT_D32_FLOAT;

	Microsoft::WRL::ComPtr<ID3D12Resource> _depthSceneCB;
	UINT8* _depthSceneCBData;
	UINT _depthSceneCBFrameSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> _sceneCB;
	UINT8* _sceneCBData;

	// MDI stuff
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> _commandSignature;
	// per frame granularity for async compute and graphics work
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_culledCommands[DX::FramesCount][Settings::FrustumsCount];
	UINT _culledCommandsCounterOffset = 0;

	UINT _width;
	UINT _height;
};