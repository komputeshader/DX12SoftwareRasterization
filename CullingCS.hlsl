#include "Common.hlsli"

cbuffer CullingCB : register(b0)
{
	uint TotalInstancesCount;
	uint TotalMeshesCount;
	uint CascadesCount;
	uint FrustumCullingEnabled;
	uint CameraHiZCullingEnabled;
	uint ShadowsHiZCullingEnabled;
	uint ClusterCullingEnabled;
	uint pad0;
	float2 DepthResolution;
	float2 ShadowMapResolution;
	float3 CameraPosition;
	uint pad1;
	float4 CascadeCameraPosition[MaxCascadesCount];
	Frustum Camera;
	Frustum Cascade[MaxCascadesCount];
	float4x4 PrevFrameCameraVP;
	float4x4 PrevFrameCascadeVP[MaxCascadesCount];
};

StructuredBuffer<MeshMeta> MeshesMeta : register(t0);
StructuredBuffer<Instance> Instances : register(t1);

Texture2D PrevFrameDepth : register(t2);
Texture2D CascadeShadowMap[MaxCascadesCount] : register(t3);

SamplerState DepthSampler : register(s0);

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

bool AABBVsHiZ(
	in AABB box,
	in float4x4 VP,
	in float2 HiZResolution,
	in Texture2D HiZ)
{
	float3 boxCorners[8] =
	{
		box.center + box.extents * float3(1.0, 1.0, 1.0),
		box.center + box.extents * float3(1.0, 1.0, -1.0),
		box.center + box.extents * float3(1.0, -1.0, 1.0),
		box.center + box.extents * float3(1.0, -1.0, -1.0),
		box.center + box.extents * float3(-1.0, 1.0, 1.0),
		box.center + box.extents * float3(-1.0, 1.0, -1.0),
		box.center + box.extents * float3(-1.0, -1.0, 1.0),
		box.center + box.extents * float3(-1.0, -1.0, -1.0)
	};

	float3 minP = FloatMax.xxx;
	float3 maxP = -FloatMax.xxx;
	[unroll]
	for (uint corner = 0; corner < 8; corner++)
	{
		float4 cornerNDC = mul(
			VP,
			float4(boxCorners[corner], 1.0));
		cornerNDC.xyz /= cornerNDC.w;

		minP = min(minP, cornerNDC.xyz);
		maxP = max(maxP, cornerNDC.xyz);
	}
	// NDC -> DX [0,1]
	minP.xy = minP.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	maxP.xy = maxP.xy * float2(0.5, -0.5) + float2(0.5, 0.5);

	float mipLevel =
		ceil(log2(0.5 * max(
			(maxP.x - minP.x) * HiZResolution.x,
			(maxP.y - minP.y) * HiZResolution.y)));
	float tileDepth = HiZ.SampleLevel(
		DepthSampler,
		(minP.xy + maxP.xy) * 0.5,
		mipLevel).r;

	return !(tileDepth > maxP.z);
}

