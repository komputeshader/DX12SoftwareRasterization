#include "Utils.h"
#include "DXSampleHelper.h"
#include "DescriptorManager.h"
#include "DX.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

AABB Utils::MergeAABBs(const AABB& a, const AABB& b)
{
	XMVECTOR ac = XMLoadFloat3(&a.center);
	XMVECTOR ae = XMLoadFloat3(&a.extents);

	XMVECTOR bc = XMLoadFloat3(&b.center);
	XMVECTOR be = XMLoadFloat3(&b.extents);

	XMVECTOR min = XMVectorSubtract(ac, ae);
	min = XMVectorMin(min, XMVectorSubtract(bc, be));

	XMVECTOR max = XMVectorAdd(ac, ae);
	max = XMVectorMax(max, XMVectorAdd(bc, be));

	AABB merged;

	XMStoreFloat3(&merged.center, (min + max) * 0.5f);
	XMStoreFloat3(&merged.extents, (max - min) * 0.5f);

	return merged;
}

AABB Utils::TransformAABB(
	const AABB& a,
	DirectX::FXMMATRIX m,
	bool ignoreCenter)
{
	XMFLOAT4X4 T;
	XMStoreFloat4x4(&T, XMMatrixTranspose(m));

	float ac[3] = { a.center.x, a.center.y, a.center.z };
	if (ignoreCenter)
	{
		ac[0] = 0.0f; ac[1] = 0.0f; ac[2] = 0.0f;
	}

	float ae[3] = { a.extents.x, a.extents.y, a.extents.z };

	float bc[3] = { T.m[0][3], T.m[1][3], T.m[2][3] };
	float be[3] = { 0.0f, 0.0f, 0.0f };

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			bc[i] += T.m[i][j] * ac[j];
			be[i] += abs(T.m[i][j]) * ae[j];
		}
	}

	AABB result;

	result.center = { bc[0], bc[1], bc[2] };
	result.extents = { be[0], be[1], be[2] };

	return result;
}

ComPtr<ID3DBlob> Utils::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors = nullptr;

	hr = D3DCompileFromFile(
		filename.c_str(),
		defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(),
		target.c_str(),
		compileFlags,
		0,
		&byteCode,
		&errors);

	if (errors != nullptr)
	{
		OutputDebugStringA((char*)errors->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	return byteCode;
}

void Utils::CreateDefaultHeapBuffer(
	ID3D12GraphicsCommandList* commandList,
	const void* data,
	UINT64 bufferSize,
	ComPtr<ID3D12Resource>& defaultBuffer,
	ComPtr<ID3D12Resource>& uploadBuffer,
	D3D12_RESOURCE_STATES endState)
{
	{
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
		ThrowIfFailed(
			DX::Device->CreateCommittedResource(
				&prop,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&uploadBuffer)));
	}

	{
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
		ThrowIfFailed(
			DX::Device->CreateCommittedResource(
				&prop,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&defaultBuffer)));
	}

	D3D12_SUBRESOURCE_DATA subresourceData;
	subresourceData.pData = data;
	subresourceData.RowPitch = bufferSize;
	subresourceData.SlicePitch = subresourceData.RowPitch;

	UpdateSubresources(
		commandList,
		defaultBuffer.Get(),
		uploadBuffer.Get(),
		0,
		0,
		1,
		&subresourceData);

	// prevent redundant transition
	if (endState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
			defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			endState);
		commandList->ResourceBarrier(1, &transition);
	}
}

void Utils::CreateCBResources(
	// CB size is required to be 256-byte aligned.
	UINT64 bufferSize,
	void** data,
	ComPtr<ID3D12Resource>& uploadBuffer)
{
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer)));

	// Map and initialize the constant buffer. We don't unmap this until the
	// app closes. Keeping things mapped for the lifetime of the resource is okay.
	// We do not intend to read from this resource on the CPU.
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(
		uploadBuffer->Map(
			0,
			&readRange,
			data));
}

