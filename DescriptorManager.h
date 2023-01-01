#pragma once

#include "DX.h"
#include "Types.h"

// these serve as indices into the descriptor heap
// NOTE: root signatures are dependent on that ordering
enum CBVSRVUAVIndices
{
	MeshMetaSRV,
	InstancesSRV = MeshMetaSRV + ScenesCount,
	CullingCountersSRV = InstancesSRV + ScenesCount,
	CullingCountersUAV = CullingCountersSRV + Settings::FrustumsCount,
	GUIFontTextureSRV = CullingCountersUAV + Settings::FrustumsCount,
	ShadowMapSRV,
	DepthVerticesSRV,
	VerticesSRV = DepthVerticesSRV + ScenesCount,
	IndicesSRV = VerticesSRV + ScenesCount,
	SWRDepthSRV = IndicesSRV + ScenesCount,
	SWRDepthMipsSRV,
	SWRDepthMipsUAV = SWRDepthMipsSRV + Settings::Demo.BackBufferMipsCount,
	SWRRenderTargetUAV = SWRDepthMipsUAV +
		Settings::Demo.BackBufferMipsCount,
	SWRShadowMapSRV,
	BigTrianglesSRV,
	BigTrianglesUAV,
	SWRStatsUAV,
	CascadeMipsSRV,
	CascadeMipsUAV = CascadeMipsSRV +
		Settings::CascadesCount * Settings::ShadowMapMipsCount,

	SingleDescriptorsCount = CascadeMipsUAV +
		Settings::CascadesCount * Settings::ShadowMapMipsCount,

	// descriptors for frame resources
	VisibleInstancesSRV = SingleDescriptorsCount,
	VisibleInstancesUAV = VisibleInstancesSRV + Settings::FrustumsCount,
	CulledCommandsUAV = VisibleInstancesUAV + Settings::FrustumsCount,
	SWRCulledCommandsUAV = CulledCommandsUAV + Settings::FrustumsCount,

	PerFrameDescriptorsCount = SWRCulledCommandsUAV + Settings::FrustumsCount - VisibleInstancesSRV,
	CBVUAVSRVCount = SingleDescriptorsCount + PerFrameDescriptorsCount * DX::FramesCount
};

enum RTVIndices
{
	ForwardRendererRTV,
	RTVCount = ForwardRendererRTV + DX::FramesCount
};

enum DSVIndices
{
	HWRDepthDSV,
	CascadeDSV,
	DSVCount = CascadeDSV + Settings::CascadesCount
};

class Descriptors
{
public:

	static Descriptors DS;
	static Descriptors RT;
	// SV stands for Shader Visible
	static Descriptors SV;
	static Descriptors NonSV; 

	void Initialize(
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		UINT capacity);

	Descriptors() = default;
	Descriptors(const Descriptors&) = delete;
	Descriptors& operator=(const Descriptors&) = delete;

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index);
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index);

	ID3D12DescriptorHeap* GetHeap() { return _heap.Get(); }

private:

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
	UINT _descriptorSize = 0;
	UINT _capacity = 0;
};