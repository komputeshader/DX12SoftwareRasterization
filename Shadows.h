#pragma once

#include "Settings.h"
#include "Types.h"

class ShadowsResources
{
public:

	static ShadowsResources Shadows;

	ShadowsResources() = default;
	ShadowsResources(const ShadowsResources&) = delete;
	ShadowsResources& operator=(const ShadowsResources&) = delete;
	~ShadowsResources() = default;

	void Initialize();
	void Update();
	void PreparePrevFrameShadowMap();

	void GUINewFrame();

	ID3D12Resource* GetShadowMapHWR() { return _shadowMapHWR.Get(); }
	ID3D12Resource* GetShadowMapSWR() { return _shadowMapSWR.Get(); }
	ID3D12PipelineState* GetPSO() { return _shadowsPSO.Get(); }
	const CD3DX12_VIEWPORT& GetViewport() { return _viewport; }
	const CD3DX12_RECT& GetScissorRect() { return _scissorRect; }

	const DirectX::XMFLOAT4X4& GetCascadeVP(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _cascadeVP[cascade];
	}
	const DirectX::XMFLOAT4X4& GetPrevFrameCascadeVP(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _prevFrameCascadeVP[cascade];
	}
	float GetCascadeBias(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _cascadeBias[cascade];
	}
	float GetCascadeSplitNormalized(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _cascadeSplitsNormalized[cascade];
	}
	float GetCascadeSplit(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _cascadeSplits[cascade];
	}
	const Frustum& GetCascadeFrustum(UINT cascade) const
	{
		assert(cascade < Settings::MaxCascadesCount);
		return _cascadeFrustums[cascade];
	}

	bool ShowCascades() const { return _showCascades; }

private:

	static const float ShadowMinDistance;

	void _createHWRShadowMapResources();
	void _createSWRShadowMapResources();
	void _createPrevFrameShadowMapResources();
	void _createPSO();
	void _updateFrustumPlanes();
	void _computeNearAndFar(
		FLOAT& fNearPlane,
		FLOAT& fFarPlane,
		DirectX::FXMVECTOR vLightCameraOrthographicMin,
		DirectX::FXMVECTOR vLightCameraOrthographicMax,
		DirectX::XMVECTOR* pvPointsInCameraView);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _shadowsRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _shadowsPSO;
	// for hardware rasterizer pipeline
	Microsoft::WRL::ComPtr<ID3D12Resource> _shadowMapHWR;
	// SWR needs unordered writes
	Microsoft::WRL::ComPtr<ID3D12Resource> _shadowMapSWR;
	// for Hi-Z culling
	Microsoft::WRL::ComPtr<ID3D12Resource> _prevFrameShadowMap;
	CD3DX12_VIEWPORT _viewport;
	CD3DX12_RECT _scissorRect;

	DirectX::XMFLOAT4X4 _cascadeVP[Settings::MaxCascadesCount];
	DirectX::XMFLOAT4X4 _prevFrameCascadeVP[Settings::MaxCascadesCount];
	Frustum _cascadeFrustums[Settings::MaxCascadesCount];
	float _cascadeBias[Settings::MaxCascadesCount];
	float _cascadeSplitsNormalized[Settings::MaxCascadesCount];
	float _cascadeSplits[Settings::MaxCascadesCount];
	UINT _cascadesCount;
	float _shadowDistance = 5000.0f;
	bool _boundCascadesBySpheres = false;
	float _bias = 0.001f;

	// debug and visualisation stuff
	bool _showCascades = false;
};