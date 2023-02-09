#include "Scene.h"
#include "DX.h"
#include "DXSampleHelper.h"
#include "DescriptorManager.h"

#include <iostream>
#include <unordered_map>

#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"
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
	float translation,
	float scale,
	UINT instancesCountX,
	UINT instancesCountZ)
{
	fastObjMesh* OBJMesh = fast_obj_read(OBJPath.c_str());
	if (!OBJMesh)
	{
		Utils::PrintToOutput(
			"Error loading %s: file not found\n",
			OBJPath.c_str());
		assert(false);
	}

	std::vector<MeshMeta> meshesMeta;
	XMVECTOR objectMin = g_XMFltMax.v;
	XMVECTOR objectMax = -g_XMFltMax.v;

	std::vector<XMFLOAT3> unindexedPositions;
	std::vector<XMFLOAT3> unindexedNormals;
	std::vector<XMFLOAT4> unindexedColors;
	std::vector<XMFLOAT2> unindexedUVs;

	UINT64 facesCount = 0;
	for (UINT group = 0; group < OBJMesh->group_count; group++)
	{
		const fastObjGroup& currentGroup = OBJMesh->groups[group];

		UINT64 currentFacesCount = currentGroup.face_count;
		facesCount += currentFacesCount;

		unindexedPositions.reserve(currentFacesCount * 3);
		unindexedNormals.reserve(currentFacesCount * 3);
		unindexedColors.reserve(currentFacesCount * 3);
		unindexedUVs.reserve(currentFacesCount * 3);

		XMVECTOR min = g_XMFltMax.v;
		XMVECTOR max = -g_XMFltMax.v;

		int idx = 0;
		for (UINT face = 0; face < currentGroup.face_count; face++)
		{
			// TODO: ensure triangulation
			UINT fv = OBJMesh->face_vertices[currentGroup.face_offset + face];

			for (UINT vertex = 0; vertex < fv; vertex++)
			{
				fastObjIndex attributeIndices =
					OBJMesh->indices[currentGroup.index_offset + idx];

				decltype(unindexedPositions)::value_type tmpPosition = {};
				decltype(unindexedNormals)::value_type tmpNormal = {};
				decltype(unindexedUVs)::value_type tmpUV = {};
				decltype(unindexedColors)::value_type tmpColor = {};

				if (attributeIndices.p)
				{
					tmpPosition =
					{
						OBJMesh->positions[3 * attributeIndices.p + 0],
						OBJMesh->positions[3 * attributeIndices.p + 1],
						OBJMesh->positions[3 * attributeIndices.p + 2]
					};

					tmpPosition.x *= scale;
					tmpPosition.y *= scale;
					tmpPosition.z *= scale;

					unindexedPositions.push_back(tmpPosition);

					min = XMVectorMin(
						min,
						XMLoadFloat3(&tmpPosition));
					max = XMVectorMax(
						max,
						XMLoadFloat3(&tmpPosition));
				}

				if (attributeIndices.t)
				{
					tmpUV =
					{
						OBJMesh->texcoords[2 * attributeIndices.t + 0],
						OBJMesh->texcoords[2 * attributeIndices.t + 1]
					};
					unindexedUVs.push_back(tmpUV);
				}

				if (attributeIndices.n)
				{
					tmpNormal =
					{
						OBJMesh->normals[3 * attributeIndices.n + 0],
						OBJMesh->normals[3 * attributeIndices.n + 1],
						OBJMesh->normals[3 * attributeIndices.n + 2]
					};
					XMStoreFloat3(
						&tmpNormal,
						XMVector3Normalize(XMLoadFloat3(&tmpNormal)));

					unindexedNormals.push_back(tmpNormal);
				}

				tmpColor = { 0.8f, 0.8f, 0.8f, 1.0f };
				unindexedColors.push_back(tmpColor);

				idx++;
			}
		}

		// optimize mesh data and perform indexing
		meshopt_Stream streams[] =
		{
			{
				unindexedPositions.data(),
				sizeof(decltype(unindexedPositions)::value_type),
				sizeof(decltype(unindexedPositions)::value_type)
			},
			{
				unindexedNormals.data(),
				sizeof(decltype(unindexedNormals)::value_type),
				sizeof(decltype(unindexedNormals)::value_type)
			},
			{
				unindexedColors.data(),
				sizeof(decltype(unindexedColors)::value_type),
				sizeof(decltype(unindexedColors)::value_type)
			},
			{
				unindexedUVs.data(),
				sizeof(decltype(unindexedUVs)::value_type),
				sizeof(decltype(unindexedUVs)::value_type)
			}
		};

		UINT64 indexCount = currentFacesCount * 3;
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
			unindexedPositions.data(),
			unindexedPositions.data(),
			unindexedPositions.size(),
			sizeof(decltype(unindexedPositions)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			unindexedNormals.data(),
			unindexedNormals.data(),
			unindexedNormals.size(),
			sizeof(decltype(unindexedNormals)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			unindexedColors.data(),
			unindexedColors.data(),
			unindexedColors.size(),
			sizeof(decltype(unindexedColors)::value_type),
			remap.data());
		meshopt_remapVertexBuffer(
			unindexedUVs.data(),
			unindexedUVs.data(),
			unindexedUVs.size(),
			sizeof(decltype(unindexedUVs)::value_type),
			remap.data());
		meshopt_optimizeVertexCache(
			indicesCPU.data() + indicesCPUOldSize,
			indicesCPU.data() + indicesCPUOldSize,
			indexCount,
			unindexedPositions.size());

#ifdef SCENE_MESHLETIZATION
		// generate meshlets for more efficient culling
		// not for use with mesh shaders
		const UINT64 maxVertices = 128;
		// should be in sync with SWRTriangleThreadsX
		const UINT64 maxTriangles = 256;
		// 0.0 had better results overall
		const float coneWeight = 0.0f;

		UINT64 maxMeshlets = meshopt_buildMeshletsBound(
			indexCount,
			maxVertices,
			maxTriangles);
		std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
		// indices into positionsCPU + offset
		std::vector<UINT> meshletVertices(maxMeshlets* maxVertices);
		std::vector<UINT8> meshletTriangles(
			maxMeshlets* maxTriangles * 3);

		UINT64 meshletCount = meshopt_buildMeshlets(
			meshlets.data(),
			meshletVertices.data(),
			meshletTriangles.data(),
			indicesCPU.data() + indicesCPUOldSize,
			indexCount,
			reinterpret_cast<float*>(unindexedPositions.data()),
			uniqueVertexCount,
			sizeof(decltype(unindexedPositions)::value_type),
			maxVertices,
			maxTriangles,
			coneWeight);

		const meshopt_Meshlet& last = meshlets[meshletCount - 1];

		meshletVertices.resize(last.vertex_offset + last.vertex_count);
		meshletTriangles.resize(
			last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
		meshlets.resize(meshletCount);

		// emulation of classic index buffer
		indicesCPU.resize(indicesCPUOldSize + meshletTriangles.size());

		MeshMeta mesh = {};
		for (const auto& meshlet : meshlets)
		{
			meshopt_Bounds bounds = meshopt_computeMeshletBounds(
				&meshletVertices[meshlet.vertex_offset],
				&meshletTriangles[meshlet.triangle_offset],
				meshlet.triangle_count,
				reinterpret_cast<float*>(unindexedPositions.data()),
				uniqueVertexCount,
				sizeof(decltype(unindexedPositions)::value_type));
			memcpy(
				&mesh.AABB.center,
				&bounds.center,
				sizeof(decltype(mesh.AABB.center)));
			mesh.AABB.extents =
			{
				bounds.radius,
				bounds.radius,
				bounds.radius
			};

			mesh.indexCountPerInstance = meshlet.triangle_count * 3;
			mesh.instanceCount = 1;
			mesh.startIndexLocation = indicesCPUOldSize;
			mesh.baseVertexLocation = positionsCPUOldSize;
			mesh.startInstanceLocation = 0;

			memcpy(
				&mesh.coneApex,
				&bounds.cone_apex,
				sizeof(decltype(mesh.coneApex)));
			memcpy(
				&mesh.coneAxis,
				&bounds.cone_axis,
				sizeof(decltype(mesh.coneAxis)));
			mesh.coneCutoff = bounds.cone_cutoff;

			meshesMeta.push_back(mesh);

			for (UINT vertex = 0; vertex < meshlet.triangle_count * 3; vertex++)
			{
				indicesCPU[indicesCPUOldSize + vertex] =
					meshletVertices[
						meshlet.vertex_offset + meshletTriangles[
							meshlet.triangle_offset + vertex]];
			}

			indicesCPUOldSize += meshlet.triangle_count * 3;
		}
#else
		MeshMeta mesh = {};
		XMStoreFloat3(&mesh.AABB.center, (min + max) * 0.5f);
		XMStoreFloat3(&mesh.AABB.extents, (max - min) * 0.5f);
		mesh.indexCountPerInstance = indexCount;
		mesh.instanceCount = 1;
		mesh.startIndexLocation = indicesCPUOldSize;
		mesh.baseVertexLocation = positionsCPUOldSize;
		mesh.startInstanceLocation = 0;
		mesh.coneCutoff = D3D12_FLOAT32_MAX;
		meshesMeta.push_back(mesh);
#endif

		objectMin = XMVectorMin(objectMin, min);
		objectMax = XMVectorMax(objectMax, max);

		// pack vertex attributes
		// TODO: pack positions
		for (UINT vertex = 0; vertex < uniqueVertexCount; vertex++)
		{
			auto& dst = positionsCPU[positionsCPUOldSize + vertex].position;
			auto& src = unindexedPositions[vertex];
			dst = src;
		}

		if (!unindexedNormals.empty())
		{
			for (UINT vertex = 0; vertex < uniqueVertexCount; vertex++)
			{
				auto& dst = normalsCPU[normalsCPUOldSize + vertex].packedNormal;
				auto& src = unindexedNormals[vertex];
				dst =
					(meshopt_quantizeUnorm(src.x * 0.5f + 0.5f, 10) << 20) |
					(meshopt_quantizeUnorm(src.y * 0.5f + 0.5f, 10) << 10) |
					meshopt_quantizeUnorm(src.z * 0.5f + 0.5f, 10);
			}
		}

		if (!unindexedUVs.empty())
		{
			for (UINT vertex = 0; vertex < uniqueVertexCount; vertex++)
			{
				auto& dst =
					texcoordsCPU[texcoordsCPUOldSize + vertex].packedUV;
				auto& src = unindexedUVs[vertex];
				dst |= UINT(meshopt_quantizeHalf(src.x)) << 16;
				dst |= UINT(meshopt_quantizeHalf(src.y));
			}
		}

		if (!unindexedColors.empty())
		{
			for (UINT vertex = 0; vertex < uniqueVertexCount; vertex++)
			{
				auto& dst = colorsCPU[colorsCPUOldSize + vertex].packedColor;
				auto& src = unindexedColors[vertex];
				dst.x |= UINT(meshopt_quantizeHalf(src.x)) << 16;
				dst.x |= UINT(meshopt_quantizeHalf(src.y));
				dst.y |= UINT(meshopt_quantizeHalf(src.z)) << 16;
				dst.y |= UINT(meshopt_quantizeHalf(src.w));
			}
		}

		unindexedPositions.clear();
		unindexedNormals.clear();
		unindexedColors.clear();
		unindexedUVs.clear();
	}

	fast_obj_destroy(OBJMesh);

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
				instance.color =
				{
					static_cast<float>(meshIndex & 1),
					static_cast<float>(meshIndex & 3) / 4,
					static_cast<float>(meshIndex & 7) / 8
				};

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