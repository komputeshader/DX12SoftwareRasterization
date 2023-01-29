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

StructuredBuffer<MeshMeta> MeshesMeta : register(t0);

// ugly hardcode, but memory is read once for all frustums
StructuredBuffer<uint> CameraInstancesCounters : register(t1);
StructuredBuffer<uint> Cascade0InstancesCounters : register(t2);
StructuredBuffer<uint> Cascade1InstancesCounters : register(t3);
StructuredBuffer<uint> Cascade2InstancesCounters : register(t4);
StructuredBuffer<uint> Cascade3InstancesCounters : register(t5);

AppendStructuredBuffer<IndirectCommand> CameraCommands : register(u0);
AppendStructuredBuffer<IndirectCommand> Cascade0Commands : register(u1);
AppendStructuredBuffer<IndirectCommand> Cascade1Commands : register(u2);
AppendStructuredBuffer<IndirectCommand> Cascade2Commands : register(u3);
AppendStructuredBuffer<IndirectCommand> Cascade3Commands : register(u4);

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

	MeshMeta meshMeta = MeshesMeta[dispatchThreadID.x];

	IndirectCommand result;
	result.startInstanceLocation = meshMeta.startInstanceLocation;
	result.args.indexCountPerInstance = meshMeta.indexCountPerInstance;
	result.args.startIndexLocation = meshMeta.startIndexLocation;
	result.args.baseVertexLocation = meshMeta.baseVertexLocation;
	result.args.startInstanceLocation = 0;

	uint cameraCount = CameraInstancesCounters[dispatchThreadID.x];
	if (cameraCount > 0)
	{
		result.args.instanceCount = cameraCount;
		CameraCommands.Append(result);
	}

	uint cascade0Count = Cascade0InstancesCounters[dispatchThreadID.x];
	if (cascade0Count > 0)
	{
		result.args.instanceCount = cascade0Count;
		Cascade0Commands.Append(result);
	}

	uint cascade1Count = Cascade1InstancesCounters[dispatchThreadID.x];
	if (cascade1Count > 0)
	{
		result.args.instanceCount = cascade1Count;
		Cascade1Commands.Append(result);
	}

	uint cascade2Count = Cascade2InstancesCounters[dispatchThreadID.x];
	if (cascade2Count > 0)
	{
		result.args.instanceCount = cascade2Count;
		Cascade2Commands.Append(result);
	}

	uint cascade3Count = Cascade3InstancesCounters[dispatchThreadID.x];
	if (cascade3Count > 0)
	{
		result.args.instanceCount = cascade3Count;
		Cascade3Commands.Append(result);
	}
}