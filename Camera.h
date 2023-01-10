#pragma once

#include "Utils.h"

class Camera
{
public:

	Camera();
	~Camera() {}

	void MoveVertical(float amount);
	void Walk(float amount);
	void Strafe(float amount);

	void RotateY(float angle);
	void RotateX(float angle);

	void UpdateViewMatrix();

	void SetProjection(
		float fovY,
		float aspect,
		float nearZ,
		float farZ);

	void LookAt(
		DirectX::FXMVECTOR pos,
		DirectX::FXMVECTOR target,
		DirectX::FXMVECTOR worldUp);
	void LookAt(
		const DirectX::XMFLOAT3& pos,
		const DirectX::XMFLOAT3& target,
		const DirectX::XMFLOAT3& up);

	const DirectX::XMFLOAT4X4& GetView() const { return _view; }
	const DirectX::XMFLOAT4X4& GetProjection() const { return _projection; }
	const DirectX::XMFLOAT4X4& GetVP() const { return _viewProjection; }
	const DirectX::XMFLOAT4X4& GetPrevFrameVP() const
	{
		return _prevFrameViewProjection;
	}

	Frustum GetFrustum() const { return _frustum; }

	const DirectX::XMFLOAT4& GetFrustumCornerWS(UINT corner) const
	{
		assert(corner < 8);
		return _frustum.cornersWS[corner];
	}

	const DirectX::XMFLOAT3& GetUp() const { return _up; }
	const DirectX::XMFLOAT3& GetRight() const { return _right; }
	const DirectX::XMFLOAT3& GetLook() const { return _look; }
	const DirectX::XMFLOAT3& GetPosition() const { return _position; }

	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& pos);

	float GetNearZ() const { return _nearZ; }
	float GetFarZ() const { return _farZ; }
	float GetAspect() const { return _aspect; }
	float GetFovX() const
	{
		return 2.0f * atanf((0.5f * GetNearWindowWidth()) / _nearZ);
	}
	float GetFovY() const { return _fovY; }
	float GetNearWindowHeight() const { return _nearWindowHeight; }
	float GetFarWindowHeight() const { return _farWindowHeight; }
	float GetNearWindowWidth() const { return _aspect * _nearWindowHeight; }
	float GetFarWindowWidth() const { return _aspect * _farWindowHeight; }

	bool ReverseZ() const { return _reverseZ; }

private:

	void _updateFrustumPlanes();

	DirectX::XMFLOAT4X4 _view = Identity4x4;
	DirectX::XMFLOAT4X4 _projection = Identity4x4;
	DirectX::XMFLOAT4X4 _viewProjection = Identity4x4;
	DirectX::XMFLOAT4X4 _prevFrameViewProjection = Identity4x4;

	Frustum _frustum;

	DirectX::XMFLOAT3 _up{ 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 _right{ 1.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 _look{ 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT3 _position{ 0.0f, 0.0f, -1.0f };

	float _nearZ = 0.0f;
	float _farZ = 0.0f;
	float _aspect = 0.0f;
	float _fovY = 0.0f;
	float _nearWindowHeight = 0.0f;
	float _farWindowHeight = 0.0f;

	bool _reverseZ = true;
};