#include "SoftwareRasterization.h"
#include "DescriptorManager.h"
#include "ForwardRenderer.h"
#include "imgui/imgui.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct SWRDepthSceneCB
{
	XMFLOAT4X4 VP;
	XMFLOAT2 outputRes;
	XMFLOAT2 invOutputRes;
	float bigTriangleThreshold;
	float bigTriangleTileSize;
	float pad[42];
};
static_assert(
	(sizeof(SWRDepthSceneCB) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

struct SWRSceneCB
{
	XMFLOAT4X4 VP;
	XMFLOAT4X4 cascadeVP[Settings::MaxCascadesCount];
	XMFLOAT3 sunDirection;
	UINT cascadesCount;
	XMFLOAT2 outputRes;
	XMFLOAT2 invOutputRes;
	float bigTriangleThreshold;
	float bigTriangleTileSize;
	UINT showCascades;
	UINT pad0[1];
	float cascadeBias[Settings::MaxCascadesCount];
	float cascadeSplits[Settings::MaxCascadesCount];
	UINT pad1[20];
};
static_assert(
	(sizeof(SWRSceneCB) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

struct TriangleCommand
{
	UINT indexCountPerInstance;
	UINT startIndexLocation;
	INT baseVertexLocation;
	UINT startInstanceLocation;
	D3D12_DISPATCH_ARGUMENTS arguments;
};

struct BigTriangle
{
	UINT triangleIndex;
	UINT instanceIndex;
	INT baseVertexLocation;

	float tileOffset;
};

struct BigTrianglesCommand
{
	D3D12_DISPATCH_ARGUMENTS arguments;
};

void SoftwareRasterization::Resize(
	ForwardRenderer* renderer,
	UINT width,
	UINT height)
{
	_renderer = renderer;

	_width = width;
	_height = height;

	_createTriangleDepthPSO();
	_createBigTriangleDepthPSO();
	_createTriangleOpaquePSO();
	_createBigTriangleOpaquePSO();
	_createRenderTargetResources();
	_createDepthBufferResources();

	// depth CBV
	_depthSceneCBFrameSize = sizeof(SWRDepthSceneCB) * Settings::FrustumsCount;
	Utils::CreateCBResources(
		_depthSceneCBFrameSize * DX::FramesCount,
		reinterpret_cast<void**>(&_depthSceneCBData),
		_depthSceneCB);

	//opaque CBV
	Utils::CreateCBResources(
		sizeof(SWRSceneCB) * DX::FramesCount,
		reinterpret_cast<void**>(&_sceneCBData),
		_sceneCB);

	_createBigTrianglesBuffers();
	_createBigTrianglesMDIResources();
	_createCullingMDIResources();
	_createStatsResources();
	_createResetBuffer();
}

void SoftwareRasterization::_createBigTrianglesBuffers()
{
	// TODO:
	//
	// theoretically, we can't have more than width * height
	// triangles actually rendered, since there are only width * height
	// pixels out there
	//
	// in practice, it can be forced with the visibility buffer algorithm,
	// for example
	//
	// current upper bound is unlikely to be reached in practice,
	// but is still just a heuristic, and needs to be fixed
	//
	// anyway, Scene::MaxSceneFacesCount is way too conservative upper bound,
	// and in practice we'll always have smth like O(WH)
	UINT bigTrianglesPerFrame[Settings::FrustumsCount] =
	{
		Settings::BackBufferWidth * Settings::BackBufferHeight
	};
	for (UINT cascade = 1; cascade <= Settings::CascadesCount; cascade++)
	{
		bigTrianglesPerFrame[cascade] =
			Settings::ShadowMapRes * Settings::ShadowMapRes;
	}
	//UINT bigTrianglesPerFrame =
	//	Scene::MaxSceneFacesCount * sizeof(BigTriangle);

	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		_bigTrianglesCounterOffset[frustum] = Utils::AlignForUavCounter(
			bigTrianglesPerFrame[frustum] * sizeof(BigTriangle));

		CD3DX12_RESOURCE_DESC desc =
			CD3DX12_RESOURCE_DESC::Buffer(
				_bigTrianglesCounterOffset[frustum] + sizeof(UINT),
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(
			DX::Device->CreateCommittedResource(
				&prop,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&_bigTriangles[frustum])));
		NAME_D3D12_OBJECT_INDEXED(_bigTriangles, frustum);

		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		UAVDesc.Buffer.FirstElement = 0;
		UAVDesc.Buffer.NumElements = bigTrianglesPerFrame[frustum];
		UAVDesc.Buffer.StructureByteStride = sizeof(BigTriangle);
		UAVDesc.Buffer.CounterOffsetInBytes =
			_bigTrianglesCounterOffset[frustum];
		UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		DX::Device->CreateUnorderedAccessView(
			_bigTriangles[frustum].Get(),
			_bigTriangles[frustum].Get(),
			&UAVDesc,
			Descriptors::SV.GetCPUHandle(BigTrianglesUAV + frustum));

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Shader4ComponentMapping =
			D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = bigTrianglesPerFrame[frustum];
		SRVDesc.Buffer.StructureByteStride = sizeof(BigTriangle);
		SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		DX::Device->CreateShaderResourceView(
			_bigTriangles[frustum].Get(),
			&SRVDesc,
			Descriptors::SV.GetCPUHandle(BigTrianglesSRV + frustum));
	}
}

void SoftwareRasterization::_createStatsResources()
{
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		StatsCount * sizeof(UINT),
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&_trianglesStats)));
	NAME_D3D12_OBJECT(_trianglesStats);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Buffer.NumElements = StatsCount;
	UAVDesc.Buffer.StructureByteStride = sizeof(UINT);
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	DX::Device->CreateUnorderedAccessView(
		_trianglesStats.Get(),
		nullptr,
		&UAVDesc,
		Descriptors::SV.GetCPUHandle(SWRStatsUAV));

	prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	desc = CD3DX12_RESOURCE_DESC::Buffer(StatsCount * sizeof(UINT));
	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		ThrowIfFailed(
			DX::Device->CreateCommittedResource(
				&prop,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&_trianglesStatsReadback[frame])));
		NAME_D3D12_OBJECT(_trianglesStatsReadback[frame]);
	}
}

