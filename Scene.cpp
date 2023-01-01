#include "Scene.h"
#include "DX.h"
#include "DXSampleHelper.h"
#include "DescriptorManager.h"

#include <iostream>
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION // define this in only *one* .cc
// Optional. define TINYOBJLOADER_USE_MAPBOX_EARCUT gives robust trinagulation. Requires C++11
//#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"

Scene* Scene::CurrentScene;
Scene Scene::PlantScene;
Scene Scene::BuddhaScene;
UINT64 Scene::MaxSceneFacesCount = 0;
UINT64 Scene::MaxSceneInstancesCount = 0;
UINT64 Scene::MaxSceneMeshesMetaCount = 0;

using namespace DirectX;

// taken from glm/gtx/hash.inl
void HashCombine(size_t& seed, size_t hash)
{
	hash += 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= hash;
}

namespace std
{
	template<> struct hash<DirectX::XMFLOAT2>
	{
		size_t operator()(const DirectX::XMFLOAT2& v) const
		{
			size_t seed = 0;
			hash<float> hasher;
			HashCombine(seed, hasher(v.x));
			HashCombine(seed, hasher(v.y));
			return seed;
		}
	};

	template<> struct hash<DirectX::XMFLOAT3>
	{
		size_t operator()(const DirectX::XMFLOAT3& v) const
		{
			size_t seed = 0;
			hash<float> hasher;
			HashCombine(seed, hasher(v.x));
			HashCombine(seed, hasher(v.y));
			HashCombine(seed, hasher(v.z));
			return seed;
		}
	};

	template<> struct hash<DirectX::XMFLOAT4>
	{
		size_t operator()(const DirectX::XMFLOAT4& v) const
		{
			size_t seed = 0;
			hash<float> hasher;
			HashCombine(seed, hasher(v.x));
			HashCombine(seed, hasher(v.y));
			HashCombine(seed, hasher(v.z));
			HashCombine(seed, hasher(v.w));
			return seed;
		}
	};

	template<> struct hash<Vertex>
	{
		size_t operator()(const Vertex& vertex) const
		{
			size_t seed = 0;
			HashCombine(seed, hash<DirectX::XMFLOAT3>()(vertex.position));
			HashCombine(seed, hash<DirectX::XMFLOAT3>()(vertex.normal));
			HashCombine(seed, hash<DirectX::XMFLOAT3>()(vertex.color));
			HashCombine(seed, hash<DirectX::XMFLOAT2>()(vertex.uv));
			return seed;
		}
	};
}

