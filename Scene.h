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

	std::vector<DepthVertex> mutualDepthVertices;
	std::vector<Vertex> mutualVertices;
	std::vector<UINT> mutualIndices;
	// mesh is a smallest entity with it's own bounding volume
	std::vector<MeshMeta> mutualMeshMeta;
	// unique objects in the scene
	std::vector<Instance> instancesCPU;

	std::vector<Prefab> prefabs;

	static UINT64 MaxSceneFacesCount;
	static UINT64 MaxSceneInstancesCount;
	static UINT64 MaxSceneMeshesMetaCount;

	UINT64 totalFacesCount = 0;
	AABB sceneAABB;

	// GPU Resources

	// we need only positions as vertex data for depth passes
	Microsoft::WRL::ComPtr<ID3D12Resource> depthVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthVertexBufferUpload;
	D3D12_VERTEX_BUFFER_VIEW depthVertexBufferView;
	CD3DX12_GPU_DESCRIPTOR_HANDLE depthVerticesSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferUpload;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	CD3DX12_GPU_DESCRIPTOR_HANDLE verticesSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferUpload;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	CD3DX12_GPU_DESCRIPTOR_HANDLE indicesSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> meshMetaBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> meshMetaBufferUpload;
	CD3DX12_GPU_DESCRIPTOR_HANDLE meshMetaSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> instancesGPU;
	Microsoft::WRL::ComPtr<ID3D12Resource> instancesGPUUpload;
	CD3DX12_GPU_DESCRIPTOR_HANDLE instancesSRV;

private:

	void _loadObj(
		const std::string& OBJPath,
		const std::string& mtlSearchPath,
		float translation = 0.0f,
		float scale = 1.0f,
		UINT instancesCountX = 1,
		UINT instancesCountZ = 1);

	void _createVBResources(ScenesIndices sceneIndex);
	void _createIBResources(ScenesIndices sceneIndex);
	void _createMeshMetaResources(ScenesIndices sceneIndex);
	void _createInstancesBufferResources(ScenesIndices sceneIndex);
};