void SoftwareRasterization::_createBigTrianglesMDIResources()
{
	D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[1] = {};
	argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
	commandSignatureDesc.ByteStride = sizeof(BigTrianglesCommand);

	ThrowIfFailed(
		DX::Device->CreateCommandSignature(
			&commandSignatureDesc,
			nullptr,
			IID_PPV_ARGS(&_bigTrianglesCS)));
	NAME_D3D12_OBJECT(_bigTrianglesCS);

	UINT commandsSizePerFrame = sizeof(BigTrianglesCommand);
	BigTrianglesCommand dispatch;
	dispatch.arguments.ThreadGroupCountX = 1;
	dispatch.arguments.ThreadGroupCountY = 1;
	dispatch.arguments.ThreadGroupCountZ = 1;
	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		Utils::CreateDefaultHeapBuffer(
			DX::CommandList.Get(),
			&dispatch,
			commandsSizePerFrame,
			_bigTrianglesDispatch[frustum],
			_bigTrianglesDispatchUpload[frustum],
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		NAME_D3D12_OBJECT_INDEXED(_bigTrianglesDispatch, frustum);
		NAME_D3D12_OBJECT_INDEXED(_bigTrianglesDispatchUpload, frustum);
	}
}

void SoftwareRasterization::_createCullingMDIResources()
{
	D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
	argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	argumentDescs[0].Constant.RootParameterIndex = RootConstants;
	argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
	argumentDescs[0].Constant.Num32BitValuesToSet = 4;
	argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
	commandSignatureDesc.ByteStride = sizeof(TriangleCommand);

	ThrowIfFailed(
		DX::Device->CreateCommandSignature(
			&commandSignatureDesc,
			_triangleDepthRS.Get(),
			IID_PPV_ARGS(&_triangleDepthCS)));
	NAME_D3D12_OBJECT(_triangleDepthCS);

	ThrowIfFailed(
		DX::Device->CreateCommandSignature(
			&commandSignatureDesc,
			_triangleOpaqueRS.Get(),
			IID_PPV_ARGS(&_triangleOpaqueCS)));
	NAME_D3D12_OBJECT(_triangleOpaqueCS);

	UINT commandsSizePerFrame =
		Scene::MaxSceneMeshesMetaCount * sizeof(TriangleCommand);
	_culledCommandsCounterOffset =
		Utils::AlignForUavCounter(commandsSizePerFrame);
	// Allocate a buffer large enough to hold all of the indirect commands
	// for a single frame as well as a UAV counter.
	CD3DX12_RESOURCE_DESC commandBufferDesc =
		CD3DX12_RESOURCE_DESC::Buffer(
			_culledCommandsCounterOffset + sizeof(UINT),
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Buffer.NumElements = Scene::MaxSceneMeshesMetaCount;
	UAVDesc.Buffer.StructureByteStride = sizeof(TriangleCommand);
	UAVDesc.Buffer.CounterOffsetInBytes = _culledCommandsCounterOffset;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
		{
			ThrowIfFailed(
				DX::Device->CreateCommittedResource(
					&prop,
					D3D12_HEAP_FLAG_NONE,
					&commandBufferDesc,
					D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
					nullptr,
					IID_PPV_ARGS(&_culledCommands[frame][frustum])));
			SetNameIndexed(
				_culledCommands[frame][frustum].Get(),
				L"_culledSWRCommands",
				frame * Settings::FrustumsCount + frustum);

			DX::Device->CreateUnorderedAccessView(
				_culledCommands[frame][frustum].Get(),
				_culledCommands[frame][frustum].Get(),
				&UAVDesc,
				Descriptors::SV.GetCPUHandle(
					SWRCulledCommandsUAV + frustum +
					frame * PerFrameDescriptorsCount));
		}
	}
}

