#include "HardwareRasterization.h"
#include "Win32Application.h"
#include "Scene.h"
#include "DescriptorManager.h"
#include "DXSampleHelper.h"
#include "DX.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct SceneCB
{
	XMFLOAT4X4 VP;
	XMFLOAT4X4 cascadeVP[Settings::MaxCascadesCount];
	XMFLOAT3 sunDirection;
	UINT cascadesCount;
	float cascadeBias[Settings::MaxCascadesCount];
	float cascadeSplits[Settings::MaxCascadesCount];
	UINT showCascades;
	float pad[27];
};
static_assert(
	(sizeof(SceneCB) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

struct IndirectCommand
{
	UINT startInstanceLocation;
	D3D12_DRAW_INDEXED_ARGUMENTS arguments;
};

void HardwareRasterization::Resize(
	UINT width,
	UINT height)
{
	_width = width;
	_height = height;

	_viewport = CD3DX12_VIEWPORT(
		0.0f,
		0.0f,
		static_cast<float>(_width),
		static_cast<float>(_height));
	_scissorRect = CD3DX12_RECT(
		0,
		0,
		static_cast<LONG>(_width),
		static_cast<LONG>(_height));

	_createDepthBufferResources();
	_loadAssets();
}

void HardwareRasterization::_createDepthBufferResources()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = _width;
	depthStencilDesc.Height = _height;
	depthStencilDesc.DepthOrArraySize = 1;
	// 0 for maximum number of mips
	depthStencilDesc.MipLevels = 0;
	depthStencilDesc.Format = _depthFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optimizedClear = {};
	optimizedClear.Format = _depthFormat;
	optimizedClear.DepthStencil.Depth =
		Scene::CurrentScene->camera.ReverseZ() ? 0.0 : 1.0f;
	optimizedClear.DepthStencil.Stencil = 0;
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&optimizedClear,
			IID_PPV_ARGS(&_depthBuffer)));
	NAME_D3D12_OBJECT(_depthBuffer);

	DX::Device->CreateDepthStencilView(
		_depthBuffer.Get(),
		nullptr,
		Descriptors::DS.GetCPUHandle(HWRDepthDSV));
}

void HardwareRasterization::_loadAssets()
{
	_createHWRRS();
	_createDepthPassPSO();
	_createOpaquePassPSO();
	_createMDIStuff();

	// depth pass + cascades
	_depthSceneCBFrameSize = sizeof(DepthSceneCB) * Settings::FrustumsCount;
	Utils::CreateCBResources(
		_depthSceneCBFrameSize * DX::FramesCount,
		reinterpret_cast<void**>(&_depthSceneCBData),
		_depthSceneCB);

	Utils::CreateCBResources(
		sizeof(SceneCB) * DX::FramesCount,
		reinterpret_cast<void**>(&_sceneCBData),
		_sceneCB);
}

void HardwareRasterization::_createMDIStuff()
{
	D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
	argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	argumentDescs[0].Constant.RootParameterIndex = 1;
	argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
	argumentDescs[0].Constant.Num32BitValuesToSet = 1;
	argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
	commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

	ThrowIfFailed(
		DX::Device->CreateCommandSignature(
			&commandSignatureDesc,
			_HWRRS.Get(),
			IID_PPV_ARGS(&_commandSignature)));
	NAME_D3D12_OBJECT(_commandSignature);

	UINT commandsSizePerFrame =
		Scene::MaxSceneMeshesMetaCount * sizeof(IndirectCommand);
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
	UAVDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
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
				L"_culledCommands",
				frame * Settings::FrustumsCount + frustum);

			DX::Device->CreateUnorderedAccessView(
				_culledCommands[frame][frustum].Get(),
				_culledCommands[frame][frustum].Get(),
				&UAVDesc,
				Descriptors::SV.GetCPUHandle(
					CulledCommandsUAV + frustum +
					frame * PerFrameDescriptorsCount));
		}
	}
}

