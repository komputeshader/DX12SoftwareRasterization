#include "ForwardRenderer.h"
#include "Win32Application.h"
#include "DescriptorManager.h"
#include "DX.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

ForwardRenderer::ForwardRenderer(
	UINT width,
	UINT height,
	std::wstring name) :
	DXSample(width, height, name)
{
}

void ForwardRenderer::OnInit()
{
	DX::CreateDevice();
	DX::CreateCommandQueues();
	DX::CreateCommandAllocators();
	DX::CreateCommandLists();
	_createSwapChain();
	_createDescriptorHeaps();
	_createFrameResources();

	Scene::PlantScene.LoadPlant();
	Scene::BuddhaScene.LoadBuddha();
	_createVisibleInstancesBuffer();
	_createDepthBufferResources();
	_createCulledCommandsBuffers();
	_loadAssets();

	_timer.Reset();
}

void ForwardRenderer::_createSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = DX::FramesCount;
	swapChainDesc.Width = _width;
	swapChainDesc.Height = _height;
	swapChainDesc.Format = Settings::BackBufferFormat;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(
		DX::Factory->CreateSwapChainForHwnd(
			// Swap chain needs the queue so that it can force a flush on it.
			DX::CommandQueue.Get(),
			Win32Application::GetHwnd(),
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(
		DX::Factory->MakeWindowAssociation(
			Win32Application::GetHwnd(),
			DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(
		swapChain.As(&_swapChain));
	DX::FrameIndex = _swapChain->GetCurrentBackBufferIndex();
}

void ForwardRenderer::_createDescriptorHeaps()
{
	Descriptors::RT.Initialize(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		RTVIndices::RTVCount);

	Descriptors::DS.Initialize(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		DSVIndices::DSVCount);

	Descriptors::SV.Initialize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		CBVSRVUAVIndices::CBVUAVSRVCount);

	Descriptors::NonSV.Initialize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		CBVSRVUAVIndices::CBVUAVSRVCount);
}

void ForwardRenderer::_createFrameResources()
{
	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		ThrowIfFailed(
			_swapChain->GetBuffer(
				frame,
				IID_PPV_ARGS(&_renderTargets[frame])));
		DX::Device->CreateRenderTargetView(
			_renderTargets[frame].Get(),
			nullptr,
			Descriptors::RT.GetCPUHandle(ForwardRendererRTV + frame));
		NAME_D3D12_OBJECT_INDEXED(_renderTargets, frame);
	}
}

void ForwardRenderer::_createVisibleInstancesBuffer()
{
	UINT64 bufferSize = Scene::MaxSceneInstancesCount * sizeof(Instance);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = Scene::MaxSceneInstancesCount;
	SRVDesc.Buffer.StructureByteStride = sizeof(Instance);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	// buffers for visible instances
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(
		bufferSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Buffer.NumElements = Scene::MaxSceneInstancesCount;
	UAVDesc.Buffer.StructureByteStride = sizeof(Instance);
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
		{
			ThrowIfFailed(
				DX::Device->CreateCommittedResource(
					&prop,
					D3D12_HEAP_FLAG_NONE,
					&desc,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
					nullptr,
					IID_PPV_ARGS(&_visibleInstances[frame][frustum])));
			SetNameIndexed(
				_visibleInstances[frame][frustum].Get(),
				L"_visibleInstances",
				frame * _countof(_visibleInstances[frame]) + frustum);

			DX::Device->CreateShaderResourceView(
				_visibleInstances[frame][frustum].Get(),
				&SRVDesc,
				Descriptors::SV.GetCPUHandle(
					VisibleInstancesSRV + frustum +
					frame * PerFrameDescriptorsCount));

			DX::Device->CreateUnorderedAccessView(
				_visibleInstances[frame][frustum].Get(),
				nullptr,
				&UAVDesc,
				Descriptors::SV.GetCPUHandle(
					VisibleInstancesUAV + frustum +
					frame * PerFrameDescriptorsCount));
		}
	}
}