void SoftwareRasterization::Update()
{
	const Camera& camera = Scene::CurrentScene->camera;

	SWRDepthSceneCB depthData = {};
	depthData.VP = camera.GetVP();
	depthData.outputRes =
	{
		static_cast<float>(_width),
		static_cast<float>(_height)
	};
	depthData.invOutputRes =
	{
		1.0f / depthData.outputRes.x,
		1.0f / depthData.outputRes.y
	};
	depthData.bigTriangleThreshold = static_cast<float>(_bigTriangleThreshold);
	depthData.bigTriangleTileSize = static_cast<float>(_bigTriangleTileSize);
	memcpy(
		_depthSceneCBData + DX::FrameIndex * _depthSceneCBFrameSize,
		&depthData,
		sizeof(SWRDepthSceneCB));

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		depthData.VP =
			ShadowsResources::Shadows.GetCascadeVP(cascade);
		depthData.outputRes =
		{
			static_cast<float>(Settings::ShadowMapRes),
			static_cast<float>(Settings::ShadowMapRes)
		};
		depthData.invOutputRes =
		{
			1.0f / depthData.outputRes.x,
			1.0f / depthData.outputRes.y
		};
		memcpy(
			_depthSceneCBData +
			DX::FrameIndex * _depthSceneCBFrameSize +
			(1 + cascade) * sizeof(SWRDepthSceneCB),
			&depthData,
			sizeof(SWRDepthSceneCB));
	}

	SWRSceneCB sceneData = {};
	sceneData.VP = camera.GetVP();
	XMStoreFloat3(
		&sceneData.sunDirection,
		XMVector3Normalize(XMLoadFloat3(
			&Scene::CurrentScene->lightDirection)));
	sceneData.outputRes =
	{
		static_cast<float>(_width),
		static_cast<float>(_height)
	};
	sceneData.invOutputRes =
	{
		1.0f / sceneData.outputRes.x,
		1.0f / sceneData.outputRes.y
	};
	sceneData.cascadesCount = Settings::CascadesCount;
	sceneData.bigTriangleThreshold = static_cast<float>(_bigTriangleThreshold);
	sceneData.bigTriangleTileSize = static_cast<float>(_bigTriangleTileSize);
	sceneData.showCascades = ShadowsResources::Shadows.ShowCascades() ? 1 : 0;
	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		sceneData.cascadeVP[cascade] =
			ShadowsResources::Shadows.GetCascadeVP(cascade);
		sceneData.cascadeBias[cascade] =
			ShadowsResources::Shadows.GetCascadeBias(cascade);
		sceneData.cascadeSplits[cascade] =
			ShadowsResources::Shadows.GetCascadeSplit(cascade);
	}

	memcpy(
		_sceneCBData + DX::FrameIndex * sizeof(SWRSceneCB),
		&sceneData,
		sizeof(SWRSceneCB));
}

void SoftwareRasterization::Draw()
{
	_beginFrame();
	_drawDepth();
	_drawShadows();
	_createBigTrianglesDispatches();
	_drawDepthBigTriangles();
	_drawShadowsBigTriangles();
	_finishDepthsRendering();
	_drawOpaque();
	_endFrame();
}

void SoftwareRasterization::_beginFrame()
{
	CD3DX12_RESOURCE_BARRIER barriers[3 + Settings::FrustumsCount] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_trianglesStats.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_DEST);
	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		barriers[1 + frustum] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[frustum].Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST);
	}
	DX::CommandList->ResourceBarrier(1 + Settings::FrustumsCount, barriers);

	_clearStatistics();
	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		_clearBigTrianglesCounter(frustum);
	}

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_depthBuffer.Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		ShadowsResources::Shadows.GetShadowMapSWR(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
		_trianglesStats.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		barriers[3 + frustum] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[frustum].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	DX::CommandList->ResourceBarrier(3 + Settings::FrustumsCount, barriers);

	UINT clearValue[] = { 0, 0, 0, 0 };
	DX::CommandList->ClearUnorderedAccessViewUint(
		Descriptors::SV.GetGPUHandle(SWRDepthUAV),
		Descriptors::NonSV.GetCPUHandle(SWRDepthUAV),
		_depthBuffer.Get(),
		clearValue,
		0,
		nullptr);

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		DX::CommandList->ClearUnorderedAccessViewUint(
			Descriptors::SV.GetGPUHandle(SWRShadowMapUAV + cascade),
			Descriptors::NonSV.GetCPUHandle(SWRShadowMapUAV + cascade),
			ShadowsResources::Shadows.GetShadowMapSWR(),
			clearValue,
			0,
			nullptr);
	}

	DX::CommandList->ClearUnorderedAccessViewFloat(
		Descriptors::SV.GetGPUHandle(SWRRenderTargetUAV),
		Descriptors::NonSV.GetCPUHandle(SWRRenderTargetUAV),
		_renderTarget.Get(),
		SkyColor,
		0,
		nullptr);
}

void SoftwareRasterization::_drawDepth()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"SWR Depth");

	DX::CommandList->SetComputeRootSignature(_triangleDepthRS.Get());
	DX::CommandList->SetPipelineState(_triangleDepthPSO.Get());
	DX::CommandList->SetComputeRootConstantBufferView(
		SceneCB,
		_depthSceneCB->GetGPUVirtualAddress() +
		DX::FrameIndex * _depthSceneCBFrameSize);
	DX::CommandList->SetComputeRootDescriptorTable(
		2, Scene::CurrentScene->positionsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		3, Scene::CurrentScene->indicesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		4,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		5, Descriptors::SV.GetGPUHandle(PrevFrameDepthSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		6, Descriptors::SV.GetGPUHandle(SWRDepthUAV));
	DX::CommandList->SetComputeRootDescriptorTable(
		7, Descriptors::SV.GetGPUHandle(BigTrianglesUAV));
	DX::CommandList->SetComputeRootDescriptorTable(
		8, Descriptors::SV.GetGPUHandle(SWRStatsUAV));

	if (Settings::CullingEnabled)
	{
		DX::CommandList->ExecuteIndirect(
			_triangleDepthCS.Get(),
			Scene::CurrentScene->meshesMetaCPU.size(),
			_culledCommands[DX::FrameIndex][0].Get(),
			0,
			_culledCommands[DX::FrameIndex][0].Get(),
			_culledCommandsCounterOffset);
	}
	else
	{
		for (const auto& prefab : Scene::CurrentScene->prefabs)
		{
			for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
			{
				const auto& currentMesh =
					Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];

				_drawIndexedInstanced(
					currentMesh.indexCountPerInstance,
					currentMesh.instanceCount,
					currentMesh.startIndexLocation,
					currentMesh.baseVertexLocation,
					currentMesh.startInstanceLocation);
			}
		}
	}

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTrianglesDispatch[0].Get(),
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	PIXEndEvent(DX::CommandList.Get());
}

