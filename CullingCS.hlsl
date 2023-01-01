#include "Common.hlsli"

struct MeshMeta
{
	AABB aabb;

	uint indexCountPerInstance;
	uint instanceCount;
	uint startIndexLocation;
	int baseVertexLocation;
	uint startInstanceLocation;

	uint pad[3];
};

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
StructuredBuffer<Instance> Instances : register(t1);

// ugly hardcode, but memory is read once for all frustums
RWStructuredBuffer<Instance> CameraVisibleInstances : register(u0);
RWStructuredBuffer<Instance> Cascade0VisibleInstances : register(u1);
RWStructuredBuffer<Instance> Cascade1VisibleInstances : register(u2);
RWStructuredBuffer<Instance> Cascade2VisibleInstances : register(u3);
RWStructuredBuffer<Instance> Cascade3VisibleInstances : register(u4);

RWStructuredBuffer<uint> CameraInstancesCounters : register(u9);
RWStructuredBuffer<uint> Cascade0InstancesCounters : register(u10);
RWStructuredBuffer<uint> Cascade1InstancesCounters : register(u11);
RWStructuredBuffer<uint> Cascade2InstancesCounters : register(u12);
RWStructuredBuffer<uint> Cascade3InstancesCounters : register(u13);

bool FrustumVsAABB(Frustum f, AABB box)
{
	float3 pMax = box.center + box.extents;
	float3 pMin = box.center - box.extents;

	uint sameSideCornersXMin = 0;
	uint sameSideCornersXMax = 0;
	uint sameSideCornersYMin = 0;
	uint sameSideCornersYMax = 0;
	uint sameSideCornersZMin = 0;
	uint sameSideCornersZMax = 0;
	[unroll]
	for (uint i = 0; i < 8; i++)
	{
		sameSideCornersXMin += (f.corners[i].x < pMin.x) ? 1 : 0;
		sameSideCornersXMax += (f.corners[i].x > pMax.x) ? 1 : 0;
		sameSideCornersYMin += (f.corners[i].y < pMin.y) ? 1 : 0;
		sameSideCornersYMax += (f.corners[i].y > pMax.y) ? 1 : 0;
		sameSideCornersZMin += (f.corners[i].z < pMin.z) ? 1 : 0;
		sameSideCornersZMax += (f.corners[i].z > pMax.z) ? 1 : 0;
	}

	return
		!(sameSideCornersXMin == 8
		|| sameSideCornersXMax == 8
		|| sameSideCornersYMin == 8
		|| sameSideCornersYMax == 8
		|| sameSideCornersZMin == 8
		|| sameSideCornersZMax == 8);
}

bool AABBVsPlane(AABB box, float4 plane)
{
	float r = dot(box.extents, abs(plane.xyz));
	float s = dot(plane.xyz, box.center) + plane.w;
	return r + s >= 0;
}

bool AABBVsFrustum(AABB box, Frustum frustum)
{
	bool largeAABBTest = FrustumVsAABB(frustum, box);
	bool l = AABBVsPlane(box, frustum.left);
	bool r = AABBVsPlane(box, frustum.right);
	bool b = AABBVsPlane(box, frustum.bottom);
	bool t = AABBVsPlane(box, frustum.top);
	bool n = AABBVsPlane(box, frustum.near);
	bool f = AABBVsPlane(box, frustum.far);

	return l && r && b && t && n && f && largeAABBTest;
}

[numthreads(CullingThreadsX, CullingThreadsY, CullingThreadsZ)]
void main(
	uint3 groupID : SV_GroupID,
	uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (dispatchThreadID.x >= TotalInstancesCount)
	{
		return;
	}

	Instance instance = Instances[dispatchThreadID.x];
	MeshMeta meshMeta = MeshesMeta[instance.meshID];
	meshMeta.aabb = TransformAABB(meshMeta.aabb, instance.worldTransform);

	uint writeIndex = meshMeta.startInstanceLocation;

	if (AABBVsFrustum(meshMeta.aabb, Camera))
	{
		uint writeOffset;
		InterlockedAdd(
			CameraInstancesCounters[instance.meshID],
			1,
			writeOffset);

		CameraVisibleInstances[writeIndex + writeOffset] = instance;
	}

	if (AABBVsFrustum(meshMeta.aabb, Cascade[0]))
	{
		uint writeOffset;
		InterlockedAdd(
			Cascade0InstancesCounters[instance.meshID],
			1,
			writeOffset);

		Cascade0VisibleInstances[writeIndex + writeOffset] = instance;
	}

	if (AABBVsFrustum(meshMeta.aabb, Cascade[1]))
	{
		uint writeOffset;
		InterlockedAdd(
			Cascade1InstancesCounters[instance.meshID],
			1,
			writeOffset);

		Cascade1VisibleInstances[writeIndex + writeOffset] = instance;
	}

	if (AABBVsFrustum(meshMeta.aabb, Cascade[2]))
	{
		uint writeOffset;
		InterlockedAdd(
			Cascade2InstancesCounters[instance.meshID],
			1,
			writeOffset);

		Cascade2VisibleInstances[writeIndex + writeOffset] = instance;
	}

	if (AABBVsFrustum(meshMeta.aabb, Cascade[3]))
	{
		uint writeOffset;
		InterlockedAdd(
			Cascade3InstancesCounters[instance.meshID],
			1,
			writeOffset);

		Cascade3VisibleInstances[writeIndex + writeOffset] = instance;
	}
}