void Scene::LoadBuddha()
{
	CurrentScene = this;

	XMVECTOR sceneMin = g_XMFltMax.v;
	XMVECTOR sceneMax = -g_XMFltMax.v;
	XMStoreFloat3(&sceneAABB.center, (sceneMin + sceneMax) * 0.5f);
	XMStoreFloat3(&sceneAABB.extents, (sceneMax - sceneMin) * 0.5f);

	camera.LookAt(
		XMVectorSet(-30.0f, 100.0f, -30.0f, 0.0f),
		XMVectorSet(100.0f, 0.0f, 100.0f, 0.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
	);

	camera.SetProjection(
		XMConvertToRadians(FOV),
		Settings::BackBufferAspectRatio,
		nearZ,
		farZ);

	lightDirection = { -1.0f, 1.0f, -1.0f };

	_loadObj(
		"Buddha//buddha.obj",
		"",
		50.0f,
		100.0f,
		10,
		10);

	_createVBResources(Buddha);
	_createIBResources(Buddha);
	_createMeshMetaResources(Buddha);
	_createInstancesBufferResources(Buddha);

	MaxSceneFacesCount = max(
		MaxSceneFacesCount,
		totalFacesCount);
	MaxSceneInstancesCount = max(
		MaxSceneInstancesCount,
		instancesCPU.size());
	MaxSceneMeshesMetaCount = max(
		MaxSceneMeshesMetaCount,
		mutualMeshMeta.size());
}

void Scene::LoadPlant()
{
	CurrentScene = this;

	XMVECTOR sceneMin = g_XMFltMax.v;
	XMVECTOR sceneMax = -g_XMFltMax.v;
	XMStoreFloat3(&sceneAABB.center, (sceneMin + sceneMax) * 0.5f);
	XMStoreFloat3(&sceneAABB.extents, (sceneMax - sceneMin) * 0.5f);

	camera.LookAt(
		XMVectorSet(-1000.0f, 500.0f, 600.0f, 0.0f),
		XMVectorSet(-999.0f, 500.0f, 600.0f, 0.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
	);

	camera.SetProjection(
		XMConvertToRadians(FOV),
		Settings::BackBufferAspectRatio,
		nearZ,
		farZ);

	lightDirection = { 1.0f, 1.0f, 1.0f };

	_loadObj(
		"powerplant//powerplant.obj",
		"./powerplant",
		0.0f,
		0.01f,
		3,
		1);

	_createVBResources(Plant);
	_createIBResources(Plant);
	_createMeshMetaResources(Plant);
	_createInstancesBufferResources(Plant);

	MaxSceneFacesCount = max(
		MaxSceneFacesCount,
		totalFacesCount);
	MaxSceneInstancesCount = max(
		MaxSceneInstancesCount,
		instancesCPU.size());
	MaxSceneMeshesMetaCount = max(
		MaxSceneMeshesMetaCount,
		mutualMeshMeta.size());
}

void Scene::_loadObj(
	const std::string& OBJPath,
	const std::string& mtlSearchPath,
	float translation,
	float scale,
	UINT instancesCountX,
	UINT instancesCountZ)
{
	tinyobj::ObjReaderConfig reader_config;
	// Path to material files
	reader_config.mtl_search_path = mtlSearchPath;

	tinyobj::ObjReader reader;

	if (!reader.ParseFromFile(OBJPath, reader_config))
	{
		if (!reader.Error().empty())
		{
			std::cerr << "TinyObjReader: " << reader.Error();
		}
		exit(1);
	}

	if (!reader.Warning().empty())
	{
		std::cout << "TinyObjReader: " << reader.Warning();
	}

	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();
	auto& materials = reader.GetMaterials();

	std::vector<MeshMeta> meshesMeta;
	XMVECTOR objectMin = g_XMFltMax.v;
	XMVECTOR objectMax = -g_XMFltMax.v;

	std::unordered_map<Vertex, UINT> uniqueVertices;
	UINT64 facesCount = 0;

	for (size_t s = 0; s < shapes.size(); s++)
	{
		MeshMeta mesh{};
		mesh.startIndexLocation = mutualIndices.size();
		mesh.baseVertexLocation = 0;//vertices.size();
		mesh.indexCountPerInstance = mutualIndices.size();

		XMVECTOR min = g_XMFltMax.v;
		XMVECTOR max = -g_XMFltMax.v;

		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
		{
			facesCount++;

			size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
			size_t fm = shapes[s].mesh.material_ids[f];
			XMFLOAT3 faceDiffuse;
			if (mtlSearchPath.size())
			{
				auto& d = materials[fm].diffuse;
				faceDiffuse = { d[0], d[1], d[2] };
			}
			else
			{
				faceDiffuse = { 0.8f, 0.8f, 0.8f };
			}
			// Loop over vertices in the face.
			for (size_t v = 0; v < fv; v++)
			{
				Vertex tmpVertex = {};

				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
				tmpVertex.position = {
					attrib.vertices[3 * size_t(idx.vertex_index) + 0],
					attrib.vertices[3 * size_t(idx.vertex_index) + 1],
					attrib.vertices[3 * size_t(idx.vertex_index) + 2]
				};

				tmpVertex.position.x *= scale;
				tmpVertex.position.y *= scale;
				tmpVertex.position.z *= scale;

				min = XMVectorMin(min, XMLoadFloat3(&tmpVertex.position));
				max = XMVectorMax(max, XMLoadFloat3(&tmpVertex.position));

				// Check if `normal_index` is zero or positive. negative = no normal data
				if (idx.normal_index >= 0)
				{
					tmpVertex.normal = {
						attrib.normals[3 * size_t(idx.normal_index) + 0],
						attrib.normals[3 * size_t(idx.normal_index) + 1],
						attrib.normals[3 * size_t(idx.normal_index) + 2]
					};
				}

				// Check if `texcoord_index` is zero or positive. negative = no texcoord data
				if (idx.texcoord_index >= 0)
				{
					tmpVertex.uv = {
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
					};
				}

				tmpVertex.color = faceDiffuse;

				if (uniqueVertices.count(tmpVertex) == 0)
				{
					uniqueVertices[tmpVertex] = mutualVertices.size();

					mutualDepthVertices.push_back({ tmpVertex.position });
					mutualVertices.push_back(tmpVertex);
				}

				mutualIndices.push_back(uniqueVertices[tmpVertex]);
			}

			index_offset += fv;

			// per-face material
			//shapes[s].mesh.material_ids[f];
		}

		XMStoreFloat3(&mesh.AABB.center, (min + max) * 0.5f);
		XMStoreFloat3(&mesh.AABB.extents, (max - min) * 0.5f);
		mesh.indexCountPerInstance =
			mutualIndices.size() - mesh.indexCountPerInstance;
		mesh.instanceCount = 1;
		mesh.startInstanceLocation = 0;
		meshesMeta.push_back(mesh);

		objectMin = XMVectorMin(objectMin, min);
		objectMax = XMVectorMax(objectMax, max);
	}

	AABB objectBoundingVolume;
	XMStoreFloat3(
		&objectBoundingVolume.center,
		(objectMin + objectMax) * 0.5f);
	XMStoreFloat3(
		&objectBoundingVolume.extents,
		(objectMax - objectMin) * 0.5f);

	Prefab newPrefab;
	newPrefab.meshesOffset = mutualMeshMeta.size();
	newPrefab.meshesCount = meshesMeta.size();
	prefabs.push_back(newPrefab);

	mutualMeshMeta.insert(
		mutualMeshMeta.end(),
		meshesMeta.begin(),
		meshesMeta.end());

	// generate instances
	const UINT totalMeshInstances = instancesCountX * instancesCountZ;

	totalFacesCount += facesCount * totalMeshInstances;

	UINT newInstancesOffset = instancesCPU.size();
	instancesCPU.resize(
		instancesCPU.size() +
		newPrefab.meshesCount * instancesCountX * instancesCountZ);
	for (UINT mesh = 0; mesh < newPrefab.meshesCount; mesh++)
	{
		UINT meshIndex = newPrefab.meshesOffset + mesh;
		auto& currentMesh = mutualMeshMeta[meshIndex];
		currentMesh.instanceCount = totalMeshInstances;
		currentMesh.startInstanceLocation =
			newInstancesOffset + mesh * totalMeshInstances;
		for (UINT instanceZ = 0; instanceZ < instancesCountZ; instanceZ++)
		{
			for (UINT instanceX = 0; instanceX < instancesCountX; instanceX++)
			{
				XMMATRIX transform = XMMatrixTranslation(
					(translation + objectBoundingVolume.extents.x * 2.0f) *
					instanceX,
					0.0f,
					(translation + objectBoundingVolume.extents.z * 2.0f) *
					instanceZ);

				Instance& instance =
					instancesCPU[currentMesh.startInstanceLocation +
					instanceZ * instancesCountX + instanceX];
				XMStoreFloat4x4(
					&instance.worldTransform,
					transform);
				instance.meshID = meshIndex;

				sceneAABB = Utils::MergeAABBs(
					sceneAABB,
					Utils::TransformAABB(objectBoundingVolume, transform));
			}
		}
	}
}

void Scene::_createVBResources(ScenesIndices sceneIndex)
{
	UINT64 bufferSize =
		mutualVertices.size() *
		sizeof(decltype(mutualVertices)::value_type);
	Utils::CreateDefaultHeapBuffer(
		DX::CommandList.Get(),
		mutualVertices.data(),
		bufferSize,
		vertexBuffer,
		vertexBufferUpload,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	NAME_D3D12_OBJECT(vertexBuffer);
	NAME_D3D12_OBJECT(vertexBufferUpload);

	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes =
		sizeof(decltype(mutualVertices)::value_type);
	vertexBufferView.SizeInBytes = bufferSize;

	bufferSize =
		mutualDepthVertices.size() *
		sizeof(decltype(mutualDepthVertices)::value_type);
	Utils::CreateDefaultHeapBuffer(
		DX::CommandList.Get(),
		mutualDepthVertices.data(),
		bufferSize,
		depthVertexBuffer,
		depthVertexBufferUpload,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	NAME_D3D12_OBJECT(depthVertexBuffer);
	NAME_D3D12_OBJECT(depthVertexBufferUpload);

	depthVertexBufferView.BufferLocation =
		depthVertexBuffer->GetGPUVirtualAddress();
	depthVertexBufferView.StrideInBytes =
		sizeof(decltype(mutualDepthVertices)::value_type);
	depthVertexBufferView.SizeInBytes = bufferSize;

	// VB SRVs
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = mutualDepthVertices.size();
	SRVDesc.Buffer.StructureByteStride =
		sizeof(decltype(mutualDepthVertices)::value_type);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	DX::Device->CreateShaderResourceView(
		depthVertexBuffer.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(DepthVerticesSRV + sceneIndex));

	depthVerticesSRV =
		Descriptors::SV.GetGPUHandle(DepthVerticesSRV + sceneIndex);

	SRVDesc.Buffer.NumElements = mutualVertices.size();
	SRVDesc.Buffer.StructureByteStride =
		sizeof(decltype(mutualVertices)::value_type);
	DX::Device->CreateShaderResourceView(
		vertexBuffer.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(VerticesSRV + sceneIndex));

	verticesSRV =
		Descriptors::SV.GetGPUHandle(VerticesSRV + sceneIndex);
}

void Scene::_createIBResources(ScenesIndices sceneIndex)
{
	UINT64 bufferSize =
		mutualIndices.size() * sizeof(decltype(mutualIndices)::value_type);
	Utils::CreateDefaultHeapBuffer(
		DX::CommandList.Get(),
		mutualIndices.data(),
		bufferSize,
		indexBuffer,
		indexBufferUpload,
		D3D12_RESOURCE_STATE_INDEX_BUFFER);
	NAME_D3D12_OBJECT(indexBuffer);
	NAME_D3D12_OBJECT(indexBufferUpload);

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = bufferSize;
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	// indices SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = mutualIndices.size();
	SRVDesc.Buffer.StructureByteStride =
		sizeof(decltype(mutualIndices)::value_type);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	DX::Device->CreateShaderResourceView(
		indexBuffer.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(IndicesSRV + sceneIndex));

	indicesSRV =
		Descriptors::SV.GetGPUHandle(IndicesSRV + sceneIndex);
}

void Scene::_createMeshMetaResources(ScenesIndices sceneIndex)
{
	UINT64 bufferSize =
		mutualMeshMeta.size() * sizeof(decltype(mutualMeshMeta)::value_type);
	Utils::CreateDefaultHeapBuffer(
		DX::CommandList.Get(),
		mutualMeshMeta.data(),
		bufferSize,
		meshMetaBuffer,
		meshMetaBufferUpload,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	NAME_D3D12_OBJECT(meshMetaBuffer);
	NAME_D3D12_OBJECT(meshMetaBufferUpload);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = mutualMeshMeta.size();
	SRVDesc.Buffer.StructureByteStride =
		sizeof(decltype(mutualMeshMeta)::value_type);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	DX::Device->CreateShaderResourceView(
		meshMetaBuffer.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(MeshMetaSRV + sceneIndex));

	meshMetaSRV =
		Descriptors::SV.GetGPUHandle(MeshMetaSRV + sceneIndex);
}

void Scene::_createInstancesBufferResources(ScenesIndices sceneIndex)
{
	// main buffer with instances data
	UINT64 bufferSize =
		instancesCPU.size() * sizeof(decltype(instancesCPU)::value_type);
	Utils::CreateDefaultHeapBuffer(
		DX::CommandList.Get(),
		instancesCPU.data(),
		bufferSize,
		instancesGPU,
		instancesGPUUpload,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	NAME_D3D12_OBJECT(instancesGPU);
	NAME_D3D12_OBJECT(instancesGPUUpload);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = instancesCPU.size();
	SRVDesc.Buffer.StructureByteStride =
		sizeof(decltype(instancesCPU)::value_type);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	DX::Device->CreateShaderResourceView(
		instancesGPU.Get(),
		&SRVDesc,
		Descriptors::SV.GetCPUHandle(InstancesSRV + sceneIndex));

	instancesSRV =
		Descriptors::SV.GetGPUHandle(InstancesSRV + sceneIndex);
}