void SoftwareRasterization::_drawShadows()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"SWR Shadows");

	DX::CommandList->SetComputeRootSignature(_triangleDepthRS.Get());
	DX::CommandList->SetPipelineState(_triangleDepthPSO.Get());
	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	for (UINT cascade = 1; cascade <= Settings::CascadesCount; cascade++)
	{
		DX::CommandList->SetComputeRootConstantBufferView(
			SceneCB,
			_depthSceneCB->GetGPUVirtualAddress() +
			DX::FrameIndex * _depthSceneCBFrameSize +
			cascade * sizeof(SWRDepthSceneCB));
		DX::CommandList->SetComputeRootDescriptorTable(
			2, Scene::CurrentScene->positionsGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			3, Scene::CurrentScene->indicesGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			4,
			Settings::CullingEnabled
			? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + cascade +
				DX::FrameIndex * PerFrameDescriptorsCount)
			: Scene::CurrentScene->instancesGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			5, Descriptors::SV.GetGPUHandle(
				PrevFrameShadowMapSRV + cascade - 1));
		DX::CommandList->SetComputeRootDescriptorTable(
			6, Descriptors::SV.GetGPUHandle(SWRShadowMapUAV + cascade - 1));
		DX::CommandList->SetComputeRootDescriptorTable(
			7, Descriptors::SV.GetGPUHandle(BigTrianglesUAV + cascade));
		DX::CommandList->SetComputeRootDescriptorTable(
			8, Descriptors::SV.GetGPUHandle(SWRStatsUAV));

		if (Settings::CullingEnabled)
		{
			DX::CommandList->ExecuteIndirect(
				_triangleDepthCS.Get(),
				Scene::CurrentScene->meshesMetaCPU.size(),
				_culledCommands[DX::FrameIndex][cascade].Get(),
				0,
				_culledCommands[DX::FrameIndex][cascade].Get(),
				_culledCommandsCounterOffset);
		}
		else
		{
			for (const auto& prefab : Scene::CurrentScene->prefabs)
			{
				for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
				{
					const auto& currentMesh =
						Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];

					_drawIndexedInstanced(
						currentMesh.indexCountPerInstance,
						currentMesh.instanceCount,
						currentMesh.startIndexLocation,
						currentMesh.baseVertexLocation,
						currentMesh.startInstanceLocation);
				}
			}
		}

		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[cascade].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTrianglesDispatch[cascade].Get(),
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
		DX::CommandList->ResourceBarrier(_countof(barriers), barriers);
	}

	PIXEndEvent(DX::CommandList.Get());
}

void SoftwareRasterization::_createBigTrianglesDispatches()
{
	CD3DX12_RESOURCE_BARRIER barriers[2 * Settings::FrustumsCount] = {};
	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		barriers[2 * frustum] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[frustum].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
		barriers[2 * frustum + 1] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTrianglesDispatch[frustum].Get(),
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
	}
	DX::CommandList->ResourceBarrier(2 * Settings::FrustumsCount, barriers);

	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		_createBigTrianglesDispatch(frustum);
	}

	for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
	{
		barriers[2 * frustum] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[frustum].Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
		barriers[2 * frustum + 1] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTrianglesDispatch[frustum].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
	}
	DX::CommandList->ResourceBarrier(2 * Settings::FrustumsCount, barriers);
}

void SoftwareRasterization::_drawDepthBigTriangles()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"SWR Depth Big Triangles");

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTrianglesDispatch[0].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	DX::CommandList->SetComputeRootSignature(_bigTriangleDepthRS.Get());
	DX::CommandList->SetPipelineState(_bigTriangleDepthPSO.Get());
	DX::CommandList->SetComputeRootConstantBufferView(
		SceneCB,
		_depthSceneCB->GetGPUVirtualAddress() +
		DX::FrameIndex * _depthSceneCBFrameSize);
	DX::CommandList->SetComputeRootDescriptorTable(
		1, Scene::CurrentScene->positionsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		2, Scene::CurrentScene->indicesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		3,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		4, Descriptors::SV.GetGPUHandle(BigTrianglesSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		5, Descriptors::SV.GetGPUHandle(SWRDepthUAV));

	DX::CommandList->ExecuteIndirect(
		_bigTrianglesCS.Get(),
		1,
		_bigTrianglesDispatch[0].Get(),
		0,
		nullptr,
		0);

	PIXEndEvent(DX::CommandList.Get());
}

void SoftwareRasterization::_drawShadowsBigTriangles()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"SWR Shadows Big Triangles");

	DX::CommandList->SetComputeRootSignature(_bigTriangleDepthRS.Get());
	DX::CommandList->SetPipelineState(_bigTriangleDepthPSO.Get());
	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	for (UINT cascade = 1; cascade <= Settings::CascadesCount; cascade++)
	{
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTriangles[cascade].Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			_bigTrianglesDispatch[cascade].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
		DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

		DX::CommandList->SetComputeRootConstantBufferView(
			SceneCB,
			_depthSceneCB->GetGPUVirtualAddress() +
			DX::FrameIndex * _depthSceneCBFrameSize +
			cascade * sizeof(SWRDepthSceneCB));
		DX::CommandList->SetComputeRootDescriptorTable(
			1, Scene::CurrentScene->positionsGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			2, Scene::CurrentScene->indicesGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			3,
			Settings::CullingEnabled
			? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + cascade +
				DX::FrameIndex * PerFrameDescriptorsCount)
			: Scene::CurrentScene->instancesGPU.GetSRV());
		DX::CommandList->SetComputeRootDescriptorTable(
			4, Descriptors::SV.GetGPUHandle(BigTrianglesSRV + cascade));
		DX::CommandList->SetComputeRootDescriptorTable(
			5, Descriptors::SV.GetGPUHandle(SWRShadowMapUAV + cascade - 1));

		DX::CommandList->ExecuteIndirect(
			_bigTrianglesCS.Get(),
			1,
			_bigTrianglesDispatch[cascade].Get(),
			0,
			nullptr,
			0);
	}

	PIXEndEvent(DX::CommandList.Get());
}

