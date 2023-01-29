#pragma once

#include "Timer.h"
#include "Profiler.h"
#include "Settings.h"
#include "Shadows.h"

class ForwardRenderer;

class HardwareRasterization
{
public:

	HardwareRasterization() = default;
	HardwareRasterization(const HardwareRasterization&) = delete;
	HardwareRasterization& operator=(const HardwareRasterization&) = delete;
	~HardwareRasterization() = default;

	void Resize(
		ForwardRenderer* renderer,
		UINT width,
		UINT height);
	void Draw(ID3D12Resource* renderTarget);
	void Update();

private:

	void _loadAssets();
	void _createDepthBufferResources();

	void _createHWRRS();
	void _createDepthPassPSO();
	void _createOpaquePassPSO();
	void _createMDIStuff();

	void _beginFrame();
	void _drawDepth();
	void _drawShadows();
	void _drawOpaque(ID3D12Resource* renderTarget);
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

	UINT _width;
	UINT _height;
	ForwardRenderer* _renderer;
};