bool BackfacingMeshlet(
	in float3 cameraPosition,
	in float3 coneApex,
	in float3 coneAxis,
	in float coneCutoff)
{
	return dot(
		normalize(coneApex - cameraPosition),
		coneAxis) >= coneCutoff;
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
	meshMeta.coneApex = mul(
		instance.worldTransform,
		float4(meshMeta.coneApex, 1.0)).xyz;

	uint writeIndex = meshMeta.startInstanceLocation;

	bool cameraBackface = BackfacingMeshlet(
		CameraPosition,
		meshMeta.coneApex,
		meshMeta.coneAxis,
		meshMeta.coneCutoff);
	bool cameraFC = AABBVsFrustum(meshMeta.aabb, Camera);
	if ((!cameraBackface || !ClusterCullingEnabled)
		&& (cameraFC || !FrustumCullingEnabled))
	{
		bool cameraHiZC = AABBVsHiZ(
			meshMeta.aabb,
			PrevFrameCameraVP,
			DepthResolution,
			PrevFrameDepth);
		if (cameraHiZC || !CameraHiZCullingEnabled)
		{
			uint writeOffset;
			InterlockedAdd(
				CameraInstancesCounters[instance.meshID],
				1,
				writeOffset);

			CameraVisibleInstances[writeIndex + writeOffset] = instance;
		}
	}

	bool cascade0Backface = BackfacingMeshlet(
		CascadeCameraPosition[0].xyz,
		meshMeta.coneApex,
		meshMeta.coneAxis,
		meshMeta.coneCutoff);
	bool cascade0FC = AABBVsFrustum(meshMeta.aabb, Cascade[0]);
	if ((!cascade0Backface || !ClusterCullingEnabled)
		&& (cascade0FC || !FrustumCullingEnabled))
	{
		bool cascade0HiZC = AABBVsHiZ(
			meshMeta.aabb,
			PrevFrameCascadeVP[0],
			ShadowMapResolution,
			CascadeShadowMap[0]);
		if (cascade0HiZC || !ShadowsHiZCullingEnabled)
		{
			uint writeOffset;
			InterlockedAdd(
				Cascade0InstancesCounters[instance.meshID],
				1,
				writeOffset);

			Cascade0VisibleInstances[writeIndex + writeOffset] = instance;
		}
	}

	bool cascade1Backface = BackfacingMeshlet(
		CascadeCameraPosition[1].xyz,
		meshMeta.coneApex,
		meshMeta.coneAxis,
		meshMeta.coneCutoff);
	bool cascade1FC = AABBVsFrustum(meshMeta.aabb, Cascade[1]);
	if ((!cascade1Backface || !ClusterCullingEnabled)
		&& (cascade1FC || !FrustumCullingEnabled))
	{
		bool cascade1HiZC = AABBVsHiZ(
			meshMeta.aabb,
			PrevFrameCascadeVP[1],
			ShadowMapResolution,
			CascadeShadowMap[1]);
		if (cascade1HiZC || !ShadowsHiZCullingEnabled)
		{
			uint writeOffset;
			InterlockedAdd(
				Cascade1InstancesCounters[instance.meshID],
				1,
				writeOffset);

			Cascade1VisibleInstances[writeIndex + writeOffset] = instance;
		}
	}

	bool cascade2Backface = BackfacingMeshlet(
		CascadeCameraPosition[2].xyz,
		meshMeta.coneApex,
		meshMeta.coneAxis,
		meshMeta.coneCutoff);
	bool cascade2FC = AABBVsFrustum(meshMeta.aabb, Cascade[2]);
	if ((!cascade2Backface || !ClusterCullingEnabled)
		&& (cascade2FC || !FrustumCullingEnabled))
	{
		bool cascade2HiZC = AABBVsHiZ(
			meshMeta.aabb,
			PrevFrameCascadeVP[2],
			ShadowMapResolution,
			CascadeShadowMap[2]);
		if (cascade2HiZC || !ShadowsHiZCullingEnabled)
		{
			uint writeOffset;
			InterlockedAdd(
				Cascade2InstancesCounters[instance.meshID],
				1,
				writeOffset);

			Cascade2VisibleInstances[writeIndex + writeOffset] = instance;
		}
	}

	bool cascade3Backface = BackfacingMeshlet(
		CascadeCameraPosition[3].xyz,
		meshMeta.coneApex,
		meshMeta.coneAxis,
		meshMeta.coneCutoff);
	bool cascade3FC = AABBVsFrustum(meshMeta.aabb, Cascade[3]);
	if ((!cascade3Backface || !ClusterCullingEnabled)
		&& (cascade3FC || !FrustumCullingEnabled))
	{
		bool cascade3HiZC = AABBVsHiZ(
			meshMeta.aabb,
			PrevFrameCascadeVP[3],
			ShadowMapResolution,
			CascadeShadowMap[3]);
		if (cascade3HiZC || !ShadowsHiZCullingEnabled)
		{
			uint writeOffset;
			InterlockedAdd(
				Cascade3InstancesCounters[instance.meshID],
				1,
				writeOffset);

			Cascade3VisibleInstances[writeIndex + writeOffset] = instance;
		}
	}
}