void SoftwareRasterization::_finishDepthsRendering()
{
	_renderer->PreparePrevFrameDepth(_depthBuffer.Get());

	if (Settings::ShadowsHiZCullingEnabled)
	{
		ShadowsResources::Shadows.PreparePrevFrameShadowMap();
	}
	else
	{
		CD3DX12_RESOURCE_BARRIER barriers[1] = {};
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			ShadowsResources::Shadows.GetShadowMapSWR(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		DX::CommandList->ResourceBarrier(1, barriers);
	}
}

void SoftwareRasterization::_drawOpaque()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"SWR Opaque");

	CD3DX12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COPY_DEST);
	DX::CommandList->ResourceBarrier(1, barriers);

	_clearBigTrianglesCounter(0);

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	DX::CommandList->ResourceBarrier(1, barriers);

	DX::CommandList->SetComputeRootSignature(_triangleOpaqueRS.Get());
	DX::CommandList->SetPipelineState(_triangleOpaquePSO.Get());
	DX::CommandList->SetComputeRootConstantBufferView(
		SceneCB,
		_sceneCB->GetGPUVirtualAddress() +
		DX::FrameIndex * sizeof(SWRSceneCB));
	DX::CommandList->SetComputeRootDescriptorTable(
		2, Scene::CurrentScene->positionsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		3, Scene::CurrentScene->normalsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		4, Scene::CurrentScene->colorsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		5, Scene::CurrentScene->texcoordsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		6, Scene::CurrentScene->indicesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		7,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	// misleading naming, actually, at this point in time, it is
	// current frame depth with Hi-Z mipchain
	DX::CommandList->SetComputeRootDescriptorTable(
		8, Descriptors::SV.GetGPUHandle(PrevFrameDepthSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		9, Descriptors::SV.GetGPUHandle(SWRShadowMapSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		10, Descriptors::SV.GetGPUHandle(SWRRenderTargetUAV));
	DX::CommandList->SetComputeRootDescriptorTable(
		11, Descriptors::SV.GetGPUHandle(BigTrianglesUAV));
	DX::CommandList->SetComputeRootDescriptorTable(
		12, Descriptors::SV.GetGPUHandle(SWRStatsUAV));

	if (Settings::CullingEnabled)
	{
		DX::CommandList->ExecuteIndirect(
			_triangleOpaqueCS.Get(),
			Scene::CurrentScene->meshesMetaCPU.size(),
			_culledCommands[DX::FrameIndex][0].Get(),
			0,
			_culledCommands[DX::FrameIndex][0].Get(),
			_culledCommandsCounterOffset);
	}
	else
	{
		for (const auto& prefab : Scene::CurrentScene->prefabs)
		{
			for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
			{
				const auto& currentMesh =
					Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];

				_drawIndexedInstanced(
					currentMesh.indexCountPerInstance,
					currentMesh.instanceCount,
					currentMesh.startIndexLocation,
					currentMesh.baseVertexLocation,
					currentMesh.startInstanceLocation);
			}
		}
	}

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTrianglesDispatch[0].Get(),
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
		D3D12_RESOURCE_STATE_COPY_DEST);
	DX::CommandList->ResourceBarrier(2, barriers);

	_createBigTrianglesDispatch(0);

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTriangles[0].Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		_bigTrianglesDispatch[0].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	DX::CommandList->ResourceBarrier(2, barriers);

	DX::CommandList->SetComputeRootSignature(_bigTriangleOpaqueRS.Get());
	DX::CommandList->SetPipelineState(_bigTriangleOpaquePSO.Get());
	DX::CommandList->SetComputeRootConstantBufferView(
		SceneCB,
		_sceneCB->GetGPUVirtualAddress() + DX::FrameIndex * sizeof(SWRSceneCB));
	DX::CommandList->SetComputeRootDescriptorTable(
		1, Scene::CurrentScene->positionsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		2, Scene::CurrentScene->normalsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		3, Scene::CurrentScene->colorsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		4, Scene::CurrentScene->texcoordsGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		5, Scene::CurrentScene->indicesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		6,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	DX::CommandList->SetComputeRootDescriptorTable(
		7, Descriptors::SV.GetGPUHandle(BigTrianglesSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		8, Descriptors::SV.GetGPUHandle(SWRDepthSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		9, Descriptors::SV.GetGPUHandle(SWRShadowMapSRV));
	DX::CommandList->SetComputeRootDescriptorTable(
		10, Descriptors::SV.GetGPUHandle(SWRRenderTargetUAV));

	DX::CommandList->ExecuteIndirect(
		_bigTrianglesCS.Get(),
		1,
		_bigTrianglesDispatch[0].Get(),
		0,
		nullptr,
		0);

	PIXEndEvent(DX::CommandList.Get());
}

void SoftwareRasterization::_endFrame()
{
	// collect statistics
	CD3DX12_RESOURCE_BARRIER barriers[1] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_trianglesStats.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	DX::CommandList->ResourceBarrier(1, barriers);

	DX::CommandList->CopyBufferRegion(
		_trianglesStatsReadback[DX::FrameIndex].Get(),
		0,
		_trianglesStats.Get(),
		0,
		StatsCount * sizeof(UINT));

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		_trianglesStats.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	DX::CommandList->ResourceBarrier(1, barriers);

	if (Settings::CullingEnabled)
	{
		CD3DX12_RESOURCE_BARRIER barriers[Settings::FrustumsCount] = {};
		for (UINT frustum = 0; frustum < Settings::FrustumsCount; frustum++)
		{
			barriers[frustum] = CD3DX12_RESOURCE_BARRIER::Transition(
				_culledCommands[DX::FrameIndex][frustum].Get(),
				D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
				D3D12_RESOURCE_STATE_COPY_DEST);
		}
		DX::CommandList->ResourceBarrier(_countof(barriers), barriers);
	}

	UINT* result = nullptr;
	ThrowIfFailed(
		_trianglesStatsReadback[DX::FrameIndex]->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&result)));
	memcpy(
		_statsResult,
		result,
		StatsCount * sizeof(UINT));
	_trianglesStatsReadback[DX::FrameIndex]->Unmap(0, nullptr);
}

void SoftwareRasterization::GUINewFrame()
{
	int location = Settings::SWRGUILocation;
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
	if (ImGui::Begin("Software Rasterization", nullptr, window_flags))
	{
		ImGui::SliderInt(
			"Big Triangle Threshold",
			&_bigTriangleThreshold,
			1,
			1'000,
			"%i",
			ImGuiSliderFlags_AlwaysClamp);

		ImGui::SliderInt(
			"Big Triangle Tile Size",
			&_bigTriangleTileSize,
			64,
			2048,
			"%i",
			ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::End();
}

void SoftwareRasterization::_drawIndexedInstanced(
	UINT indexCountPerInstance,
	UINT instanceCount,
	UINT startIndexLocation,
	INT baseVertexLocation,
	UINT startInstanceLocation)
{
	UINT commandData[] =
	{
		indexCountPerInstance,
		startIndexLocation,
		baseVertexLocation,
		startInstanceLocation
	};
	DX::CommandList->SetComputeRoot32BitConstants(
		RootConstants,
		_countof(commandData),
		commandData,
		0);

	assert(indexCountPerInstance % 3 == 0);
	UINT trianglesCount = indexCountPerInstance / 3;
	DX::CommandList->Dispatch(
		Utils::DispatchSize(
			Settings::SWRTriangleThreadsX,
			trianglesCount),
		instanceCount,
		1);
}

void SoftwareRasterization::_clearStatistics()
{
	DX::CommandList->CopyBufferRegion(
		_trianglesStats.Get(),
		0,
		_counterReset.Get(),
		0,
		StatsCount * sizeof(UINT));
}

void SoftwareRasterization::_clearBigTrianglesCounter(UINT frustum)
{
	DX::CommandList->CopyBufferRegion(
		_bigTriangles[frustum].Get(),
		_bigTrianglesCounterOffset[frustum],
		_counterReset.Get(),
		0,
		sizeof(UINT));
}

void SoftwareRasterization::_createBigTrianglesDispatch(UINT frustum)
{
	// counter is equal to amount of big triangles to rasterize
	// that much groups on X dimension should be dispatched
	DX::CommandList->CopyBufferRegion(
		_bigTrianglesDispatch[frustum].Get(),
		0, // X comes first
		_bigTriangles[frustum].Get(),
		_bigTrianglesCounterOffset[frustum],
		sizeof(UINT));
}

void SoftwareRasterization::_createRenderTargetResources()
{
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Alignment = 0;
	rtDesc.Width = _width;
	rtDesc.Height = _height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = Settings::BackBufferFormat;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.SampleDesc.Quality = 0;
	rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&rtDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&_renderTarget)));
	NAME_D3D12_OBJECT(_renderTarget);

	// render target UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC rtUAV = {};
	rtUAV.Format = Settings::BackBufferFormat;
	rtUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	rtUAV.Texture2D.MipSlice = 0;
	rtUAV.Texture2D.PlaneSlice = 0;

	DX::Device->CreateUnorderedAccessView(
		_renderTarget.Get(),
		nullptr,
		&rtUAV,
		Descriptors::SV.GetCPUHandle(SWRRenderTargetUAV));

	DX::Device->CreateUnorderedAccessView(
		_renderTarget.Get(),
		nullptr,
		&rtUAV,
		Descriptors::NonSV.GetCPUHandle(SWRRenderTargetUAV));
}