void ForwardRenderer::_createCulledCommandsBuffers()
{
	CD3DX12_RESOURCE_DESC commandBufferDesc =
		CD3DX12_RESOURCE_DESC::Buffer(
			Scene::MaxSceneMeshesMetaCount * sizeof(IndirectCommand),
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Buffer.NumElements = Scene::MaxSceneMeshesMetaCount;
	UAVDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	SRVDesc.Buffer.NumElements = Scene::MaxSceneMeshesMetaCount;
	SRVDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);

	D3D12_DISPATCH_ARGUMENTS dispatch;
	dispatch.ThreadGroupCountX = 1;
	dispatch.ThreadGroupCountY = 1;
	dispatch.ThreadGroupCountZ = 1;

	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
		{
			Utils::CreateDefaultHeapBuffer(
				DX::CommandList.Get(),
				&dispatch,
				sizeof(D3D12_DISPATCH_ARGUMENTS),
				_culledCommandsCounters[frame][frustum],
				_culledCommandsCountersUpload[frame][frustum],
				D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
				true);
			SetNameIndexed(
				_culledCommandsCounters[frame][frustum].Get(),
				L"_culledCommandsCounters",
				frame * Settings::FrustumsCount + frustum);
			SetNameIndexed(
				_culledCommandsCountersUpload[frame][frustum].Get(),
				L"_culledCommandsCountersUpload",
				frame * Settings::FrustumsCount + frustum);

			ThrowIfFailed(
				DX::Device->CreateCommittedResource(
					&prop,
					D3D12_HEAP_FLAG_NONE,
					&commandBufferDesc,
					Settings::SWREnabled
					? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
					: D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
					nullptr,
					IID_PPV_ARGS(&_culledCommands[frame][frustum])));
			SetNameIndexed(
				_culledCommands[frame][frustum].Get(),
				L"_culledCommands",
				frame * Settings::FrustumsCount + frustum);

			DX::Device->CreateUnorderedAccessView(
				_culledCommands[frame][frustum].Get(),
				_culledCommandsCounters[frame][frustum].Get(),
				&UAVDesc,
				Descriptors::SV.GetCPUHandle(
					CulledCommandsUAV + frustum +
					frame * PerFrameDescriptorsCount));

			DX::Device->CreateShaderResourceView(
				_culledCommands[frame][frustum].Get(),
				&SRVDesc,
				Descriptors::SV.GetCPUHandle(
					CulledCommandsSRV + frustum +
					frame * PerFrameDescriptorsCount));
		}
	}
}

void ForwardRenderer::_createDepthBufferResources()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = _width;
	depthStencilDesc.Height = _height;
	depthStencilDesc.DepthOrArraySize = 1;
	// 0 for max number of mips
	depthStencilDesc.MipLevels = 0;
	depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&_prevFrameDepthBuffer)));
	NAME_D3D12_OBJECT(_prevFrameDepthBuffer);

	// UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC depthUAV = {};
	depthUAV.Format = DXGI_FORMAT_R32_UINT;
	depthUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	depthUAV.Texture2D.PlaneSlice = 0;

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSRV = {};
	depthSRV.Format = DXGI_FORMAT_R32_FLOAT;
	depthSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSRV.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSRV.Texture2D.MipLevels = 1;
	depthSRV.Texture2D.PlaneSlice = 0;
	depthSRV.Texture2D.ResourceMinLODClamp = 0.0f;

	for (UINT mip = 0; mip < Settings::BackBufferMipsCount; mip++)
	{
		depthUAV.Texture2D.MipSlice = mip;

		DX::Device->CreateUnorderedAccessView(
			_prevFrameDepthBuffer.Get(),
			nullptr,
			&depthUAV,
			Descriptors::SV.GetCPUHandle(PrevFrameDepthMipsUAV + mip));

		DX::Device->CreateUnorderedAccessView(
			_prevFrameDepthBuffer.Get(),
			nullptr,
			&depthUAV,
			Descriptors::NonSV.GetCPUHandle(PrevFrameDepthMipsUAV + mip));

		depthSRV.Texture2D.MostDetailedMip = mip;

		DX::Device->CreateShaderResourceView(
			_prevFrameDepthBuffer.Get(),
			&depthSRV,
			Descriptors::SV.GetCPUHandle(PrevFrameDepthMipsSRV + mip));
	}

	depthSRV.Format = DXGI_FORMAT_R32_FLOAT;
	depthSRV.Texture2D.MostDetailedMip = 0;
	depthSRV.Texture2D.MipLevels = -1;
	DX::Device->CreateShaderResourceView(
		_prevFrameDepthBuffer.Get(),
		&depthSRV,
		Descriptors::SV.GetCPUHandle(PrevFrameDepthSRV));
}

