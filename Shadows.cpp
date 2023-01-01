#include "Shadows.h"

#include "DXSample.h"
#include "DXSampleHelper.h"
#include "Scene.h"
#include "DescriptorManager.h"
#include "Utils.h"
#include "DX.h"
#include "imgui/imgui.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

ShadowsResources ShadowsResources::Shadows;
const float ShadowsResources::ShadowMinDistance = 200.0f;

struct ShadowsConstantBuffer
{
	XMFLOAT4X4 VP;
	float pad[48];
};
static_assert(
	(sizeof(ShadowsConstantBuffer) % 256) == 0,
	"Constant Buffer size must be 256-byte aligned");

void ShadowsResources::Initialize()
{
	_cascadesCount = Settings::CascadesCount;

	_createHWRShadowMapResources();
	_createSWRShadowMapResources();
	_createPSO();

	_viewport = CD3DX12_VIEWPORT(
		0.0f,
		0.0f,
		static_cast<float>(Settings::ShadowMapRes),
		static_cast<float>(Settings::ShadowMapRes));
	_scissorRect = CD3DX12_RECT(
		0,
		0,
		static_cast<LONG>(Settings::ShadowMapRes),
		static_cast<LONG>(Settings::ShadowMapRes));

	for (UINT cascade = 0; cascade < _cascadesCount; cascade++)
	{
		_cascadeSplitsNormalized[cascade] = pow(
			4.0f,
			static_cast<float>(cascade + 1)) /
			pow(4.0f, static_cast<float>(_cascadesCount));
	}
}

void ShadowsResources::_createHWRShadowMapResources()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = Settings::ShadowMapRes;
	depthStencilDesc.Height = Settings::ShadowMapRes;
	depthStencilDesc.DepthOrArraySize = _cascadesCount;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optimizedClear = {};
	optimizedClear.Format = DXGI_FORMAT_D32_FLOAT;
	optimizedClear.DepthStencil.Depth = 0.0f;
	optimizedClear.DepthStencil.Stencil = 0;
	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optimizedClear,
			IID_PPV_ARGS(&_shadowMapHWR)));
	NAME_D3D12_OBJECT(_shadowMapHWR);

	D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	DSVDesc.Flags = D3D12_DSV_FLAG_NONE;
	DSVDesc.Texture2DArray.MipSlice = 0;
	DSVDesc.Texture2DArray.ArraySize = 1;
	for (UINT cascade = 0; cascade < _cascadesCount; cascade++)
	{
		DSVDesc.Texture2DArray.FirstArraySlice = cascade;
		DX::Device->CreateDepthStencilView(
			_shadowMapHWR.Get(),
			&DSVDesc,
			Descriptors::DS.GetCPUHandle(CascadeDSV + cascade));
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	SRVDesc.Texture2DArray.MipLevels = -1;
	SRVDesc.Texture2DArray.ArraySize = _cascadesCount;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	DX::Device->CreateShaderResourceView(
		_shadowMapHWR.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(ShadowMapSRV));
}