void SoftwareRasterization::_createDepthBufferResources()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = _width;
	depthStencilDesc.Height = _height;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
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
			IID_PPV_ARGS(&_depthBuffer)));
	NAME_D3D12_OBJECT(_depthBuffer);

	// depth UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC depthUAV = {};
	depthUAV.Format = DXGI_FORMAT_R32_UINT;
	depthUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	depthUAV.Texture2D.PlaneSlice = 0;
	depthUAV.Texture2D.MipSlice = 0;

	DX::Device->CreateUnorderedAccessView(
		_depthBuffer.Get(),
		nullptr,
		&depthUAV,
		Descriptors::SV.GetCPUHandle(SWRDepthUAV));

	DX::Device->CreateUnorderedAccessView(
		_depthBuffer.Get(),
		nullptr,
		&depthUAV,
		Descriptors::NonSV.GetCPUHandle(SWRDepthUAV));

	// depth SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSRV = {};
	depthSRV.Format = DXGI_FORMAT_R32_FLOAT;
	depthSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSRV.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSRV.Texture2D.MostDetailedMip = 0;
	depthSRV.Texture2D.MipLevels = 1;
	depthSRV.Texture2D.PlaneSlice = 0;
	depthSRV.Texture2D.ResourceMinLODClamp = 0.0f;

	DX::Device->CreateShaderResourceView(
		_depthBuffer.Get(),
		&depthSRV,
		Descriptors::SV.GetCPUHandle(SWRDepthSRV));
}

