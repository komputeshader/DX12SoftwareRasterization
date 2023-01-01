#pragma once

#include "Types.h"
#include "Settings.h"

class Utils
{
public:

	static DirectX::XMFLOAT4X4 Identity4x4()
	{
		static DirectX::XMFLOAT4X4 I(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);

		return I;
	}

	static inline UINT DispatchSize(UINT groupSize, UINT elementsCount)
	{
		assert(groupSize != 0 && "DispatchSize : groupSize cannot be 0");

		return (elementsCount + groupSize - 1) / groupSize;
	}

	static AABB MergeAABBs(const AABB& a, const AABB& b);

	static AABB TransformAABB(
		const AABB& a,
		DirectX::FXMMATRIX m,
		bool ignoreCenter = false);

	static void GetFrustumPlanes(DirectX::FXMMATRIX m, Frustum& f);

	static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);

	static void CreateDefaultHeapBuffer(
		ID3D12GraphicsCommandList* commandList,
		const void* data,
		UINT64 bufferSize,
		Microsoft::WRL::ComPtr<ID3D12Resource>& defaultBuffer,
		Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer,
		D3D12_RESOURCE_STATES endState);

	static void CreateCBResources(
		// CB size is required to be 256-byte aligned.
		UINT64 bufferSize,
		void** data,
		Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

	static void CreateRS(
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc,
		Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature);

	static UINT MipsCount(UINT width, UINT height);

	static void GenerateHiZ(
		ID3D12Resource* resource,
		ID3D12GraphicsCommandList* commandList,
		ID3D12RootSignature* HiZRS,
		ID3D12PipelineState* HiZPSO,
		UINT startSRV,
		UINT startUAV,
		UINT inputWidth,
		UINT inputHeight);

	static inline std::wstring GetAssetFullPath(LPCWSTR assetName)
	{
		return Settings::Demo.AssetsPath + assetName;
	}

	static inline UINT AlignForUavCounter(UINT bufferSize)
	{
		UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
		return (bufferSize + (alignment - 1)) & ~(alignment - 1);
	}

	static inline UINT AsUINT(float f)
	{
		return *reinterpret_cast<UINT*>(&f);
	}
};