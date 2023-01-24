#include "Scene.h"
#include "DX.h"
#include "DXSampleHelper.h"
#include "DescriptorManager.h"

#include <iostream>
#include <unordered_map>

#include "rapidobj.hpp"
#include "meshoptimizer/meshoptimizer.h"

Scene* Scene::CurrentScene;
Scene Scene::PlantScene;
Scene Scene::BuddhaScene;
UINT64 Scene::MaxSceneFacesCount = 0;
UINT64 Scene::MaxSceneInstancesCount = 0;
UINT64 Scene::MaxSceneMeshesMetaCount = 0;

using namespace DirectX;

void Scene::LoadBuddha()
{
	CurrentScene = this;

	XMVECTOR sceneMin = g_XMFltMax.v;
	XMVECTOR sceneMax = -g_XMFltMax.v;
	XMStoreFloat3(&sceneAABB.center, (sceneMin + sceneMax) * 0.5f);
	XMStoreFloat3(&sceneAABB.extents, (sceneMax - sceneMin) * 0.5f);

	camera.SetProjection(
		XMConvertToRadians(FOV),
		Settings::BackBufferAspectRatio,
		nearZ,
		farZ);

	camera.LookAt(
		XMVectorSet(-30.0f, 100.0f, -30.0f, 0.0f),
		XMVectorSet(100.0f, 0.0f, 100.0f, 0.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
	);

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

	MaxSceneFacesCount = std::max(
		MaxSceneFacesCount,
		totalFacesCount);
	MaxSceneInstancesCount = std::max(
		MaxSceneInstancesCount,
		instancesCPU.size());
	MaxSceneMeshesMetaCount = std::max(
		MaxSceneMeshesMetaCount,
		meshesMetaCPU.size());
}

void Scene::LoadPlant()
{
	CurrentScene = this;

	XMVECTOR sceneMin = g_XMFltMax.v;
	XMVECTOR sceneMax = -g_XMFltMax.v;
	XMStoreFloat3(&sceneAABB.center, (sceneMin + sceneMax) * 0.5f);
	XMStoreFloat3(&sceneAABB.extents, (sceneMax - sceneMin) * 0.5f);

	camera.SetProjection(
		XMConvertToRadians(FOV),
		Settings::BackBufferAspectRatio,
		nearZ,
		farZ);

	camera.LookAt(
		XMVectorSet(-1000.0f, 500.0f, 600.0f, 0.0f),
		XMVectorSet(-999.0f, 500.0f, 600.0f, 0.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
	);

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

	MaxSceneFacesCount = std::max(
		MaxSceneFacesCount,
		totalFacesCount);
	MaxSceneInstancesCount = std::max(
		MaxSceneInstancesCount,
		instancesCPU.size());
	MaxSceneMeshesMetaCount = std::max(
		MaxSceneMeshesMetaCount,
		meshesMetaCPU.size());
}

void Scene::_loadObj(
	const std::string& OBJPath,
	const std::string& mtlSearchPath,
	float translation,
	float scale,
	UINT instancesCountX,
	UINT instancesCountZ)
{
	rapidobj::Result result = rapidobj::ParseFile(OBJPath);

	if (result.error)
	{
		std::cout << result.error.code.message() << '\n';
		return;
	}

	bool success = Triangulate(result);

	if (!success)
	{
		std::cout << result.error.code.message() << '\n';
		return;
	}

	auto& attrib = result.attributes;
	auto& shapes = result.shapes;
	auto& materials = result.materials;

	std::vector<MeshMeta> meshesMeta;
	XMVECTOR objectMin = g_XMFltMax.v;
	XMVECTOR objectMax = -g_XMFltMax.v;

	UINT64 facesCount = 0;
	for (size_t s = 0; s < shapes.size(); s++)
	{
		UINT64 currentShapeFacesCount = shapes[s].mesh.num_face_vertices.size();
		facesCount += currentShapeFacesCount;

		std::vector<VertexPosition> unindexedPositions;
		unindexedPositions.reserve(currentShapeFacesCount * 3);
		std::vector<VertexNormal> unindexedNormals;
		unindexedNormals.reserve(currentShapeFacesCount * 3);
		std::vector<VertexColor> unindexedColors;
		unindexedColors.reserve(currentShapeFacesCount * 3);
		std::vector<VertexUV> unindexedUVs;
		unindexedUVs.reserve(currentShapeFacesCount * 3);

		MeshMeta mesh{};
		mesh.startIndexLocation = indicesCPU.size();
		mesh.baseVertexLocation = positionsCPU.size();

		XMVECTOR min = g_XMFltMax.v;
		XMVECTOR max = -g_XMFltMax.v;

		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < currentShapeFacesCount; f++)
		{
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
			VertexPosition tmpPosition = {};
			VertexNormal tmpNormal = {};
			VertexColor tmpColor = {};
			VertexUV tmpUV = {};
			for (size_t v = 0; v < fv; v++)
			{
				auto idx = shapes[s].mesh.indices[index_offset + v];
				tmpPosition.position =
				{
					attrib.positions[3 * size_t(idx.position_index) + 0],
					attrib.positions[3 * size_t(idx.position_index) + 1],
					attrib.positions[3 * size_t(idx.position_index) + 2]
				};

				tmpPosition.position.x *= scale;
				tmpPosition.position.y *= scale;
				tmpPosition.position.z *= scale;

				min = XMVectorMin(
					min,
					XMLoadFloat3(&tmpPosition.position));
				max = XMVectorMax(
					max,
					XMLoadFloat3(&tmpPosition.position));

				// Check if `normal_index` is zero or positive. negative = no normal data
				if (idx.normal_index >= 0)
				{
					tmpNormal.normal =
					{
						attrib.normals[3 * size_t(idx.normal_index) + 0],
						attrib.normals[3 * size_t(idx.normal_index) + 1],
						attrib.normals[3 * size_t(idx.normal_index) + 2]
					};
				}

				// Check if `texcoord_index` is zero or positive. negative = no texcoord data
				if (idx.texcoord_index >= 0)
				{
					tmpUV.uv =
					{
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
					};
				}

				tmpColor.color = faceDiffuse;

				unindexedPositions.push_back(tmpPosition);
				unindexedNormals.push_back(tmpNormal);
				unindexedColors.push_back(tmpColor);
				unindexedUVs.push_back(tmpUV);
			}

			index_offset += fv;

			// per-face material
			//shapes[s].mesh.material_ids[f];
		}

		meshopt_Stream streams[] =
		{
			{
				unindexedPositions.data(),
				sizeof(decltype(unindexedPositions)::value_type::position),
				sizeof(decltype(unindexedPositions)::value_type)
			},
			{
				unindexedNormals.data(),
				sizeof(decltype(unindexedNormals)::value_type::normal),
				sizeof(decltype(unindexedNormals)::value_type)
			},
			{
				unindexedColors.data(),
				sizeof(decltype(unindexedColors)::value_type::color),
				sizeof(decltype(unindexedColors)::value_type)
			},
			{
				unindexedUVs.data(),
				sizeof(decltype(unindexedUVs)::value_type::uv),
				sizeof(decltype(unindexedUVs)::value_type)
			}
		};

		UINT64 indexCount = currentShapeFacesCount * 3;
		std::vector<UINT> remap(indexCount);
		size_t uniqueVertexCount = meshopt_generateVertexRemapMulti(
			remap.data(),
			nullptr,
			indexCount,
			unindexedPositions.size(),
			streams,
			_countof(streams));

		UINT positionsCPUOldSize = positionsCPU.size();
		UINT normalsCPUOldSize = normalsCPU.size();
		UINT colorsCPUOldSize = colorsCPU.size();
		UINT texcoordsCPUOldSize = texcoordsCPU.size();
		UINT indicesCPUOldSize = indicesCPU.size();

		positionsCPU.resize(positionsCPUOldSize + uniqueVertexCount);
		normalsCPU.resize(normalsCPUOldSize + uniqueVertexCount);
		colorsCPU.resize(colorsCPUOldSize + uniqueVertexCount);
		texcoordsCPU.resize(texcoordsCPUOldSize + uniqueVertexCount);
		indicesCPU.resize(indicesCPUOldSize + indexCount);

		meshopt_remapIndexBuffer(
			indicesCPU.data() + indicesCPUOldSize,
			nullptr,
			indexCount,
			remap.data());
		meshopt_remapVertexBuffer(
			positionsCPU.data() + positionsCPUOldSize,
			unindexedPositions.data(),
			unindexedPositions.size(),
			sizeof(decltype(positionsCPU)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			normalsCPU.data() + normalsCPUOldSize,
			unindexedNormals.data(),
			unindexedNormals.size(),
			sizeof(decltype(normalsCPU)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			colorsCPU.data() + colorsCPUOldSize,
			unindexedColors.data(),
			unindexedColors.size(),
			sizeof(decltype(colorsCPU)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			texcoordsCPU.data() + texcoordsCPUOldSize,
			unindexedUVs.data(),
			unindexedUVs.size(),
			sizeof(decltype(texcoordsCPU)::value_type),
			remap.data());

		XMStoreFloat3(&mesh.AABB.center, (min + max) * 0.5f);
		XMStoreFloat3(&mesh.AABB.extents, (max - min) * 0.5f);
		mesh.indexCountPerInstance =
			indicesCPU.size() - indicesCPUOldSize;
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
	newPrefab.meshesOffset = meshesMetaCPU.size();
	newPrefab.meshesCount = meshesMeta.size();
	prefabs.push_back(newPrefab);

	meshesMetaCPU.insert(
		meshesMetaCPU.end(),
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
		auto& currentMesh = meshesMetaCPU[meshIndex];
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
	positionsGPU.Initialize(
		DX::CommandList.Get(),
		positionsCPU.data(),
		positionsCPU.size(),
		sizeof(decltype(positionsCPU)::value_type),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		VertexPositionsSRV + sceneIndex,
		L"VertexPositions");

	normalsGPU.Initialize(
		DX::CommandList.Get(),
		normalsCPU.data(),
		normalsCPU.size(),
		sizeof(decltype(normalsCPU)::value_type),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		VertexNormalsSRV + sceneIndex,
		L"VertexNormals");

	colorsGPU.Initialize(
		DX::CommandList.Get(),
		colorsCPU.data(),
		colorsCPU.size(),
		sizeof(decltype(colorsCPU)::value_type),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		VertexColorsSRV + sceneIndex,
		L"VertexColors");

	texcoordsGPU.Initialize(
		DX::CommandList.Get(),
		texcoordsCPU.data(),
		texcoordsCPU.size(),
		sizeof(decltype(texcoordsCPU)::value_type),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		VertexTexcoordsSRV + sceneIndex,
		L"VertexTexcoords");
}

void Scene::_createIBResources(ScenesIndices sceneIndex)
{
	indicesGPU.Initialize(
		DX::CommandList.Get(),
		indicesCPU.data(),
		indicesCPU.size(),
		sizeof(decltype(indicesCPU)::value_type),
		D3D12_RESOURCE_STATE_INDEX_BUFFER,
		IndicesSRV + sceneIndex,
		L"Indices");
}

void Scene::_createMeshMetaResources(ScenesIndices sceneIndex)
{
	meshesMetaGPU.Initialize(
		DX::CommandList.Get(),
		meshesMetaCPU.data(),
		meshesMetaCPU.size(),
		sizeof(decltype(meshesMetaCPU)::value_type),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		MeshesMetaSRV + sceneIndex,
		L"MeshesMeta");
}

void Scene::_createInstancesBufferResources(ScenesIndices sceneIndex)
{
	instancesGPU.Initialize(
		DX::CommandList.Get(),
		instancesCPU.data(),
		instancesCPU.size(),
		sizeof(decltype(instancesCPU)::value_type),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		InstancesSRV + sceneIndex,
		L"Instances");
}