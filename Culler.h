#pragma once

#include "Settings.h"

class Culler
{
public:

	Culler();
	void Update();
	void Cull(
		ID3D12GraphicsCommandList* commandList,
		Microsoft::WRL::ComPtr<ID3D12Resource>* visibleInstances,
		Microsoft::WRL::ComPtr<ID3D12Resource>* culledCommands,
		Microsoft::WRL::ComPtr<ID3D12Resource>* culledCommandsCounters);

private:

	void _createClearPSO();
	void _createCullingPSO();
	void _createGenerateCommandsPSO();
	void _createCullingCounters();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _clearRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _clearPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _cullingRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _cullingPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _generateHWRCommandsRS;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _generateHWRCommandsPSO;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		_cullingCounters[Settings::FrustumsCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> _culledCommandsCounterReset;

	Microsoft::WRL::ComPtr<ID3D12Resource> _cullingCB;
	UINT8* _cullingCBData;
};