#pragma once

#include "DX.h"
#include "Types.h"

// these serve as indices into the descriptor heap
// NOTE: root signatures are dependent on that ordering
enum CBVSRVUAVIndices
{
	MeshesMetaSRV,
	InstancesSRV = MeshesMetaSRV + ScenesCount,
	CullingCountersSRV = InstancesSRV + ScenesCount,
	CullingCountersUAV = CullingCountersSRV + Settings::FrustumsCount,
	GUIFontTextureSRV = CullingCountersUAV + Settings::FrustumsCount,
	HWRShadowMapSRV,
	VertexPositionsSRV,
	VertexNormalsSRV = VertexPositionsSRV + ScenesCount,
	VertexColorsSRV = VertexNormalsSRV + ScenesCount,
	VertexTexcoordsSRV = VertexColorsSRV + ScenesCount,
	IndicesSRV = VertexTexcoordsSRV + ScenesCount,
	SWRDepthSRV = IndicesSRV + ScenesCount,
	SWRDepthUAV,
	PrevFrameDepthSRV,
	PrevFrameDepthMipsSRV,
	PrevFrameDepthMipsUAV = PrevFrameDepthMipsSRV +
		Settings::BackBufferMipsCount,
	SWRRenderTargetUAV = PrevFrameDepthMipsUAV +
		Settings::BackBufferMipsCount,
	SWRShadowMapSRV,
	SWRShadowMapUAV,
	PrevFrameShadowMapSRV = SWRShadowMapUAV + Settings::CascadesCount,
	PrevFrameShadowMapMipsSRV = PrevFrameShadowMapSRV + Settings::CascadesCount,
	PrevFrameShadowMapMipsUAV = PrevFrameShadowMapMipsSRV +
		Settings::CascadesCount * Settings::ShadowMapMipsCount,
	BigTrianglesSRV = PrevFrameShadowMapMipsUAV +
		Settings::CascadesCount * Settings::ShadowMapMipsCount,
	BigTrianglesUAV,
	SWRStatsUAV,

	SingleDescriptorsCount,

	// descriptors for frame resources
	VisibleInstancesSRV = SingleDescriptorsCount,
	VisibleInstancesUAV = VisibleInstancesSRV + Settings::FrustumsCount,
	CulledCommandsUAV = VisibleInstancesUAV + Settings::FrustumsCount,
	SWRCulledCommandsUAV = CulledCommandsUAV + Settings::FrustumsCount,

	PerFrameDescriptorsCount = SWRCulledCommandsUAV +
		Settings::FrustumsCount - VisibleInstancesSRV,
	CBVUAVSRVCount = SingleDescriptorsCount +
		PerFrameDescriptorsCount * DX::FramesCount
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