void SoftwareRasterization::_createResetBuffer()
{
	// allocate a buffer that can be used to reset the UAV counters and
	// initialize it to 0
	// is used also for clearing statistics buffer,
	// so the size is bigger than 1
	UINT bufferSize = StatsCount * sizeof(UINT);
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_counterReset)));
	NAME_D3D12_OBJECT(_counterReset);

	UINT8* mappedCounterReset = nullptr;
	// We do not intend to read from this resource on the CPU.
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(
		_counterReset->Map(
			0,
			&readRange,
			reinterpret_cast<void**>(&mappedCounterReset)));
	ZeroMemory(mappedCounterReset, bufferSize);
	_counterReset->Unmap(0, nullptr);
}

void SoftwareRasterization::_createTriangleDepthPSO()
{
	CD3DX12_ROOT_PARAMETER1 computeRootParameters[9] = {};
	computeRootParameters[SceneCB].InitAsConstantBufferView(0);
	computeRootParameters[RootConstants].InitAsConstants(4, 1);
	CD3DX12_DESCRIPTOR_RANGE1 ranges[7] = {};
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		0);
	computeRootParameters[2].InitAsDescriptorTable(1, &ranges[0]);
	ranges[1].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		8);
	computeRootParameters[3].InitAsDescriptorTable(1, &ranges[1]);
	ranges[2].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		9);
	computeRootParameters[4].InitAsDescriptorTable(1, &ranges[2]);
	ranges[3].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		10);
	computeRootParameters[5].InitAsDescriptorTable(1, &ranges[3]);
	ranges[4].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		0);
	computeRootParameters[6].InitAsDescriptorTable(1, &ranges[4]);
	ranges[5].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		1);
	computeRootParameters[7].InitAsDescriptorTable(1, &ranges[5]);
	ranges[6].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		2);
	computeRootParameters[8].InitAsDescriptorTable(1, &ranges[6]);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
	computeRootSignatureDesc.Init_1_1(
		_countof(computeRootParameters),
		computeRootParameters,
		1,
		&Utils::HiZSamplerDesc);

	Utils::CreateRS(
		computeRootSignatureDesc,
		_triangleDepthRS);
	NAME_D3D12_OBJECT(_triangleDepthRS);

	ShaderHelper computeShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"TriangleDepthCS.cso").c_str(),
		&computeShader.data,
		&computeShader.size);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = _triangleDepthRS.Get();
	psoDesc.CS = { computeShader.data, computeShader.size };

	ThrowIfFailed(
		DX::Device->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_triangleDepthPSO)));
	NAME_D3D12_OBJECT(_triangleDepthPSO);
}

void SoftwareRasterization::_createBigTriangleDepthPSO()
{
	CD3DX12_ROOT_PARAMETER1 computeRootParameters[6] = {};
	computeRootParameters[SceneCB].InitAsConstantBufferView(0);
	CD3DX12_DESCRIPTOR_RANGE1 ranges[5] = {};
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		0);
	computeRootParameters[1].InitAsDescriptorTable(1, &ranges[0]);
	ranges[1].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		8);
	computeRootParameters[2].InitAsDescriptorTable(1, &ranges[1]);
	ranges[2].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		9);
	computeRootParameters[3].InitAsDescriptorTable(1, &ranges[2]);
	ranges[3].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		10);
	computeRootParameters[4].InitAsDescriptorTable(1, &ranges[3]);
	ranges[4].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		0);
	computeRootParameters[5].InitAsDescriptorTable(1, &ranges[4]);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
	computeRootSignatureDesc.Init_1_1(
		_countof(computeRootParameters),
		computeRootParameters);

	Utils::CreateRS(
		computeRootSignatureDesc,
		_bigTriangleDepthRS);
	NAME_D3D12_OBJECT(_bigTriangleDepthRS);

	ShaderHelper computeShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"BigTriangleDepthCS.cso").c_str(),
		&computeShader.data,
		&computeShader.size);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = _bigTriangleDepthRS.Get();
	psoDesc.CS = { computeShader.data, computeShader.size };

	ThrowIfFailed(
		DX::Device->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_bigTriangleDepthPSO)));
	NAME_D3D12_OBJECT(_bigTriangleDepthPSO);
}