void ShadowsResources::_createSWRShadowMapResources()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = Settings::ShadowMapRes;
	depthStencilDesc.Height = Settings::ShadowMapRes;
	depthStencilDesc.DepthOrArraySize = _cascadesCount;
	// 0 for max number of mips
	depthStencilDesc.MipLevels = 0;
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
			IID_PPV_ARGS(&_shadowMapSWR)));
	NAME_D3D12_OBJECT(_shadowMapSWR);

	// shadows SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MipLevels = 1;
	SRVDesc.Texture2DArray.ArraySize = Settings::CascadesCount;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	DX::Device->CreateShaderResourceView(
		_shadowMapSWR.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(SWRShadowMapSRV));

	// shadows UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_R32_UINT;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	UAVDesc.Texture2DArray.ArraySize = 1;
	UAVDesc.Texture2DArray.PlaneSlice = 0;

	for (UINT cascade = 0; cascade < Settings::CascadesCount; cascade++)
	{
		SRVDesc.Texture2DArray.FirstArraySlice = cascade;
		SRVDesc.Texture2DArray.ArraySize = 1;

		UAVDesc.Texture2DArray.FirstArraySlice = cascade;
		for (UINT mip = 0; mip < Settings::ShadowMapMipsCount; mip++)
		{
			SRVDesc.Texture2DArray.MostDetailedMip = mip;

			UAVDesc.Texture2DArray.MipSlice = mip;

			DX::Device->CreateUnorderedAccessView(
				_shadowMapSWR.Get(),
				nullptr,
				&UAVDesc,
				Descriptors::SV.GetCPUHandle(
					CascadeMipsUAV +
					cascade * Settings::ShadowMapMipsCount + mip));

			DX::Device->CreateUnorderedAccessView(
				_shadowMapSWR.Get(),
				nullptr,
				&UAVDesc,
				Descriptors::NonSV.GetCPUHandle(
					CascadeMipsUAV +
					cascade * Settings::ShadowMapMipsCount + mip));

			DX::Device->CreateShaderResourceView(
				_shadowMapSWR.Get(),
				&SRVDesc,
				Descriptors::SV.GetCPUHandle(
					CascadeMipsSRV +
					cascade * Settings::ShadowMapMipsCount + mip));
		}
	}
}

void ShadowsResources::_createPSO()
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
		_shadowsRS);
	NAME_D3D12_OBJECT(_shadowsRS);

	ShaderHelper vertexShader;
	ReadDataFromFile(
		Utils::GetAssetFullPath(L"DrawDepthVS.cso").c_str(),
		&vertexShader.data,
		&vertexShader.size);

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
	psoDesc.VS = { vertexShader.data, vertexShader.size };
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = _shadowsRS.Get();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 0;
	psoDesc.SampleDesc.Count = 1;

	ThrowIfFailed(
		DX::Device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&_shadowsPSO)));
	NAME_D3D12_OBJECT(_shadowsPSO);
}

