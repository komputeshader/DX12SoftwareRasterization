#pragma once

#include "Common.h"

#define SCENE_MESHLETIZATION

class Settings
{
public:

	static Settings Demo;

	static const UINT BackBufferWidth = 1920;
	static const UINT BackBufferHeight = 1080;
	static const float BackBufferAspectRatio;
	static const UINT BackBufferMipsCount = 11;

	static const UINT ShadowMapRes = 2048;
	// TODO: eliminate hardcode
	static const UINT ShadowMapMipsCount = 12;
	// should match it's duplicate in shaders
	static const UINT MaxCascadesCount = 8;
	static const UINT CascadesCount = 4;
	// 1 for main camera
	static const UINT FrustumsCount = 1 + CascadesCount;
	static bool CullingEnabled;
	static bool FrustumCullingEnabled;
	static bool CameraHiZCullingEnabled;
	static bool ShadowsHiZCullingEnabled;
	static bool ClusterBackfaceCullingEnabled;
	static bool SWREnabled;
	static bool ShowMeshlets;
	static const float CameraNearZ;
	static const float CameraFarZ;
	static const float GUITransparency;
	static const INT StatsGUILocation = 0;
	static const INT SWRGUILocation = 1;
	static const INT ShadowsGUILocation = 2;

	static const bool UseWarpDevice;

	std::wstring AssetsPath;
	static const DXGI_FORMAT BackBufferFormat =
		DXGI_FORMAT_R16G16B16A16_FLOAT;

	// should match it's duplicates in TypesAndConstants.hlsli
	static const UINT CullingThreadsX = 256;
	static const UINT CullingThreadsY = 1;
	static const UINT CullingThreadsZ = 1;

	static const UINT SWRTriangleThreadsX = 256;
	static const UINT SWRTriangleThreadsY = 1;
	static const UINT SWRTriangleThreadsZ = 1;

	static const UINT HiZThreadsX = 16;
	static const UINT HiZThreadsY = 16;
	static const UINT HiZThreadsZ = 1;

};