void SoftwareRasterization::_createTriangleOpaquePSO()
{
	CD3DX12_ROOT_PARAMETER1 computeRootParameters[13] = {};
	computeRootParameters[SceneCB].InitAsConstantBufferView(0);
	computeRootParameters[RootConstants].InitAsConstants(4, 1);
	CD3DX12_DESCRIPTOR_RANGE1 ranges[11] = {};
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		0);
	computeRootParameters[2].InitAsDescriptorTable(1, &ranges[0]);
	ranges[1].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		1);
	computeRootParameters[3].InitAsDescriptorTable(1, &ranges[1]);
	ranges[2].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		2);
	computeRootParameters[4].InitAsDescriptorTable(1, &ranges[2]);
	ranges[3].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		3);
	computeRootParameters[5].InitAsDescriptorTable(1, &ranges[3]);
	ranges[4].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		8);
	computeRootParameters[6].InitAsDescriptorTable(1, &ranges[4]);
	ranges[5].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		9);
	computeRootParameters[7].InitAsDescriptorTable(1, &ranges[5]);
	ranges[6].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		10);
	computeRootParameters[8].InitAsDescriptorTable(1, &ranges[6]);
	ranges[7].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		11);
	computeRootParameters[9].InitAsDescriptorTable(1, &ranges[7]);
	ranges[8].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		0);
	computeRootParameters[10].InitAsDescriptorTable(1, &ranges[8]);
	ranges[9].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		1);
	computeRootParameters[11].InitAsDescriptorTable(1, &ranges[9]);
	ranges[10].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		2);
	computeRootParameters[12].InitAsDescriptorTable(1, &ranges[10]);

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	D3D12_STATIC_SAMPLER_DESC* pointClampSampler = &samplers[0];
	pointClampSampler->Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	pointClampSampler->AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler->AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler->AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler->MipLODBias = 0;
	pointClampSampler->MaxAnisotropy = 0;
	pointClampSampler->ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	pointClampSampler->BorderColor =
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	pointClampSampler->MinLOD = 0.0f;
	pointClampSampler->MaxLOD = D3D12_FLOAT32_MAX;
	pointClampSampler->ShaderRegister = 0;
	pointClampSampler->RegisterSpace = 0;
	pointClampSampler->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_STATIC_SAMPLER_DESC* depthSampler = &samplers[1];
	samplers[1] = samplers[0];
	depthSampler->Filter = Utils::HiZSamplerDesc.Filter;
	depthSampler->AddressU = Utils::HiZSamplerDesc.AddressU;
	depthSampler->AddressV = Utils::HiZSamplerDesc.AddressV;
	depthSampler->AddressW = Utils::HiZSamplerDesc.AddressW;
	depthSampler->BorderColor = Utils::HiZSamplerDesc.BorderColor;
	depthSampler->ShaderRegister = 1;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
	computeRootSignatureDesc.Init_1_1(
		_countof(computeRootParameters),
		computeRootParameters,
		_countof(samplers),
		samplers);

	Utils::CreateRS(
		computeRootSignatureDesc,
		_triangleOpaqueRS);
	NAME_D3D12_OBJECT(_triangleOpaqueRS);

	ShaderHelper computeShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"TriangleOpaqueCS.cso").c_str(),
		&computeShader.data,
		&computeShader.size);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = _triangleOpaqueRS.Get();
	psoDesc.CS = { computeShader.data, computeShader.size };

	ThrowIfFailed(
		DX::Device->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_triangleOpaquePSO)));
	NAME_D3D12_OBJECT(_triangleOpaquePSO);
}

void SoftwareRasterization::_createBigTriangleOpaquePSO()
{
	CD3DX12_ROOT_PARAMETER1 computeRootParameters[11] = {};
	computeRootParameters[SceneCB].InitAsConstantBufferView(0);
	CD3DX12_DESCRIPTOR_RANGE1 ranges[10] = {};
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		0);
	computeRootParameters[1].InitAsDescriptorTable(1, &ranges[0]);
	ranges[1].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		1);
	computeRootParameters[2].InitAsDescriptorTable(1, &ranges[1]);
	ranges[2].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		2);
	computeRootParameters[3].InitAsDescriptorTable(1, &ranges[2]);
	ranges[3].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		3);
	computeRootParameters[4].InitAsDescriptorTable(1, &ranges[3]);
	ranges[4].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		8);
	computeRootParameters[5].InitAsDescriptorTable(1, &ranges[4]);
	ranges[5].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		9);
	computeRootParameters[6].InitAsDescriptorTable(1, &ranges[5]);
	ranges[6].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		10);
	computeRootParameters[7].InitAsDescriptorTable(1, &ranges[6]);
	ranges[7].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		11);
	computeRootParameters[8].InitAsDescriptorTable(1, &ranges[7]);
	ranges[8].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		12);
	computeRootParameters[9].InitAsDescriptorTable(1, &ranges[8]);
	ranges[9].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		1,
		0);
	computeRootParameters[10].InitAsDescriptorTable(1, &ranges[9]);

	D3D12_STATIC_SAMPLER_DESC pointClampSampler = {};
	pointClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	pointClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.MipLODBias = 0;
	pointClampSampler.MaxAnisotropy = 0;
	pointClampSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	pointClampSampler.BorderColor =
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	pointClampSampler.MinLOD = 0.0f;
	pointClampSampler.MaxLOD = D3D12_FLOAT32_MAX;
	pointClampSampler.ShaderRegister = 0;
	pointClampSampler.RegisterSpace = 0;
	pointClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
	computeRootSignatureDesc.Init_1_1(
		_countof(computeRootParameters),
		computeRootParameters,
		1,
		&pointClampSampler);

	Utils::CreateRS(
		computeRootSignatureDesc,
		_bigTriangleOpaqueRS);
	NAME_D3D12_OBJECT(_bigTriangleOpaqueRS);

	ShaderHelper computeShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"BigTriangleOpaqueCS.cso").c_str(),
		&computeShader.data,
		&computeShader.size);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = _bigTriangleOpaqueRS.Get();
	psoDesc.CS = { computeShader.data, computeShader.size };

	ThrowIfFailed(
		DX::Device->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_bigTriangleOpaquePSO)));
	NAME_D3D12_OBJECT(_bigTriangleOpaquePSO);
}