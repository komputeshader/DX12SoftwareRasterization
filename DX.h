#pragma once

#include "Settings.h"

namespace DX
{

const UINT FramesCount = 2;
extern UINT FrameIndex;
extern DXGI_ADAPTER_DESC1 AdapterDesc;

extern Microsoft::WRL::ComPtr<ID3D12Device> Device;
extern Microsoft::WRL::ComPtr<IDXGIFactory4> Factory;
extern Microsoft::WRL::ComPtr<IDXGIAdapter3> Adapter;
extern Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;
extern Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> ComputeCommandList;
extern Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
	CommandAllocators[FramesCount];
extern Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
	ComputeCommandAllocators[FramesCount];
extern Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue;
extern Microsoft::WRL::ComPtr<ID3D12CommandQueue> ComputeCommandQueue;

// synchronization objects
extern HANDLE FenceEvent;
extern Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
extern Microsoft::WRL::ComPtr<ID3D12Fence> ComputeFence;
extern UINT64 FenceValues[DX::FramesCount];

extern D3D12_FEATURE_DATA_ROOT_SIGNATURE RSFeatureData;

void GetHardwareAdapter(
	IDXGIFactory1* pFactory,
	IDXGIAdapter1** ppAdapter,
	bool requestHighPerformanceAdapter = true);

void CreateDevice();
void CreateCommandAllocators();
void CreateCommandQueues();
void CreateCommandLists();
void CreateSyncObjects();

}