#pragma once

#include "Camera.h"
#include "Prefab.h"
#include "Settings.h"
#include "DX.h"

class Scene
{
public:

	static Scene* CurrentScene;
	static Scene PlantScene;
	static Scene BuddhaScene;

	void LoadPlant();
	void LoadBuddha();

	Camera camera;
	float FOV = 90.0f;
	float nearZ = Settings::CameraNearZ;
	float farZ = Settings::CameraFarZ;
	bool FOVChanged = false;
	// to light
	DirectX::XMFLOAT3 lightDirection;

	// mutual for all geometry
	std::vector<VertexPosition> positionsCPU;
	std::vector<VertexNormal> normalsCPU;
	std::vector<VertexColor> colorsCPU;
	std::vector<VertexUV> texcoordsCPU;
	std::vector<UINT> indicesCPU;
	// mesh is a smallest entity with it's own bounding volume
	std::vector<MeshMeta> meshesMetaCPU;
	// unique objects in the scene
	std::vector<Instance> instancesCPU;

	std::vector<Prefab> prefabs;

	static UINT64 MaxSceneFacesCount;
	static UINT64 MaxSceneInstancesCount;
	static UINT64 MaxSceneMeshesMetaCount;

	UINT64 totalFacesCount = 0;
	AABB sceneAABB;

	// GPU Resources

	// de-interleaved vertex attributes
	Utils::GPUBuffer positionsGPU;
	Utils::GPUBuffer normalsGPU;
	Utils::GPUBuffer colorsGPU;
	Utils::GPUBuffer texcoordsGPU;

	Utils::GPUBuffer indicesGPU;
	Utils::GPUBuffer meshesMetaGPU;
	Utils::GPUBuffer instancesGPU;

private:

	void _loadObj(
		const std::string& OBJPath,
		float translation = 0.0f,
		float scale = 1.0f,
		UINT instancesCountX = 1,
		UINT instancesCountZ = 1);

	void _createVBResources(ScenesIndices sceneIndex);
	void _createIBResources(ScenesIndices sceneIndex);
	void _createMeshMetaResources(ScenesIndices sceneIndex);
	void _createInstancesBufferResources(ScenesIndices sceneIndex);
};