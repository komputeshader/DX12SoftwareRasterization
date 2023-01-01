#include "Profiler.h"
#include "DXSampleHelper.h"
#include "DX.h"

FrameStatistics::FrameStatistics()
{
	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	queryHeapDesc.Count = 1;
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
	ThrowIfFailed(
		DX::Device->CreateQueryHeap(
			&queryHeapDesc,
			IID_PPV_ARGS(&_queryHeap)));
	NAME_D3D12_OBJECT(_queryHeap);

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
	ThrowIfFailed(
		DX::Device->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&_queryResult)));
	NAME_D3D12_OBJECT(_queryResult);
}

void FrameStatistics::BeginMeasure(ID3D12GraphicsCommandList* commandList)
{
	commandList->BeginQuery(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
		0);
}

void FrameStatistics::FinishMeasure(ID3D12GraphicsCommandList* commandList)
{
	commandList->EndQuery(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
		0);

	commandList->ResolveQueryData(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
		0,
		1,
		_queryResult.Get(),
		0);

	UINT8* result = nullptr;
	ThrowIfFailed(
		_queryResult->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&result)));
	memcpy(
		&_currentFrameStats,
		result,
		sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
	_queryResult->Unmap(0, nullptr);
}

Profiler::Profiler()
{
	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	// each profile is a start and end timestamps (UINT64s)
	queryHeapDesc.Count = 2 * MaxQueries;
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	ThrowIfFailed(
		DX::Device->CreateQueryHeap(
			&queryHeapDesc,
			IID_PPV_ARGS(&_queryHeap)));
	NAME_D3D12_OBJECT(_queryHeap);

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		2 * MaxQueries * sizeof(UINT64));
	for (UINT frame = 0; frame < DX::FramesCount; frame++)
	{
		ThrowIfFailed(
			DX::Device->CreateCommittedResource(
				&prop,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&_queryResult[frame])));
		NAME_D3D12_OBJECT_INDEXED(_queryResult, frame);
	}

	ZeroMemory(
		_lastFrames,
		FrameCountToAverage * sizeof(float));
}

void Profiler::BeginMeasure(ID3D12GraphicsCommandList* commandList)
{
	Profile& profile = _profiles[_profilesCount];
	profile.commandList = commandList;
	profile.queryIndex = _profilesCount * 2;

	commandList->EndQuery(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		profile.queryIndex);

	_profilesCount++;
}

void Profiler::FinishMeasure(ID3D12GraphicsCommandList* commandList)
{
	UINT cmdListProfileIndex = 0;
	for (UINT i = 0; i < MaxQueries; i++)
	{
		if (_profiles[i].commandList == commandList)
		{
			cmdListProfileIndex = i;
			break;
		}
	}

	UINT queryIndex = _profiles[cmdListProfileIndex].queryIndex;
	commandList->EndQuery(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		queryIndex + 1);

	commandList->ResolveQueryData(
		_queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		queryIndex,
		2,
		_queryResult[DX::FrameIndex].Get(),
		queryIndex * sizeof(UINT64));
}

float Profiler::GetTimeMS(ID3D12CommandQueue* queue)
{
	UINT64* result = nullptr;
	ThrowIfFailed(
		_queryResult[DX::FrameIndex]->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&result)));
	memcpy(
		_time,
		result,
		2 * MaxQueries * sizeof(UINT64));
	_queryResult[DX::FrameIndex]->Unmap(0, nullptr);

	UINT64 GPUFrequency;
	queue->GetTimestampFrequency(&GPUFrequency);
	float frequency = static_cast<float>(GPUFrequency);

	// iterate over command lists to sum up their running times
	// TODO: probably incorrect way to measure frame time due to
	// possible async execution of command lists
	float frameTime = 0.0f;
	for (UINT i = 0; i < _profilesCount; i++)
	{
		float delta = static_cast<float>(_time[2 * i + 1] - _time[2 * i]);
		frameTime += (delta / frequency) * 1000.0f;
	}

	_profilesCount = 0;

	_lastFrames[_lastFramesIndex++] = frameTime;
	if (_lastFramesIndex == FrameCountToAverage)
	{
		_lastFramesIndex = 0;
	}
	float avgTime = 0.0f;
	for (UINT i = 0; i < FrameCountToAverage; i++)
	{
		avgTime += _lastFrames[i];
	}
	avgTime /= static_cast<float>(FrameCountToAverage);

	return avgTime;
}