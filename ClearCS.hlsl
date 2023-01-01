#include "Common.hlsli"

cbuffer CullingCB : register(b0)
{
	uint TotalInstancesCount;
	uint TotalMeshesCount;
	uint CascadesCount;
	uint pad0;
	Frustum Camera;
	Frustum Cascade[MaxCascadesCount];
};

RWStructuredBuffer<uint> CameraInstancesCounters : register(u0);
RWStructuredBuffer<uint> Cascade0InstancesCounters : register(u1);
RWStructuredBuffer<uint> Cascade1InstancesCounters : register(u2);
RWStructuredBuffer<uint> Cascade2InstancesCounters : register(u3);
RWStructuredBuffer<uint> Cascade3InstancesCounters : register(u4);

[numthreads(CullingThreadsX, CullingThreadsY, CullingThreadsZ)]
void main(
	uint3 groupID : SV_GroupID,
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (dispatchThreadID.x >= TotalMeshesCount)
	{
		return;
	}

	CameraInstancesCounters[dispatchThreadID.x] = 0;
	Cascade0InstancesCounters[dispatchThreadID.x] = 0;
	Cascade1InstancesCounters[dispatchThreadID.x] = 0;
	Cascade2InstancesCounters[dispatchThreadID.x] = 0;
	Cascade3InstancesCounters[dispatchThreadID.x] = 0;
}