void ForwardRenderer::PreparePrevFrameDepth(ID3D12Resource* depth)
{
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	D3D12_TEXTURE_COPY_LOCATION src = {};

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_prevFrameDepthBuffer.Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COPY_DEST);
	dst.pResource = _prevFrameDepthBuffer.Get();

	if (Settings::SWREnabled)
	{
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			depth,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		src.pResource = depth;
	}
	else
	{
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			depth,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		src.pResource = depth;
	}
	DX::CommandList->ResourceBarrier(2, barriers);

	dst.SubresourceIndex = 0;
	src.SubresourceIndex = 0;
	DX::CommandList->CopyTextureRegion(
		&dst,
		0,
		0,
		0,
		&src,
		nullptr);

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_prevFrameDepthBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	if (Settings::SWREnabled)
	{
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			depth,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			depth,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}
	DX::CommandList->ResourceBarrier(2, barriers);

	Utils::GenerateHiZ(
		DX::CommandList.Get(),
		_prevFrameDepthBuffer.Get(),
		PrevFrameDepthMipsSRV,
		PrevFrameDepthMipsUAV,
		Settings::BackBufferWidth,
		Settings::BackBufferHeight);
}

void ForwardRenderer::_loadAssets()
{
	Settings::Demo.AssetsPath = _assetsPath;
	Utils::InitializeResources();

	_culler = std::make_unique<decltype(_culler)::element_type>();

	_HWR = std::make_unique<decltype(_HWR)::element_type>();
	_HWR->Resize(
		this,
		_width,
		_height);

	_SWR = std::make_unique<decltype(_SWR)::element_type>();
	_SWR->Resize(
		this,
		_width,
		_height);

	ShadowsResources::Shadows.Initialize();

	_stats = std::make_unique<decltype(_stats)::element_type>();
	_profiler = std::make_unique<decltype(_profiler)::element_type>();

	_initGUI();

	ThrowIfFailed(DX::CommandList->Close());
	ID3D12CommandList* ppCommandLists[] = { DX::CommandList.Get() };
	DX::CommandQueue->ExecuteCommandLists(
		_countof(ppCommandLists),
		ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	DX::CreateSyncObjects();

	// Wait for the command list to execute; we are reusing the same command 
	// list in our main loop but for now, we just want to wait for setup to 
	// complete before continuing.
	_waitForGpu();
}

void ForwardRenderer::OnUpdate()
{
	_timer.Tick();

	OnKeyboardInput();

	Camera& camera = Scene::CurrentScene->camera;
	camera.UpdateViewMatrix();
	ShadowsResources::Shadows.Update();

	_culler->Update();
	_HWR->Update();
	_SWR->Update();
}

void ForwardRenderer::OnRender()
{
	// populate command lists
	_GUINewFrame();
	ShadowsResources::Shadows.GUINewFrame();
	if (Settings::SWREnabled) _SWR->GUINewFrame();
	_beginFrameRendering();
	if (Settings::CullingEnabled && !Settings::FreezeCulling)
	{
		_culler->Cull(
			DX::ComputeCommandList.Get(),
			_visibleInstances[DX::FrameIndex],
			_culledCommands[DX::FrameIndex],
			_culledCommandsCounters[DX::FrameIndex]);
	}
	_stats->BeginMeasure(DX::CommandList.Get());
	if (Settings::SWREnabled)
	{
		_softwareRasterization();
	}
	else
	{
		_HWR->Draw(_renderTargets[DX::FrameIndex].Get());
	}
	_stats->FinishMeasure(DX::CommandList.Get());
	_drawGUI();
	_finishFrameRendering();

	// execute command lists
	if (Settings::CullingEnabled)
	{
		ID3D12CommandList* ppCommandLists[] = { DX::ComputeCommandList.Get() };
		DX::ComputeCommandQueue->ExecuteCommandLists(
			_countof(ppCommandLists),
			ppCommandLists);

		DX::ComputeCommandQueue->Signal(
			DX::ComputeFence.Get(),
			DX::FenceValues[DX::FrameIndex]);

		// wait for the compute results
		DX::CommandQueue->Wait(
			DX::ComputeFence.Get(),
			DX::FenceValues[DX::FrameIndex]);
	}

	ID3D12CommandList* ppCommandLists[] = { DX::CommandList.Get() };
	DX::CommandQueue->ExecuteCommandLists(
		_countof(ppCommandLists),
		ppCommandLists);

	ThrowIfFailed(_swapChain->Present(0, 0));

	_moveToNextFrame();
}

void ForwardRenderer::_beginFrameRendering()
{
	ThrowIfFailed(
		DX::Adapter->QueryVideoMemoryInfo(
			0,
			DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
			&_GPUMemoryInfo));

	ThrowIfFailed(
		DX::Adapter->QueryVideoMemoryInfo(
			0,
			DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
			&_CPUMemoryInfo));

	ThrowIfFailed(DX::CommandAllocators[DX::FrameIndex]->Reset());
	ThrowIfFailed(
		DX::CommandList->Reset(
			DX::CommandAllocators[DX::FrameIndex].Get(),
			nullptr));
	_profiler->BeginMeasure(DX::CommandList.Get());

	if (Settings::CullingEnabled)
	{
		ThrowIfFailed(DX::ComputeCommandAllocators[DX::FrameIndex]->Reset());
		ThrowIfFailed(
			DX::ComputeCommandList->Reset(
				DX::ComputeCommandAllocators[DX::FrameIndex].Get(),
				nullptr));
		//_profiler->BeginMeasure(DX::ComputeCommandList.Get());
	}

	_onRasterizerSwitch();

	ID3D12DescriptorHeap* ppHeaps[] = { Descriptors::SV.GetHeap() };
	DX::CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	if (Settings::CullingEnabled)
	{
		DX::ComputeCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}
}

void ForwardRenderer::_finishFrameRendering()
{
	CD3DX12_RESOURCE_BARRIER barriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			_renderTargets[DX::FrameIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT)
	};
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	if (Settings::CullingEnabled)
	{
		//_profiler->FinishMeasure(DX::ComputeCommandList.Get());
		ThrowIfFailed(DX::ComputeCommandList->Close());
	}
	_profiler->FinishMeasure(DX::CommandList.Get());
	ThrowIfFailed(DX::CommandList->Close());
}

