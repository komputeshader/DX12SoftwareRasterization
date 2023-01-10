#include "Camera.h"
#include "Settings.h"

using namespace DirectX;

Camera::Camera()
{
	SetProjection(
		XM_PIDIV4,
		1.0f,
		Settings::CameraNearZ,
		Settings::CameraFarZ);
}

void Camera::MoveVertical(float amount)
{
	const XMVECTOR up = XMLoadFloat3(&_up);
	const XMVECTOR position = XMLoadFloat3(&_position);
	XMStoreFloat3(
		&_position,
		XMVectorMultiplyAdd(XMVectorReplicate(amount), up, position));
}

void Camera::Walk(float amount)
{
	const XMVECTOR look = XMLoadFloat3(&_look);
	const XMVECTOR position = XMLoadFloat3(&_position);
	XMStoreFloat3(
		&_position,
		XMVectorMultiplyAdd(XMVectorReplicate(amount), look, position));
}

void Camera::Strafe(float amount)
{
	const XMVECTOR right = XMLoadFloat3(&_right);
	const XMVECTOR position = XMLoadFloat3(&_position);
	XMStoreFloat3(
		&_position,
		XMVectorMultiplyAdd(XMVectorReplicate(amount), right, position));
}

void Camera::RotateY(float angle)
{
	const XMMATRIX R = XMMatrixRotationY(angle);

	XMStoreFloat3(&_right, XMVector3TransformNormal(XMLoadFloat3(&_right), R));
	XMStoreFloat3(&_up, XMVector3TransformNormal(XMLoadFloat3(&_up), R));
	XMStoreFloat3(&_look, XMVector3TransformNormal(XMLoadFloat3(&_look), R));
}

void Camera::RotateX(float angle)
{
	const XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&_right), angle);

	XMStoreFloat3(&_up, XMVector3TransformNormal(XMLoadFloat3(&_up), R));
	XMStoreFloat3(&_look, XMVector3TransformNormal(XMLoadFloat3(&_look), R));
}

void Camera::UpdateViewMatrix()
{
	XMVECTOR R = XMLoadFloat3(&_right);
	XMVECTOR L = XMLoadFloat3(&_look);
	XMVECTOR P = XMLoadFloat3(&_position);

	L = XMVector3Normalize(L);
	XMVECTOR U = XMVector3Normalize(XMVector3Cross(L, R));
	R = XMVector3Cross(U, L);

	float x = -XMVectorGetX(XMVector3Dot(P, R));
	float y = -XMVectorGetX(XMVector3Dot(P, U));
	float z = -XMVectorGetX(XMVector3Dot(P, L));

	XMStoreFloat3(&_right, R);
	XMStoreFloat3(&_up, U);
	XMStoreFloat3(&_look, L);

	_view(0, 0) = _right.x;
	_view(1, 0) = _right.y;
	_view(2, 0) = _right.z;
	_view(3, 0) = x;

	_view(0, 1) = _up.x;
	_view(1, 1) = _up.y;
	_view(2, 1) = _up.z;
	_view(3, 1) = y;

	_view(0, 2) = _look.x;
	_view(1, 2) = _look.y;
	_view(2, 2) = _look.z;
	_view(3, 2) = z;

	_view(0, 3) = 0.0f;
	_view(1, 3) = 0.0f;
	_view(2, 3) = 0.0f;
	_view(3, 3) = 1.0f;

	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX projection = XMLoadFloat4x4(&_projection);
	_prevFrameViewProjection = _viewProjection;
	XMStoreFloat4x4(&_viewProjection, view * projection);
	_updateFrustumPlanes();

	float nearWindowHalfWidth = GetNearWindowWidth() * 0.5f;
	float nearWindowHalfHeight = GetNearWindowHeight() * 0.5f;
	XMVECTOR nearLook = L * _nearZ;
	XMVECTOR nearUp = U * nearWindowHalfHeight;
	XMVECTOR nearRight = R * nearWindowHalfWidth;
	XMStoreFloat4(
		&_frustum.cornersWS[0], P + nearLook + nearUp - nearRight);
	XMStoreFloat4(
		&_frustum.cornersWS[1], P + nearLook + nearUp + nearRight);
	XMStoreFloat4(
		&_frustum.cornersWS[2], P + nearLook - nearUp + nearRight);
	XMStoreFloat4(
		&_frustum.cornersWS[3], P + nearLook - nearUp - nearRight);

	float farWindowHalfWidth = GetFarWindowWidth() * 0.5f;
	float farWindowHalfHeight = GetFarWindowHeight() * 0.5f;
	XMVECTOR farLook = L * _farZ;
	XMVECTOR farUp = U * farWindowHalfHeight;
	XMVECTOR farRight = R * farWindowHalfWidth;
	XMStoreFloat4(&_frustum.cornersWS[4], P + farLook + farUp - farRight);
	XMStoreFloat4(&_frustum.cornersWS[5], P + farLook + farUp + farRight);
	XMStoreFloat4(&_frustum.cornersWS[6], P + farLook - farUp + farRight);
	XMStoreFloat4(&_frustum.cornersWS[7], P + farLook - farUp - farRight);
}

void Camera::SetProjection(
	float fovY,
	float aspect,
	float nearZ,
	float farZ)
{
	_fovY = fovY;
	_aspect = aspect;
	_nearZ = nearZ;
	_farZ = farZ;

	float tmp = 2.0f * tanf(0.5f * fovY);
	_nearWindowHeight = nearZ * tmp;
	_farWindowHeight = farZ * tmp;

	XMMATRIX projection = XMMatrixPerspectiveFovLH(
		_fovY,
		_aspect,
		_nearZ,
		_farZ);
	if (_reverseZ)
	{
		XMMATRIX reverseZ = XMMatrixSet(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, -1.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 1.0f);
		projection *= reverseZ;
	}
	XMStoreFloat4x4(&_projection, projection);
	_prevFrameViewProjection = _viewProjection;
	XMStoreFloat4x4(&_viewProjection, XMLoadFloat4x4(&_view) * projection);

	_updateFrustumPlanes();
}

void Camera::_updateFrustumPlanes()
{
	Utils::GetFrustumPlanes(XMLoadFloat4x4(&_viewProjection), _frustum);

	if (_reverseZ)
	{
		std::swap(_frustum.n, _frustum.f);
	}
}

void Camera::LookAt(
	FXMVECTOR pos,
	FXMVECTOR target,
	FXMVECTOR worldUp)
{
	XMVECTOR look = XMVector3Normalize(target - pos);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, look));
	XMVECTOR up = XMVector3Cross(look, right);

	XMStoreFloat3(&_position, pos);
	XMStoreFloat3(&_look, look);
	XMStoreFloat3(&_right, right);
	XMStoreFloat3(&_up, up);
}

void Camera::LookAt(
	const XMFLOAT3& pos,
	const XMFLOAT3& target,
	const XMFLOAT3& up)
{
	LookAt(XMLoadFloat3(&pos), XMLoadFloat3(&target), XMLoadFloat3(&up));
}

void Camera::SetPosition(float x, float y, float z)
{
	_position.x = x;
	_position.y = y;
	_position.z = z;
}

void Camera::SetPosition(const XMFLOAT3& pos)
{
	_position = pos;
}
