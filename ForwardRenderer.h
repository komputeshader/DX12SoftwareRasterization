#pragma once

#include "DXSample.h"
#include "Timer.h"
#include "Profiler.h"
#include "Settings.h"
#include "Shadows.h"
#include "Culler.h"
#include "HardwareRasterization.h"
#include "SoftwareRasterization.h"
#include "Scene.h"

class ForwardRenderer : public DXSample
{
public:

	ForwardRenderer(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
	virtual void OnDestroy();

	void OnKeyboardInput();
	virtual void OnMouseMove(UINT x, UINT y);
	virtual void OnRightButtonDown(UINT x, UINT y);
	virtual void OnKeyDown(UINT8 key);

	void PreparePrevFrameDepth(ID3D12Resource* depth);
	ID3D12Resource* GetCulledCommands(UINT frame, UINT frustum)
	{
		assert(frame >= 0);
		assert(frame < DX::FramesCount);
		assert(frustum >= 0);
		assert(frustum < Settings::FrustumsCount);
		return _culledCommands[frame][frustum].Get();
	}
	ID3D12Resource* GetCulledCommandsCounter(UINT frame, UINT frustum)
	{
		assert(frame >= 0);
		assert(frame < DX::FramesCount);
		assert(frustum >= 0);
		assert(frustum < Settings::FrustumsCount);
		return _culledCommandsCounters[frame][frustum].Get();
	}

private:

	void _loadAssets();
	void _moveToNextFrame();
	void _waitForGpu();
	void _createDescriptorHeaps();
	void _createFrameResources();
	void _createSwapChain();
	void _createVisibleInstancesBuffer();
	void _createDepthBufferResources();
	void _createCulledCommandsBuffers();

	void _initGUI();
	void _GUINewFrame();
	void _drawGUI();
	void _destroyGUI();

	void _beginFrameRendering();
	void _finishFrameRendering();
	void _onRasterizerSwitch();
	void _softwareRasterization();

	Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource> _renderTargets[DX::FramesCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_visibleInstances[DX::FramesCount][Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> _prevFrameDepthBuffer;
	// per frame granularity for async compute and graphics work
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_culledCommands[DX::FramesCount][Settings::FrustumsCount];
	// first 4 bytes used as a counter
	// all 12 bytes are used as a dispatch indirect command
	// [0] - counter / group count X
	// [1] - group count Y
	// [2] - group count Z
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_culledCommandsCounters[DX::FramesCount][Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_culledCommandsCountersUpload[DX::FramesCount][Settings::FrustumsCount];

	std::unique_ptr<Culler> _culler;
	std::unique_ptr<HardwareRasterization> _HWR;
	std::unique_ptr<SoftwareRasterization> _SWR;
	std::unique_ptr<FrameStatistics> _stats;
	std::unique_ptr<Profiler> _profiler;
	DXGI_QUERY_VIDEO_MEMORY_INFO _GPUMemoryInfo;
	DXGI_QUERY_VIDEO_MEMORY_INFO _CPUMemoryInfo;
	Timer _timer;
	POINT _lastMousePos;

	bool _switchToSWR = false;
	bool _switchFromSWR = false;
};