void ShadowsResources::Update()
{
	const AABB& sceneAABB = Scene::CurrentScene->sceneAABB;
	const Camera& camera = Scene::CurrentScene->camera;

	XMVECTOR frustumCornersWS[8];
	for (UINT corner = 0; corner < _countof(frustumCornersWS); corner++)
	{
		frustumCornersWS[corner] = XMLoadFloat4(
			&camera.GetFrustumCornerWS(corner));
	}

	float frustumLookDistance = camera.GetFarZ() - camera.GetNearZ();
	float shadowDistance = min(_shadowDistance, frustumLookDistance);
	// now in [0,1]
	float shadowDistanceNorm = shadowDistance / frustumLookDistance;

	for (UINT cascade = 0; cascade < _cascadesCount; cascade++)
	{
		_cascadeBias[cascade] = _bias;

		// in [0,1]
		float prevSplit = (cascade == 0)
			? 0.0f
			: _cascadeSplitsNormalized[cascade - 1];
		float nextSplit = _cascadeSplitsNormalized[cascade];

		_cascadeSplits[cascade] = nextSplit * shadowDistance;

		XMVECTOR currentSplitCornersWS[8];
		XMVECTOR splitCenter = g_XMZero;
		for (UINT corner = 0; corner < 4; corner++)
		{
			XMVECTOR cornerRay =
				frustumCornersWS[corner + 4] - frustumCornersWS[corner];
			// adjust for max shadow distance
			cornerRay = shadowDistanceNorm * cornerRay;
			currentSplitCornersWS[corner] =
				frustumCornersWS[corner] + cornerRay * prevSplit;
			currentSplitCornersWS[corner + 4] =
				frustumCornersWS[corner] + cornerRay * nextSplit;

			splitCenter += currentSplitCornersWS[corner] * 0.125f;
			splitCenter += currentSplitCornersWS[corner + 4] * 0.125f;
		}

		//float radius = 0.0f;
		//for (UINT corner = 0; corner < 8; corner++)
		//{
		//	radius = max(
		//		XMVectorGetX(
		//			XMVector3Length(currentSplitCornersWS[corner] - splitCenter)),
		//		radius);
		//}

		XMFLOAT3 up = camera.GetRightVectorF();
		const auto& lightDir = Scene::CurrentScene->lightDirection;
		XMFLOAT3 look =
		{
			-lightDir.x,
			-lightDir.y,
			-lightDir.z
		};
		XMFLOAT3 right;

		XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&look));
		XMVECTOR U = XMLoadFloat3(&up);
		XMVECTOR R = XMVector3Normalize(XMVector3Cross(U, L));
		U = XMVector3Cross(L, R);

		XMStoreFloat3(&right, R);
		XMStoreFloat3(&up, U);
		XMStoreFloat3(&look, L);

		XMVECTOR P = splitCenter;

		float x = -XMVectorGetX(XMVector3Dot(P, R));
		float y = -XMVectorGetX(XMVector3Dot(P, U));
		float z = -XMVectorGetX(XMVector3Dot(P, L));

		XMFLOAT4X4 viewF;

		viewF(0, 0) = right.x;
		viewF(1, 0) = right.y;
		viewF(2, 0) = right.z;
		viewF(3, 0) = x;

		viewF(0, 1) = up.x;
		viewF(1, 1) = up.y;
		viewF(2, 1) = up.z;
		viewF(3, 1) = y;

		viewF(0, 2) = look.x;
		viewF(1, 2) = look.y;
		viewF(2, 2) = look.z;
		viewF(3, 2) = z;

		viewF(0, 3) = 0.0f;
		viewF(1, 3) = 0.0f;
		viewF(2, 3) = 0.0f;
		viewF(3, 3) = 1.0f;

		XMMATRIX view = XMLoadFloat4x4(&viewF);

		XMVECTOR sceneCenter = XMLoadFloat3(&sceneAABB.center);
		XMVECTOR sceneExtents = XMLoadFloat3(&sceneAABB.extents);
		XMVECTOR sceneCornersWS[8] =
		{
			sceneCenter + sceneExtents * XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(1.0f, 1.0f, -1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(1.0f, -1.0f, 1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(1.0f, -1.0f, -1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(-1.0f, 1.0f, 1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(-1.0f, 1.0f, -1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(-1.0f, -1.0f, 1.0f, 0.0f),
			sceneCenter + sceneExtents * XMVectorSet(-1.0f, -1.0f, -1.0f, 0.0f)
		};

		XMVECTOR sceneAABBPointsLS[8];
		XMVECTOR tmp;
		XMVECTOR cascadeFrustumMinLS = g_XMFltMax.v;
		XMVECTOR cascadeFrustumMaxLS = -g_XMFltMax.v;
		for (UINT corner = 0; corner < 8; corner++)
		{
			sceneAABBPointsLS[corner] = XMVector3Transform(
				sceneCornersWS[corner],
				view);
			tmp = XMVector3Transform(currentSplitCornersWS[corner], view);
			cascadeFrustumMinLS = XMVectorMin(tmp, cascadeFrustumMinLS);
			cascadeFrustumMaxLS = XMVectorMax(tmp, cascadeFrustumMaxLS);
		}

		//if (_boundCascadesBySpheres)
		//{
		//	cascadeFrustumMinLS =
		//		XMVector3Transform(P, view) -
		//		XMVectorSet(radius, radius, radius, 0.0f);
		//	cascadeFrustumMaxLS =
		//		XMVector3Transform(P, view) +
		//		XMVectorSet(radius, radius, radius, 0.0f);
		//}

		float cascadeNearZ;
		float cascadeFarZ;
		_computeNearAndFar(
			cascadeNearZ,
			cascadeFarZ,
			cascadeFrustumMinLS,
			cascadeFrustumMaxLS,
			sceneAABBPointsLS);

		// far and near are swapped for reversed z matrix
		//
		// from XMMatrixOrthographicOffCenterLH documentation:
		// NearZ and FarZ cannot be the same value and must be greater than 0
		//
		// so, let's adjust view space for that

		if (cascadeNearZ < 0.0f)
		{
			P += -L * (-cascadeNearZ + 1.0f);

			x = -XMVectorGetX(XMVector3Dot(P, R));
			y = -XMVectorGetX(XMVector3Dot(P, U));
			z = -XMVectorGetX(XMVector3Dot(P, L));

			viewF(3, 0) = x;
			viewF(3, 1) = y;
			viewF(3, 2) = z;

			view = XMLoadFloat4x4(&viewF);

			cascadeFarZ += -cascadeNearZ + 1.0f;
			cascadeNearZ = 1.0f;
		}
		XMMATRIX projection = XMMatrixOrthographicOffCenterLH(
			XMVectorGetX(cascadeFrustumMinLS),
			XMVectorGetX(cascadeFrustumMaxLS),
			XMVectorGetY(cascadeFrustumMinLS),
			XMVectorGetY(cascadeFrustumMaxLS),
			cascadeFarZ,
			cascadeNearZ);
		XMStoreFloat4x4(&_cascadeVP[cascade], view * projection);

		// prepare frustum corners
		Frustum& f = _cascadeFrustums[cascade];
		XMStoreFloat4(
			&f.cornersWS[0],
			P + L +
			U * XMVectorGetY(cascadeFrustumMaxLS) +
			R * XMVectorGetX(cascadeFrustumMinLS));
		XMStoreFloat4(
			&f.cornersWS[1],
			P + L +
			U * XMVectorGetY(cascadeFrustumMaxLS) +
			R * XMVectorGetX(cascadeFrustumMaxLS));
		XMStoreFloat4(
			&f.cornersWS[2],
			P + L +
			U * XMVectorGetY(cascadeFrustumMinLS) +
			R * XMVectorGetX(cascadeFrustumMaxLS));
		XMStoreFloat4(
			&f.cornersWS[3],
			P + L +
			U * XMVectorGetY(cascadeFrustumMinLS) +
			R * XMVectorGetX(cascadeFrustumMinLS));
		XMStoreFloat4(
			&f.cornersWS[4],
			P + L * cascadeFarZ +
			U * XMVectorGetY(cascadeFrustumMaxLS) +
			R * XMVectorGetX(cascadeFrustumMinLS));
		XMStoreFloat4(
			&f.cornersWS[5],
			P + L * cascadeFarZ +
			U * XMVectorGetY(cascadeFrustumMaxLS) +
			R * XMVectorGetX(cascadeFrustumMaxLS));
		XMStoreFloat4(
			&f.cornersWS[6],
			P + L * cascadeFarZ +
			U * XMVectorGetY(cascadeFrustumMinLS) +
			R * XMVectorGetX(cascadeFrustumMaxLS));
		XMStoreFloat4(
			&f.cornersWS[7],
			P + L * cascadeFarZ +
			U * XMVectorGetY(cascadeFrustumMinLS) +
			R * XMVectorGetX(cascadeFrustumMinLS));
	}

	_updateFrustumPlanes();
}

void ShadowsResources::GUINewFrame()
{
	int location = Settings::ShadowsGUILocation;
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
	if (ImGui::Begin("Shadow Settings", nullptr, window_flags))
	{
		ImGui::InputFloat3(
			"Direction To Light",
			reinterpret_cast<float*>(&Scene::CurrentScene->lightDirection));

		ImGui::SliderFloat(
			"Shadow Distance",
			&_shadowDistance,
			ShadowMinDistance,
			Settings::CameraFarZ,
			"%.3f",
			ImGuiSliderFlags_AlwaysClamp);

		//ImGui::SliderFloat(
		//	"Shadow Bias",
		//	&_bias,
		//	0.0f,
		//	0.001f,
		//	"%f",
		//	ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Show Cascades", &_showCascades);
		//ImGui::Checkbox("Bound Cascades By Spheres", &_boundCascadesBySpheres);
	}
	ImGui::End();
}

void ShadowsResources::_updateFrustumPlanes()
{
	for (UINT cascade = 0; cascade < _cascadesCount; cascade++)
	{
		Utils::GetFrustumPlanes(
			XMLoadFloat4x4(&_cascadeVP[cascade]),
			_cascadeFrustums[cascade]);

		// reverse Z is used
		std::swap(_cascadeFrustums[cascade].n, _cascadeFrustums[cascade].f);
	}
}

// succeeding code is taken from the CascadesShadowMaps11 DirectX SDK sample
// for more details:
// https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

//--------------------------------------------------------------------------------------
// Used to compute an intersection of the orthographic projection and the Scene AABB
//--------------------------------------------------------------------------------------
struct Triangle
{
	XMVECTOR pt[3];
	bool culled;
};

//--------------------------------------------------------------------------------------
// Computing an accurate near and flar plane will decrease surface acne and Peter-panning.
// Surface acne is the term for erroneous self shadowing.  Peter-panning is the effect where
// shadows disappear near the base of an object.
// As offsets are generally used with PCF filtering due self shadowing issues, computing the
// correct near and far planes becomes even more important.
// This concept is not complicated, but the intersection code is.
//--------------------------------------------------------------------------------------
void ShadowsResources::_computeNearAndFar(
	FLOAT& fNearPlane,
	FLOAT& fFarPlane,
	FXMVECTOR vLightCameraOrthographicMin,
	FXMVECTOR vLightCameraOrthographicMax,
	XMVECTOR* pvPointsInCameraView)
{

	// Initialize the near and far planes
	fNearPlane = FLT_MAX;
	fFarPlane = -FLT_MAX;

	Triangle triangleList[16];
	INT iTriangleCnt = 1;

	triangleList[0].pt[0] = pvPointsInCameraView[0];
	triangleList[0].pt[1] = pvPointsInCameraView[1];
	triangleList[0].pt[2] = pvPointsInCameraView[2];
	triangleList[0].culled = false;

	// These are the indices used to tesselate an AABB into a list of triangles.
	static const INT iAABBTriIndexes[] =
	{
		0,1,2,  1,2,3,
		4,5,6,  5,6,7,
		0,2,4,  2,4,6,
		1,3,5,  3,5,7,
		0,1,4,  1,4,5,
		2,3,6,  3,6,7
	};

	INT iPointPassesCollision[3];

	// At a high level: 
	// 1. Iterate over all 12 triangles of the AABB.  
	// 2. Clip the triangles against each plane. Create new triangles as needed.
	// 3. Find the min and max z values as the near and far plane.

	//This is easier because the triangles are in camera spacing making the collisions tests simple comparisions.

	float fLightCameraOrthographicMinX = XMVectorGetX(vLightCameraOrthographicMin);
	float fLightCameraOrthographicMaxX = XMVectorGetX(vLightCameraOrthographicMax);
	float fLightCameraOrthographicMinY = XMVectorGetY(vLightCameraOrthographicMin);
	float fLightCameraOrthographicMaxY = XMVectorGetY(vLightCameraOrthographicMax);

	for (INT AABBTriIter = 0; AABBTriIter < 12; ++AABBTriIter)
	{

		triangleList[0].pt[0] = pvPointsInCameraView[iAABBTriIndexes[AABBTriIter * 3 + 0]];
		triangleList[0].pt[1] = pvPointsInCameraView[iAABBTriIndexes[AABBTriIter * 3 + 1]];
		triangleList[0].pt[2] = pvPointsInCameraView[iAABBTriIndexes[AABBTriIter * 3 + 2]];
		iTriangleCnt = 1;
		triangleList[0].culled = FALSE;

		// Clip each invidual triangle against the 4 frustums.
		// When ever a triangle is clipped into new triangles,
		// add them to the list.
		for (INT frustumPlaneIter = 0; frustumPlaneIter < 4; ++frustumPlaneIter)
		{

			FLOAT fEdge;
			INT iComponent;

			if (frustumPlaneIter == 0)
			{
				fEdge = fLightCameraOrthographicMinX; // todo make float temp
				iComponent = 0;
			}
			else if (frustumPlaneIter == 1)
			{
				fEdge = fLightCameraOrthographicMaxX;
				iComponent = 0;
			}
			else if (frustumPlaneIter == 2)
			{
				fEdge = fLightCameraOrthographicMinY;
				iComponent = 1;
			}
			else
			{
				fEdge = fLightCameraOrthographicMaxY;
				iComponent = 1;
			}

			for (INT triIter = 0; triIter < iTriangleCnt; ++triIter)
			{
				// We don't delete triangles, so we skip those that have been culled.
				if (!triangleList[triIter].culled)
				{
					INT iInsideVertCount = 0;
					XMVECTOR tempOrder;
					// Test against the correct frustum plane.
					// This could be written more compactly, but it would be harder to understand.

					if (frustumPlaneIter == 0)
					{
						for (INT triPtIter = 0; triPtIter < 3; ++triPtIter)
						{
							if (XMVectorGetX(triangleList[triIter].pt[triPtIter]) >
								XMVectorGetX(vLightCameraOrthographicMin))
							{
								iPointPassesCollision[triPtIter] = 1;
							}
							else
							{
								iPointPassesCollision[triPtIter] = 0;
							}
							iInsideVertCount += iPointPassesCollision[triPtIter];
						}
					}
					else if (frustumPlaneIter == 1)
					{
						for (INT triPtIter = 0; triPtIter < 3; ++triPtIter)
						{
							if (XMVectorGetX(triangleList[triIter].pt[triPtIter]) <
								XMVectorGetX(vLightCameraOrthographicMax))
							{
								iPointPassesCollision[triPtIter] = 1;
							}
							else
							{
								iPointPassesCollision[triPtIter] = 0;
							}
							iInsideVertCount += iPointPassesCollision[triPtIter];
						}
					}
					else if (frustumPlaneIter == 2)
					{
						for (INT triPtIter = 0; triPtIter < 3; ++triPtIter)
						{
							if (XMVectorGetY(triangleList[triIter].pt[triPtIter]) >
								XMVectorGetY(vLightCameraOrthographicMin))
							{
								iPointPassesCollision[triPtIter] = 1;
							}
							else
							{
								iPointPassesCollision[triPtIter] = 0;
							}
							iInsideVertCount += iPointPassesCollision[triPtIter];
						}
					}
					else
					{
						for (INT triPtIter = 0; triPtIter < 3; ++triPtIter)
						{
							if (XMVectorGetY(triangleList[triIter].pt[triPtIter]) <
								XMVectorGetY(vLightCameraOrthographicMax))
							{
								iPointPassesCollision[triPtIter] = 1;
							}
							else
							{
								iPointPassesCollision[triPtIter] = 0;
							}
							iInsideVertCount += iPointPassesCollision[triPtIter];
						}
					}

					// Move the points that pass the frustum test to the begining of the array.
					if (iPointPassesCollision[1] && !iPointPassesCollision[0])
					{
						tempOrder = triangleList[triIter].pt[0];
						triangleList[triIter].pt[0] = triangleList[triIter].pt[1];
						triangleList[triIter].pt[1] = tempOrder;
						iPointPassesCollision[0] = TRUE;
						iPointPassesCollision[1] = FALSE;
					}
					if (iPointPassesCollision[2] && !iPointPassesCollision[1])
					{
						tempOrder = triangleList[triIter].pt[1];
						triangleList[triIter].pt[1] = triangleList[triIter].pt[2];
						triangleList[triIter].pt[2] = tempOrder;
						iPointPassesCollision[1] = TRUE;
						iPointPassesCollision[2] = FALSE;
					}
					if (iPointPassesCollision[1] && !iPointPassesCollision[0])
					{
						tempOrder = triangleList[triIter].pt[0];
						triangleList[triIter].pt[0] = triangleList[triIter].pt[1];
						triangleList[triIter].pt[1] = tempOrder;
						iPointPassesCollision[0] = TRUE;
						iPointPassesCollision[1] = FALSE;
					}

					if (iInsideVertCount == 0)
					{ // All points failed. We're done,
						triangleList[triIter].culled = true;
					}
					else if (iInsideVertCount == 1)
					{// One point passed. Clip the triangle against the Frustum plane
						triangleList[triIter].culled = false;

						// 
						XMVECTOR vVert0ToVert1 = triangleList[triIter].pt[1] - triangleList[triIter].pt[0];
						XMVECTOR vVert0ToVert2 = triangleList[triIter].pt[2] - triangleList[triIter].pt[0];

						// Find the collision ratio.
						FLOAT fHitPointTimeRatio = fEdge - XMVectorGetByIndex(triangleList[triIter].pt[0], iComponent);
						// Calculate the distance along the vector as ratio of the hit ratio to the component.
						FLOAT fDistanceAlongVector01 = fHitPointTimeRatio / XMVectorGetByIndex(vVert0ToVert1, iComponent);
						FLOAT fDistanceAlongVector02 = fHitPointTimeRatio / XMVectorGetByIndex(vVert0ToVert2, iComponent);
						// Add the point plus a percentage of the vector.
						vVert0ToVert1 *= fDistanceAlongVector01;
						vVert0ToVert1 += triangleList[triIter].pt[0];
						vVert0ToVert2 *= fDistanceAlongVector02;
						vVert0ToVert2 += triangleList[triIter].pt[0];

						triangleList[triIter].pt[1] = vVert0ToVert2;
						triangleList[triIter].pt[2] = vVert0ToVert1;

					}
					else if (iInsideVertCount == 2)
					{ // 2 in  // tesselate into 2 triangles


					  // Copy the triangle\(if it exists) after the current triangle out of
					  // the way so we can override it with the new triangle we're inserting.
						triangleList[iTriangleCnt] = triangleList[triIter + 1];

						triangleList[triIter].culled = false;
						triangleList[triIter + 1].culled = false;

						// Get the vector from the outside point into the 2 inside points.
						XMVECTOR vVert2ToVert0 = triangleList[triIter].pt[0] - triangleList[triIter].pt[2];
						XMVECTOR vVert2ToVert1 = triangleList[triIter].pt[1] - triangleList[triIter].pt[2];

						// Get the hit point ratio.
						FLOAT fHitPointTime_2_0 = fEdge - XMVectorGetByIndex(triangleList[triIter].pt[2], iComponent);
						FLOAT fDistanceAlongVector_2_0 = fHitPointTime_2_0 / XMVectorGetByIndex(vVert2ToVert0, iComponent);
						// Calcaulte the new vert by adding the percentage of the vector plus point 2.
						vVert2ToVert0 *= fDistanceAlongVector_2_0;
						vVert2ToVert0 += triangleList[triIter].pt[2];

						// Add a new triangle.
						triangleList[triIter + 1].pt[0] = triangleList[triIter].pt[0];
						triangleList[triIter + 1].pt[1] = triangleList[triIter].pt[1];
						triangleList[triIter + 1].pt[2] = vVert2ToVert0;

						//Get the hit point ratio.
						FLOAT fHitPointTime_2_1 = fEdge - XMVectorGetByIndex(triangleList[triIter].pt[2], iComponent);
						FLOAT fDistanceAlongVector_2_1 = fHitPointTime_2_1 / XMVectorGetByIndex(vVert2ToVert1, iComponent);
						vVert2ToVert1 *= fDistanceAlongVector_2_1;
						vVert2ToVert1 += triangleList[triIter].pt[2];
						triangleList[triIter].pt[0] = triangleList[triIter + 1].pt[1];
						triangleList[triIter].pt[1] = triangleList[triIter + 1].pt[2];
						triangleList[triIter].pt[2] = vVert2ToVert1;
						// Cncrement triangle count and skip the triangle we just inserted.
						++iTriangleCnt;
						++triIter;


					}
					else
					{ // all in
						triangleList[triIter].culled = false;

					}
				}// end if !culled loop
			}
		}
		for (INT index = 0; index < iTriangleCnt; ++index)
		{
			if (!triangleList[index].culled)
			{
				// Set the near and far plan and the min and max z values respectivly.
				for (int vertind = 0; vertind < 3; ++vertind)
				{
					float fTriangleCoordZ = XMVectorGetZ(triangleList[index].pt[vertind]);
					if (fNearPlane > fTriangleCoordZ)
					{
						fNearPlane = fTriangleCoordZ;
					}
					if (fFarPlane < fTriangleCoordZ)
					{
						fFarPlane = fTriangleCoordZ;
					}
				}
			}
		}
	}
}
