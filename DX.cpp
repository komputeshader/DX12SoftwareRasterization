#include "DX.h"
#include "DXSampleHelper.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace DX
{

ComPtr<ID3D12Device> Device;
ComPtr<IDXGIFactory4> Factory;
ComPtr<IDXGIAdapter3> Adapter;
ComPtr<ID3D12GraphicsCommandList> CommandList;
ComPtr<ID3D12GraphicsCommandList> ComputeCommandList;
ComPtr<ID3D12CommandAllocator> CommandAllocators[FramesCount];
ComPtr<ID3D12CommandAllocator> ComputeCommandAllocators[FramesCount];
ComPtr<ID3D12CommandQueue> CommandQueue;
ComPtr<ID3D12CommandQueue> ComputeCommandQueue;

HANDLE FenceEvent;
ComPtr<ID3D12Fence> Fence;
ComPtr<ID3D12Fence> ComputeFence;
UINT64 FenceValues[FramesCount];

D3D12_FEATURE_DATA_ROOT_SIGNATURE RSFeatureData;

UINT FrameIndex;

void GetHardwareAdapter(
	IDXGIFactory1* pFactory,
	IDXGIAdapter1** ppAdapter,
	bool requestHighPerformanceAdapter)
{
	*ppAdapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;

	ComPtr<IDXGIFactory6> factory6;
	if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		for (
			UINT adapterIndex = 0;
			SUCCEEDED(factory6->EnumAdapterByGpuPreference(
				adapterIndex,
				requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
				IID_PPV_ARGS(&adapter)));
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}

	if (adapter.Get() == nullptr)
	{
		for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}

	*ppAdapter = adapter.Detach();
}

void CreateDevice()
{
	FrameIndex = 0;

	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	//ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(
		CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&Factory)));

	if (Settings::UseWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(
			Factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(
			D3D12CreateDevice(
				warpAdapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(&Device)));

		ThrowIfFailed(warpAdapter.As(&Adapter));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(Factory.Get(), &hardwareAdapter);

		ThrowIfFailed(
			D3D12CreateDevice(
				hardwareAdapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(&Device)));

		ThrowIfFailed(hardwareAdapter.As(&Adapter));
	}
	NAME_D3D12_OBJECT(Device);

	RSFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(
		Device->CheckFeatureSupport(
			D3D12_FEATURE_ROOT_SIGNATURE,
			&RSFeatureData,
			sizeof(RSFeatureData))))
	{
		RSFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}
}

void CreateCommandAllocators()
{
	for (UINT frame = 0; frame < FramesCount; frame++)
	{
		ThrowIfFailed(
			Device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&CommandAllocators[frame])));
		ThrowIfFailed(
			Device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_COMPUTE,
				IID_PPV_ARGS(&ComputeCommandAllocators[frame])));
	}
}

void CreateCommandLists()
{
	ThrowIfFailed(
		Device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			CommandAllocators[FrameIndex].Get(),
			nullptr,
			IID_PPV_ARGS(&CommandList)));
	NAME_D3D12_OBJECT(CommandList);

	ThrowIfFailed(
		Device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_COMPUTE,
			ComputeCommandAllocators[FrameIndex].Get(),
			nullptr,
			IID_PPV_ARGS(&ComputeCommandList)));
	NAME_D3D12_OBJECT(ComputeCommandList);

	ThrowIfFailed(ComputeCommandList->Close());
}

void CreateCommandQueues()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(
		Device->CreateCommandQueue(
			&queueDesc,
			IID_PPV_ARGS(&CommandQueue)));
	NAME_D3D12_OBJECT(CommandQueue);

	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

	ThrowIfFailed(
		Device->CreateCommandQueue(
			&queueDesc,
			IID_PPV_ARGS(&ComputeCommandQueue)));
	NAME_D3D12_OBJECT(ComputeCommandQueue);
}


void CreateSyncObjects()
{
	ThrowIfFailed(
		Device->CreateFence(
			FenceValues[FrameIndex],
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(&Fence)));
	ThrowIfFailed(
		Device->CreateFence(
			FenceValues[FrameIndex],
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(&ComputeFence)));
	FenceValues[FrameIndex]++;

	FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (FenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

}