void HardwareRasterization::Update()
{
	Camera& camera = Scene::CurrentScene->camera;

	DepthSceneCB depthSceneCB = {};
	depthSceneCB.VP = camera.GetViewProjectionMatrixF();
	memcpy(
		_depthSceneCBData + DX::FrameIndex * _depthSceneCBFrameSize,
		&depthSceneCB,
		sizeof(DepthSceneCB));

	SceneCB sceneCB = {};
	sceneCB.VP = camera.GetViewProjectionMatrixF();
	sceneCB.cascadesCount = Settings::CascadesCount;
	sceneCB.showCascades = ShadowsResources::Shadows.ShowCascades() ? 1 : 0;
	XMStoreFloat3(
		&sceneCB.sunDirection,
		XMVector3Normalize(XMLoadFloat3(
			&Scene::CurrentScene->lightDirection)));

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		memcpy(
			_depthSceneCBData + DX::FrameIndex * _depthSceneCBFrameSize +
			(1 + cascade) * sizeof(DepthSceneCB),
			&ShadowsResources::Shadows.GetViewProjectionMatrixF(cascade),
			sizeof(XMFLOAT4X4));
		sceneCB.cascadeVP[cascade] =
			ShadowsResources::Shadows.GetViewProjectionMatrixF(cascade);
		sceneCB.cascadeBias[cascade] =
			ShadowsResources::Shadows.GetCascadeBias(cascade);
		sceneCB.cascadeSplits[cascade] =
			ShadowsResources::Shadows.GetCascadeSplit(cascade);
	}

	memcpy(
		_sceneCBData + DX::FrameIndex * sizeof(SceneCB),
		&sceneCB,
		sizeof(SceneCB));
}

void HardwareRasterization::Draw(
	const D3D12_VERTEX_BUFFER_VIEW& depthVertexBufferView,
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
	ID3D12Resource* renderTarget,
	CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle)
{
	_beginFrame(indexBufferView);
	_drawDepth(depthVertexBufferView);
	_drawShadows();
	_drawOpaque(
		vertexBufferView,
		renderTarget,
		RTVHandle);
	_endFrame();
}

void HardwareRasterization::_beginFrame(
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView)
{
	DX::CommandList->IASetPrimitiveTopology(
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DX::CommandList->IASetIndexBuffer(&indexBufferView);
	DX::CommandList->SetGraphicsRootSignature(_HWRRS.Get());
}

void HardwareRasterization::_drawDepth(
	const D3D12_VERTEX_BUFFER_VIEW& depthVertexBufferView)
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"Depth Pass");

	DX::CommandList->SetGraphicsRootConstantBufferView(
		0,
		_depthSceneCB->GetGPUVirtualAddress() +
		DX::FrameIndex * _depthSceneCBFrameSize);
	DX::CommandList->SetGraphicsRootDescriptorTable(
		2,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesSRV);
	DX::CommandList->SetPipelineState(_depthPSO.Get());
	DX::CommandList->IASetVertexBuffers(0, 1, &depthVertexBufferView);
	DX::CommandList->RSSetViewports(1, &_viewport);
	DX::CommandList->RSSetScissorRects(1, &_scissorRect);
	auto DSVHandle = Descriptors::DS.GetCPUHandle(HWRDepthDSV);
	DX::CommandList->OMSetRenderTargets(0, nullptr, FALSE, &DSVHandle);
	DX::CommandList->ClearDepthStencilView(
		DSVHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		Scene::CurrentScene->camera.ReverseZ() ? 0.0f : 1.0f,
		0,
		0,
		nullptr);

	if (Settings::CullingEnabled)
	{
		DX::CommandList->ExecuteIndirect(
			_commandSignature.Get(),
			Scene::CurrentScene->mutualMeshMeta.size(),
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
					Scene::CurrentScene->mutualMeshMeta[prefab.meshesOffset + mesh];
				UINT commandData[] =
				{
					currentMesh.startInstanceLocation
				};
				DX::CommandList->SetGraphicsRoot32BitConstants(
					1,
					_countof(commandData),
					commandData,
					0);
				DX::CommandList->DrawIndexedInstanced(
					currentMesh.indexCountPerInstance,
					currentMesh.instanceCount,
					currentMesh.startIndexLocation,
					currentMesh.baseVertexLocation,
					0);
			}
		}
	}

	PIXEndEvent(DX::CommandList.Get());
}

