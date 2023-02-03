#include "HardwareRasterization.h"
#include "DescriptorManager.h"
#include "ForwardRenderer.h"
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
	UINT showMeshlets;
	float pad[26];
};
static_assert(
	(sizeof(SceneCB) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

void HardwareRasterization::Resize(
	ForwardRenderer* renderer,
	UINT width,
	UINT height)
{
	_renderer = renderer;

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
}

void HardwareRasterization::Update()
{
	Camera& camera = Scene::CurrentScene->camera;

	DepthSceneCB depthSceneCB = {};
	depthSceneCB.VP = camera.GetVP();
	memcpy(
		_depthSceneCBData + DX::FrameIndex * _depthSceneCBFrameSize,
		&depthSceneCB,
		sizeof(DepthSceneCB));

	SceneCB sceneCB = {};
	sceneCB.VP = camera.GetVP();
	sceneCB.cascadesCount = Settings::CascadesCount;
	sceneCB.showCascades = ShadowsResources::Shadows.ShowCascades() ? 1 : 0;
	sceneCB.showMeshlets = Settings::ShowMeshlets ? 1 : 0;
	XMStoreFloat3(
		&sceneCB.sunDirection,
		XMVector3Normalize(XMLoadFloat3(
			&Scene::CurrentScene->lightDirection)));

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		memcpy(
			_depthSceneCBData + DX::FrameIndex * _depthSceneCBFrameSize +
			(1 + cascade) * sizeof(DepthSceneCB),
			&ShadowsResources::Shadows.GetCascadeVP(cascade),
			sizeof(XMFLOAT4X4));
		sceneCB.cascadeVP[cascade] =
			ShadowsResources::Shadows.GetCascadeVP(cascade);
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

void HardwareRasterization::Draw(ID3D12Resource* renderTarget)
{
	_beginFrame();
	_drawDepth();
	_drawShadows();
	_drawOpaque(renderTarget);
	_endFrame();
}

void HardwareRasterization::_beginFrame()
{
	DX::CommandList->IASetPrimitiveTopology(
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DX::CommandList->IASetIndexBuffer(
		&Scene::CurrentScene->indicesGPU.GetIBView());
}

void HardwareRasterization::_drawDepth()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"Depth Pass");

	DX::CommandList->SetGraphicsRootSignature(_HWRRS.Get());
	DX::CommandList->SetPipelineState(_depthPSO.Get());
	DX::CommandList->SetGraphicsRootConstantBufferView(
		0,
		_depthSceneCB->GetGPUVirtualAddress() +
		DX::FrameIndex * _depthSceneCBFrameSize);
	DX::CommandList->SetGraphicsRootDescriptorTable(
		2,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	DX::CommandList->IASetVertexBuffers(
		0,
		1,
		&Scene::CurrentScene->positionsGPU.GetVBView());
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
			Scene::CurrentScene->meshesMetaCPU.size(),
			_renderer->GetCulledCommands(DX::FrameIndex, 0),
			0,
			_renderer->GetCulledCommandsCounter(DX::FrameIndex, 0),
			0);
	}
	else
	{
		for (const auto& prefab : Scene::CurrentScene->prefabs)
		{
			for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
			{
				const auto& currentMesh =
					Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];
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

	_renderer->PreparePrevFrameDepth(_depthBuffer.Get());

	PIXEndEvent(DX::CommandList.Get());
}

void HardwareRasterization::_drawShadows()
{
	PIXBeginEvent(DX::CommandList.Get(), 0, L"Shadows Pass");

	CD3DX12_RESOURCE_BARRIER barriers[1] = {};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		ShadowsResources::Shadows.GetShadowMapHWR(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DX::CommandList->ResourceBarrier(_countof(barriers), barriers);

	DX::CommandList->SetGraphicsRootSignature(_HWRRS.Get());
	DX::CommandList->SetPipelineState(ShadowsResources::Shadows.GetPSO());
	DX::CommandList->RSSetViewports(
		1, &ShadowsResources::Shadows.GetViewport());
	DX::CommandList->RSSetScissorRects(
		1, &ShadowsResources::Shadows.GetScissorRect());

	for (UINT cascade = 1; cascade <= Settings::CascadesCount; cascade++)
	{
		DX::CommandList->SetGraphicsRootConstantBufferView(
			0,
			_depthSceneCB->GetGPUVirtualAddress() +
			DX::FrameIndex * _depthSceneCBFrameSize +
			cascade * sizeof(DepthSceneCB));
		DX::CommandList->SetGraphicsRootDescriptorTable(
			2,
			Settings::CullingEnabled
			? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV + cascade +
				DX::FrameIndex * PerFrameDescriptorsCount)
			: Scene::CurrentScene->instancesGPU.GetSRV());
		auto shadowMapDSVHandle =
			Descriptors::DS.GetCPUHandle(CascadeDSV + cascade - 1);
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
				Scene::CurrentScene->meshesMetaCPU.size(),
				_renderer->GetCulledCommands(DX::FrameIndex, cascade),
				0,
				_renderer->GetCulledCommandsCounter(DX::FrameIndex, cascade),
				0);
		}
		else
		{
			for (const auto& prefab : Scene::CurrentScene->prefabs)
			{
				for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
				{
					const auto& currentMesh =
						Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];
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

	if (Settings::ShadowsHiZCullingEnabled)
	{
		ShadowsResources::Shadows.PreparePrevFrameShadowMap();
	}
	else
	{
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			ShadowsResources::Shadows.GetShadowMapHWR(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		DX::CommandList->ResourceBarrier(1, barriers);
	}

	PIXEndEvent(DX::CommandList.Get());
}

void HardwareRasterization::_drawOpaque(ID3D12Resource* renderTarget)
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

	DX::CommandList->SetGraphicsRootSignature(_HWRRS.Get());
	DX::CommandList->SetPipelineState(_opaquePSO.Get());
	DX::CommandList->RSSetViewports(1, &_viewport);
	DX::CommandList->RSSetScissorRects(1, &_scissorRect);
	D3D12_VERTEX_BUFFER_VIEW VBVs[] =
	{
		Scene::CurrentScene->positionsGPU.GetVBView(),
		Scene::CurrentScene->normalsGPU.GetVBView(),
		Scene::CurrentScene->colorsGPU.GetVBView(),
		Scene::CurrentScene->texcoordsGPU.GetVBView()
	};
	DX::CommandList->IASetVertexBuffers(0, _countof(VBVs), VBVs);
	DX::CommandList->SetGraphicsRootConstantBufferView(
		0,
		_sceneCB->GetGPUVirtualAddress() + DX::FrameIndex * sizeof(SceneCB));
	DX::CommandList->SetGraphicsRootDescriptorTable(
		2,
		Settings::CullingEnabled
		? Descriptors::SV.GetGPUHandle(VisibleInstancesSRV +
			DX::FrameIndex * PerFrameDescriptorsCount)
		: Scene::CurrentScene->instancesGPU.GetSRV());
	DX::CommandList->SetGraphicsRootDescriptorTable(
		3, Descriptors::SV.GetGPUHandle(HWRShadowMapSRV));
	auto DSVHandle = Descriptors::DS.GetCPUHandle(HWRDepthDSV);
	auto RTVHandle =
		Descriptors::RT.GetCPUHandle(ForwardRendererRTV + DX::FrameIndex);
	DX::CommandList->OMSetRenderTargets(1, &RTVHandle, FALSE, &DSVHandle);
	DX::CommandList->ClearRenderTargetView(RTVHandle, SkyColor, 0, nullptr);

	if (Settings::CullingEnabled)
	{
		DX::CommandList->ExecuteIndirect(
			_commandSignature.Get(),
			Scene::CurrentScene->meshesMetaCPU.size(),
			_renderer->GetCulledCommands(DX::FrameIndex, 0),
			0,
			_renderer->GetCulledCommandsCounter(DX::FrameIndex, 0),
			0);
	}
	else
	{
		for (const auto& prefab : Scene::CurrentScene->prefabs)
		{
			for (UINT mesh = 0; mesh < prefab.meshesCount; mesh++)
			{
				const auto& currentMesh =
					Scene::CurrentScene->meshesMetaCPU[prefab.meshesOffset + mesh];
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
			DXGI_FORMAT_R32_UINT,
			1,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"COLOR",
			0,
			DXGI_FORMAT_R32G32_UINT,
			2,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32_UINT,
			3,
			0,
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