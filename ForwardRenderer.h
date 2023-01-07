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

private:

	void _loadAssets();
	void _moveToNextFrame();
	void _waitForGpu();
	void _createDescriptorHeaps();
	void _createFrameResources();
	void _createSwapChain();
	void _createVisibleInstancesBuffer();

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