void HardwareRasterization::_drawShadows()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"Shadows Pass");

	CD3DX12_RESOURCE_BARRIER barriers[1] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		ShadowsResources::Shadows.GetShadowMapResourceHWR(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	DX::CommandList->SetPipelineState(ShadowsResources::Shadows.GetPSO());
	DX::CommandList->RSSetViewports(
		1, &ShadowsResources::Shadows.GetViewport());
	DX::CommandList->RSSetScissorRects(
		1, &ShadowsResources::Shadows.GetScissorRect());

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		DX::CommandList->SetGraphicsRootConstantBufferView(
			0,
			_depthSceneCB->GetGPUVirtualAddress() +
			DX::FrameIndex * _depthSceneCBFrameSize +
			(cascade + 1) * sizeof(DepthSceneCB));
		DX::CommandList->SetGraphicsRootDescriptorTable(
			2,
			Settings::CullingEnabled
			? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + 1 + cascade +
				DX::FrameIndex * PerFrameDescriptorsCount)
			: Scene::CurrentScene->instancesSRV);
		auto shadowMapDSVHandle =
			Descriptors::DS.GetCPUHandle(CascadeDSV + cascade);
		DX::CommandList->OMSetRenderTargets(
			0,
			nullptr,
			FALSE,
			&shadowMapDSVHandle);
		DX::CommandList->ClearDepthStencilView(
			shadowMapDSVHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

		if (Settings::CullingEnabled)
		{
			DX::CommandList->ExecuteIndirect(
				_commandSignature.Get(),
				Scene::CurrentScene->mutualMeshMeta.size(),
				_culledCommands[DX::FrameIndex][1 + cascade].Get(),
				0,
				_culledCommands[DX::FrameIndex][1 + cascade].Get(),
				_culledCommandsCounterOffset);
		}
		else
		{
			for (const auto& prefab : Scene::CurrentScene->prefabs)
			{
				for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
				{
					const auto& currentMesh =
						Scene::CurrentScene->mutualMeshMeta[prefab.meshesOffset + mesh];
					UINT commandData[] =
					{
						currentMesh.startInstanceLocation
					};
					DX::CommandList->SetGraphicsRoot32BitConstants(
						1,
						_countof(commandData),
						commandData,
						0);
					DX::CommandList->DrawIndexedInstanced(
						currentMesh.indexCountPerInstance,
						currentMesh.instanceCount,
						currentMesh.startIndexLocation,
						currentMesh.baseVertexLocation,
						0);
				}
			}
		}
	}

	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		ShadowsResources::Shadows.GetShadowMapResourceHWR(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	PIXEndEvent(DX::CommandList.Get());
}

