#pragma once

#include "Types.h"
#include "Settings.h"

namespace Utils
{

extern Microsoft::WRL::ComPtr<ID3D12RootSignature> HiZRS;
extern Microsoft::WRL::ComPtr<ID3D12PipelineState> HiZPSO;
extern D3D12_STATIC_SAMPLER_DESC HiZSamplerDesc;

void InitializeResources();

inline UINT DispatchSize(UINT groupSize, UINT elementsCount)
{
	assert(groupSize != 0 && "DispatchSize : groupSize cannot be 0");

	return (elementsCount + groupSize - 1) / groupSize;
}

AABB MergeAABBs(const AABB& a, const AABB& b);

AABB TransformAABB(
	const AABB& a,
	DirectX::FXMMATRIX m,
	bool ignoreCenter = false);

void GetFrustumPlanes(DirectX::FXMMATRIX m, Frustum& f);

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target);

void CreateDefaultHeapBuffer(
	ID3D12GraphicsCommandList* commandList,
	const void* data,
	UINT64 bufferSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& defaultBuffer,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer,
	D3D12_RESOURCE_STATES endState,
	bool unorderedAccess = false);

void CreateCBResources(
	// CB size is required to be 256-byte aligned.
	UINT64 bufferSize,
	void** data,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

void CreateRS(
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc,
	Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature);

UINT MipsCount(UINT width, UINT height);

void GenerateHiZ(
	ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* resource,
	UINT startSRV,
	UINT startUAV,
	UINT inputWidth,
	UINT inputHeight,
	UINT arraySlice = 0);

inline std::wstring GetAssetFullPath(LPCWSTR assetName)
{
	return Settings::Demo.AssetsPath + assetName;
}

inline UINT AlignForUavCounter(UINT bufferSize)
{
	UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
	return (bufferSize + (alignment - 1)) & ~(alignment - 1);
}

inline UINT AsUINT(float f)
{
	return *reinterpret_cast<UINT*>(&f);
}

class GPUBuffer
{
public:

	void Initialize(
		ID3D12GraphicsCommandList* commandList,
		const void* data,
		UINT elementsCount,
		UINT strideInBytes,
		D3D12_RESOURCE_STATES endState,
		UINT SRVIndex,
		LPCWSTR name);

	GPUBuffer() = default;
	GPUBuffer(const GPUBuffer&) = delete;
	GPUBuffer& operator=(const GPUBuffer&) = delete;

	ID3D12Resource* Get()
	{
		return _buffer.Get();
	}

	D3D12_VERTEX_BUFFER_VIEW& GetVBView()
	{
		assert(_isVB);
		return _VBView;
	}

	D3D12_INDEX_BUFFER_VIEW& GetIBView()
	{
		assert(_isIB);
		return _IBView;
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE GetSRV()
	{
		return _SRV;
	}

private:

	Microsoft::WRL::ComPtr<ID3D12Resource> _buffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bufferUpload;
	D3D12_VERTEX_BUFFER_VIEW _VBView;
	D3D12_INDEX_BUFFER_VIEW _IBView;
	CD3DX12_GPU_DESCRIPTOR_HANDLE _SRV;
	bool _isVB = false;
	bool _isIB = false;
};

};