void ForwardRenderer::_softwareRasterization()
{
	_SWR->Draw();

	auto result = _SWR->GetRenderTarget();

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_renderTargets[DX::FrameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_COPY_DEST);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		result,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	DX::CommandList->CopyResource(_renderTargets[DX::FrameIndex].Get(), result);

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_renderTargets[DX::FrameIndex].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		result,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	// for subsequent _drawGUI call
	auto RTVHandle = Descriptors::RT.GetCPUHandle(
		ForwardRendererRTV + DX::FrameIndex);
	DX::CommandList->OMSetRenderTargets(1, &RTVHandle, FALSE, nullptr);
}

void ForwardRenderer::_drawGUI()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"GUI Pass");

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(
		ImGui::GetDrawData(),
		DX::CommandList.Get());

	PIXEndEvent(DX::CommandList.Get());
}

// Wait for pending GPU work to complete.
void ForwardRenderer::_waitForGpu()
{
	// Schedule a Signal command in the queue.
	ThrowIfFailed(
		DX::CommandQueue->Signal(
			DX::Fence.Get(),
			DX::FenceValues[DX::FrameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(
		DX::Fence->SetEventOnCompletion(
			DX::FenceValues[DX::FrameIndex],
			DX::FenceEvent));
	WaitForSingleObjectEx(DX::FenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	DX::FenceValues[DX::FrameIndex]++;
}

// Prepare to render the next frame.
void ForwardRenderer::_moveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = DX::FenceValues[DX::FrameIndex];
	ThrowIfFailed(
		DX::CommandQueue->Signal(
			DX::Fence.Get(),
			currentFenceValue));

	// Update the frame index.
	DX::FrameIndex = _swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (DX::Fence->GetCompletedValue() < DX::FenceValues[DX::FrameIndex])
	{
		ThrowIfFailed(
			DX::Fence->SetEventOnCompletion(
				DX::FenceValues[DX::FrameIndex],
				DX::FenceEvent));
		WaitForSingleObjectEx(DX::FenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	DX::FenceValues[DX::FrameIndex] = currentFenceValue + 1;
}

void ForwardRenderer::_onRasterizerSwitch()
{
	if (_switchToSWR)
	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->positionsGPU.Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->normalsGPU.Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->colorsGPU.Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->texcoordsGPU.Get(),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->indicesGPU.Get(),
				D3D12_RESOURCE_STATE_INDEX_BUFFER,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

		_switchToSWR = false;
	}

	if (_switchFromSWR)
	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->positionsGPU.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->normalsGPU.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->colorsGPU.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->texcoordsGPU.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			CD3DX12_RESOURCE_BARRIER::Transition(
				Scene::CurrentScene->indicesGPU.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_INDEX_BUFFER)
		};
		DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

		_switchFromSWR = false;
	}
}

void ForwardRenderer::_initGUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	// Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// Enable Gamepad Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(Win32Application::GetHwnd());
	ImGui_ImplDX12_Init(
		DX::Device.Get(),
		DX::FramesCount,
		Settings::BackBufferFormat,
		nullptr,
		Descriptors::SV.GetCPUHandle(GUIFontTextureSRV),
		Descriptors::SV.GetGPUHandle(GUIFontTextureSRV));
}

void ForwardRenderer::_GUINewFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// IMGUI code
	int location = Settings::StatsGUILocation;
	ImGuiIO& io = ImGui::GetIO();
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav;
	if (location >= 0)
	{
		float PAD = 10.0f;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Use work area to avoid menu-bar/task-bar, if any!
		ImVec2 work_pos = viewport->WorkPos;
		ImVec2 work_size = viewport->WorkSize;
		ImVec2 window_pos, window_pos_pivot;
		window_pos.x = (location & 1)
			? (work_pos.x + work_size.x - PAD)
			: (work_pos.x + PAD);
		window_pos.y = (location & 2)
			? (work_pos.y + work_size.y - PAD)
			: (work_pos.y + PAD);
		window_pos_pivot.x = (location & 1) ? 1.0f : 0.0f;
		window_pos_pivot.y = (location & 2) ? 1.0f : 0.0f;
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		window_flags |= ImGuiWindowFlags_NoMove;
	}
	// Transparent background
	ImGui::SetNextWindowBgAlpha(Settings::GUITransparency);
	if (ImGui::Begin("Scene Information", nullptr, window_flags))
	{
		ImGui::Text("%ls", DX::AdapterDesc.Description);

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		static int sceneIndex = Buddha;
		if (ImGui::Combo("Scene", &sceneIndex, "Buddha\0Plant\0"))
		{
			if (sceneIndex == Buddha)
			{
				Scene::CurrentScene = &Scene::BuddhaScene;
			}
			else if (sceneIndex == Plant)
			{
				Scene::CurrentScene = &Scene::PlantScene; 
			}
		}

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		ImGui::Text(
			"Total triangles in the scene: %.3f Mil",
			Scene::CurrentScene->totalFacesCount / 1'000'000.0f);

		const auto& stats = _stats->GetStats();

		float pipelineTriangles;
		float trianglesRendered;
		if (Settings::SWREnabled)
		{
			pipelineTriangles =
				static_cast<float>(_SWR->GetPipelineTrianglesCount());
			trianglesRendered =
				static_cast<float>(_SWR->GetRenderedTrianglesCount());
		}
		else
		{
			pipelineTriangles = static_cast<float>(stats.IAPrimitives);
			trianglesRendered = static_cast<float>(stats.CPrimitives);
		}

		ImGui::Text(
			"Triangles On Pipeline: %.3f Mil",
			pipelineTriangles / 1'000'000.0f);

		ImGui::Text(
			"Triangles Rendered: %.3f Mil",
			trianglesRendered / 1'000'000.0f);

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		ImGui::Text(
			"PS Invocations: %.3f Mil",
			stats.PSInvocations / 1'000'000.0f);

		ImGui::Text(
			"CS Invocations: %.3f Mil",
			stats.CSInvocations / 1'000'000.0f);

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		if (Settings::SWREnabled)
		{
			ImGui::Text(
				"Average Vertex Cache Miss Rate\n"
				"(0.5 is ideal, 3.0 is terrible): WIP");
		}
		else
		{
			ImGui::Text(
				"Average Vertex Cache Miss Rate\n"
				"(0.5 is ideal, 3.0 is terrible): %.3f",
				stats.VSInvocations / pipelineTriangles);
		}

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		ImGui::Text(
			"Current DX12 GPU Memory Usage: %.1f GB / %.1f GB",
			_GPUMemoryInfo.CurrentUsage / 1'000'000'000.0f,
			_GPUMemoryInfo.Budget / 1'000'000'000.0f);

		ImGui::Text(
			"Current DX12 CPU Memory Usage: %.1f GB / %.1f GB",
			_CPUMemoryInfo.CurrentUsage / 1'000'000'000.0f,
			_CPUMemoryInfo.Budget / 1'000'000'000.0f);

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		ImGui::Text(
			"Frame Time: %.1f ms",
			_profiler->GetTimeMS(DX::CommandQueue.Get()));

		ImGui::Dummy(ImVec2(0.0f, 10.0f));

		if (ImGui::Checkbox("Software Rasterization", &Settings::SWREnabled))
		{
			if (Settings::SWREnabled)
			{
				_switchToSWR = true;
			}
			else
			{
				_switchFromSWR = true;
			}
		}

		ImGui::Checkbox(
			"Enable Frustum Culling",
			&Settings::FrustumCullingEnabled);

#ifdef SCENE_MESHLETIZATION
		ImGui::Checkbox(
			"Enable Cluster Backface Culling",
			&Settings::ClusterBackfaceCullingEnabled);
#endif

		ImGui::Checkbox(
			"Enable Camera Hi-Z Culling",
			&Settings::CameraHiZCullingEnabled);

		ImGui::Checkbox(
			"Enable Shadows Hi-Z Culling",
			&Settings::ShadowsHiZCullingEnabled);

		if (!Settings::FrustumCullingEnabled
			&& !Settings::CameraHiZCullingEnabled
			&& !Settings::ShadowsHiZCullingEnabled
			&& !Settings::ClusterBackfaceCullingEnabled)
		{
			Settings::CullingEnabled = false;
		}
		else
		{
			Settings::CullingEnabled = true;
		}
	}
	ImGui::End();
}

void ForwardRenderer::_destroyGUI()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void ForwardRenderer::OnSizeChanged(
	UINT width,
	UINT height,
	bool minimized)
{

}

void ForwardRenderer::OnDestroy()
{
	_destroyGUI();

	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	_waitForGpu();

	CloseHandle(DX::FenceEvent);
}

// used for camera movement
void ForwardRenderer::OnKeyboardInput()
{
	float cameraSpeed = 200.0f;

	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
	{
		cameraSpeed = 1000.0f;
	}

	float dt = _timer.DeltaTime() * cameraSpeed;

	Camera& camera = Scene::CurrentScene->camera;

	if ((GetAsyncKeyState(VK_LEFT) & 0x8000)
		|| (GetAsyncKeyState('A') & 0x8000))
	{
		camera.Strafe(-dt);
	}

	if ((GetAsyncKeyState(VK_RIGHT) & 0x8000)
		|| (GetAsyncKeyState('D') & 0x8000))
	{
		camera.Strafe(dt);
	}

	if ((GetAsyncKeyState(VK_UP) & 0x8000)
		|| (GetAsyncKeyState('W') & 0x8000))
	{
		camera.Walk(dt);
	}

	if ((GetAsyncKeyState(VK_DOWN) & 0x8000)
		|| (GetAsyncKeyState('S') & 0x8000))
	{
		camera.Walk(-dt);
	}

	if ((GetAsyncKeyState(VK_SPACE) & 0x8000)
		|| (GetAsyncKeyState('E') & 0x8000))
	{
		camera.MoveVertical(dt);
	}

	if (GetAsyncKeyState('Q') & 0x8000)
	{
		camera.MoveVertical(-dt);
	}
}
void ForwardRenderer::OnKeyDown(UINT8 key)
{
	// F is 0x46
	if (key == 0x46)
	{
		Settings::FreezeCulling = !Settings::FreezeCulling;
	}
}

void ForwardRenderer::OnMouseMove(UINT x, UINT y)
{
	float dx = XMConvertToRadians(
		0.25f * static_cast<float>(static_cast<INT>(x) - _lastMousePos.x));
	float dy = XMConvertToRadians(
		0.25f * static_cast<float>(static_cast<INT>(y) - _lastMousePos.y));

	Scene::CurrentScene->camera.RotateX(dy);
	Scene::CurrentScene->camera.RotateY(dx);

	OnRightButtonDown(x, y);
}

void ForwardRenderer::OnRightButtonDown(UINT x, UINT y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;
}