void HardwareRasterization::_drawOpaque(
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	ID3D12Resource* renderTarget,
	CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle)
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"Opaque Pass");

	CD3DX12_RESOURCE_BARRIER barriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			renderTarget,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET)
	};
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	DX::CommandList->RSSetViewports(1, &_viewport);
	DX::CommandList->RSSetScissorRects(1, &_scissorRect);
	DX::CommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	DX::CommandList->SetPipelineState(_opaquePSO.Get());
	DX::CommandList->SetGraphicsRootConstantBufferView(
		0,
		_sceneCB->GetGPUVirtualAddress() + DX::FrameIndex * sizeof(SceneCB));
	DX::CommandList->SetGraphicsRootDescriptorTable(
		2,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesSRV);
	DX::CommandList->SetGraphicsRootDescriptorTable(
		3, Descriptors::SV.GetGPUHandle(ShadowMapSRV));
	auto DSVHandle = Descriptors::DS.GetCPUHandle(HWRDepthDSV);
	DX::CommandList->OMSetRenderTargets(1, &RTVHandle, FALSE, &DSVHandle);
	DX::CommandList->ClearRenderTargetView(RTVHandle, SkyColor, 0, nullptr);

	if (Settings::CullingEnabled)
	{
		DX::CommandList->ExecuteIndirect(
			_commandSignature.Get(),
			Scene::CurrentScene->mutualMeshMeta.size(),
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
					Scene::CurrentScene->mutualMeshMeta[prefab.meshesOffset + mesh];
				UINT commandData[] =
				{
					currentMesh.startInstanceLocation
				};
				DX::CommandList->SetGraphicsRoot32BitConstants(
					1,
					_countof(commandData),
					commandData,
					0);
				DX::CommandList->DrawIndexedInstanced(
					currentMesh.indexCountPerInstance,
					currentMesh.instanceCount,
					currentMesh.startIndexLocation,
					currentMesh.baseVertexLocation,
					0);
			}
		}
	}

	PIXEndEvent(DX::CommandList.Get());
}

void HardwareRasterization::_endFrame()
{

}

void HardwareRasterization::_createHWRRS()
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[4] = {};
	rootParameters[0].InitAsConstantBufferView(0);
	rootParameters[1].InitAsConstants(1, 1);
	CD3DX12_DESCRIPTOR_RANGE1 ranges[2] = {};
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		0);
	rootParameters[2].InitAsDescriptorTable(
		1,
		&ranges[0],
		D3D12_SHADER_VISIBILITY_VERTEX);
	ranges[1].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,
		1);
	rootParameters[3].InitAsDescriptorTable(
		1,
		&ranges[1],
		D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC pointClampSampler = {};
	pointClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	pointClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	pointClampSampler.MipLODBias = 0;
	pointClampSampler.MaxAnisotropy = 0;
	pointClampSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	pointClampSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	pointClampSampler.MinLOD = 0.0f;
	pointClampSampler.MaxLOD = D3D12_FLOAT32_MAX;
	pointClampSampler.ShaderRegister = 0;
	pointClampSampler.RegisterSpace = 0;
	pointClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(
		_countof(rootParameters),
		rootParameters,
		1,
		&pointClampSampler,
		rootSignatureFlags);

	Utils::CreateRS(
		rootSignatureDesc,
		_HWRRS);
	NAME_D3D12_OBJECT(_HWRRS);
}

void HardwareRasterization::_createDepthPassPSO()
{
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = _HWRRS.Get();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthFunc = Scene::CurrentScene->camera.ReverseZ()
		? D3D12_COMPARISON_FUNC_GREATER
		: D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DSVFormat = _depthFormat;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 0;
	psoDesc.SampleDesc.Count = 1;

	ShaderHelper vertexShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"DrawDepthVS.cso").c_str(),
		&vertexShader.data,
		&vertexShader.size);

	psoDesc.VS = { vertexShader.data, vertexShader.size };
	ThrowIfFailed(
		DX::Device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_depthPSO)));
	NAME_D3D12_OBJECT(_depthPSO);
}

void HardwareRasterization::_createOpaquePassPSO()
{
	ShaderHelper vertexShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"DrawOpaqueVS.cso").c_str(),
		&vertexShader.data,
		&vertexShader.size);

	ShaderHelper pixelShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"DrawOpaquePS.cso").c_str(),
		&pixelShader.data,
		&pixelShader.size);

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"NORMAL",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			16,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"COLOR",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			32,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			48,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = _HWRRS.Get();
	psoDesc.VS = { vertexShader.data, vertexShader.size };
	psoDesc.PS = { pixelShader.data, pixelShader.size };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	// early z pass was made
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DSVFormat = _depthFormat;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = Settings::BackBufferFormat;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(
		DX::Device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_opaquePSO)));
	NAME_D3D12_OBJECT(_opaquePSO);
}