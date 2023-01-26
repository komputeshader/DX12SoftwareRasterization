#include "Settings.h"
#include "Utils.h"

Settings Settings::Demo;

// TODO: fix this
//const UINT Settings::BackBufferMipsCount =
//	Utils::MipsCount(BackBufferWidth, BackBufferHeight);
const float Settings::BackBufferAspectRatio =
	static_cast<float>(Settings::BackBufferWidth) /
	static_cast<float>(Settings::BackBufferHeight);
bool Settings::CullingEnabled = true;
bool Settings::FrustumCullingEnabled = true;
// TODO: prone to some temporal artifacts,
// which are easily fixed by two-pass Hi-Z occlusion culling
bool Settings::CameraHiZCullingEnabled = true;
bool Settings::ShadowsHiZCullingEnabled = true;
bool Settings::SWREnabled = false;
bool Settings::ShowMeshlets = false;
const float Settings::CameraNearZ = 0.001f;
const float Settings::CameraFarZ = 10000.0f;
const float Settings::GUITransparency = 0.7f;
const bool Settings::UseWarpDevice = false;