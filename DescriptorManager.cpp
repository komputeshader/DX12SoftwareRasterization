#include "DescriptorManager.h"
#include "DXSampleHelper.h"
#include "DX.h"

Descriptors Descriptors::RT;
Descriptors Descriptors::DS;
Descriptors Descriptors::SV;
Descriptors Descriptors::NonSV;

void Descriptors::Initialize(
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_DESCRIPTOR_HEAP_FLAGS flags,
	UINT capacity)
{
	_capacity = capacity;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = _capacity;
	heapDesc.Flags = flags;
	heapDesc.Type = type;
	ThrowIfFailed(
		DX::Device->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(&_heap)));
	NAME_D3D12_OBJECT(_heap);

	_descriptorSize = DX::Device->GetDescriptorHandleIncrementSize(type);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Descriptors::GetCPUHandle(UINT index)
{
	assert(index < _capacity);
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		_heap->GetCPUDescriptorHandleForHeapStart(),
		index,
		_descriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Descriptors::GetGPUHandle(UINT index)
{
	assert(index < _capacity);
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(
		_heap->GetGPUDescriptorHandleForHeapStart(),
		index,
		_descriptorSize);
}