void Utils::CreateRS(
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc,
	ComPtr<ID3D12RootSignature>& rootSignature)
{
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(
		&desc,
		DX::RSFeatureData.HighestVersion,
		&signature,
		&error);
	if (error)
	{
		OutputDebugStringA(
			reinterpret_cast<const char*>(error->GetBufferPointer()));
		error->Release();
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(
		DX::Device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&rootSignature)));
}

// based on
// https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/
void Utils::GetFrustumPlanes(XMMATRIX m, Frustum& f)
{
	XMMATRIX M = XMMatrixTranspose(m);
	XMVECTOR r1 = M.r[0];
	XMVECTOR r2 = M.r[1];
	XMVECTOR r3 = M.r[2];
	XMVECTOR r4 = M.r[3];

	XMStoreFloat4(&f.l, XMPlaneNormalize(XMVectorAdd(r4, r1)));
	XMStoreFloat4(&f.r, XMPlaneNormalize(XMVectorAdd(r4, -r1)));
	XMStoreFloat4(&f.b, XMPlaneNormalize(XMVectorAdd(r4, r2)));
	XMStoreFloat4(&f.t, XMPlaneNormalize(XMVectorAdd(r4, -r2)));
	XMStoreFloat4(&f.n, XMPlaneNormalize(r3));
	// TODO: wtf is with far value?
	XMStoreFloat4(&f.f, XMPlaneNormalize(XMVectorAdd(r4, -r3)));
}

UINT Utils::MipsCount(UINT width, UINT height)
{
	return
		static_cast<UINT>(floorf(log2f(static_cast<float>(
			max(width, height))))) + 1;
}

void Utils::GenerateHiZ(
	ID3D12Resource* resource,
	ID3D12GraphicsCommandList* commandList,
	ID3D12RootSignature* HiZRS,
	ID3D12PipelineState* HiZPSO,
	UINT startSRV,
	UINT startUAV,
	UINT inputWidth,
	UINT inputHeight)
{
	PIXBeginEvent(commandList, 0, L"Generate Hi Z");

	commandList->SetComputeRootSignature(HiZRS);
	commandList->SetPipelineState(HiZPSO);

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	UINT mipsCount = Utils::MipsCount(inputWidth, inputHeight);
	UINT outputWidth;
	UINT outputHeight;
	for (UINT mip = 1; mip < mipsCount; mip++)
	{
		outputWidth = max(inputWidth >> 1, 1);
		outputHeight = max(inputHeight >> 1, 1);

		float NPOTX = (inputWidth % 2)
			? 1.0f - 1.0f / static_cast<float>(inputWidth)
			: 1.0f;
		float NPOTY = (inputHeight % 2)
			? 1.0f - 1.0f / static_cast<float>(inputHeight)
			: 1.0f;

		UINT constants[] =
		{
			outputWidth,
			outputHeight,
			Utils::AsUINT(1.0f / static_cast<float>(outputWidth)),
			Utils::AsUINT(1.0f / static_cast<float>(outputHeight)),
			Utils::AsUINT(NPOTX),
			Utils::AsUINT(NPOTY)
		};

		inputWidth = outputWidth;
		inputHeight = outputHeight;

		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			resource,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12CalcSubresource(mip - 1, 0, 0, mipsCount, 0));
		commandList->ResourceBarrier(1, barriers);

		commandList->SetComputeRoot32BitConstants(
			0,
			_countof(constants),
			constants,
			0);
		commandList->SetComputeRootDescriptorTable(
			1,
			Descriptors::SV.GetGPUHandle(startSRV + mip - 1));
		commandList->SetComputeRootDescriptorTable(
			2,
			Descriptors::SV.GetGPUHandle(startUAV + mip));

		commandList->Dispatch(
			Utils::DispatchSize(Settings::HiZThreadsX, outputWidth),
			Utils::DispatchSize(Settings::HiZThreadsY, outputHeight),
			1);

		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			resource,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12CalcSubresource(mip - 1, 0, 0, mipsCount, 0));
		commandList->ResourceBarrier(1, barriers);
	}

	PIXEndEvent(commandList);
}