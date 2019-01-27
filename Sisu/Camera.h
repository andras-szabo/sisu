#pragma once
#include "D3DRenderer.h"
#include "SisuUtilities.h"

class D3DCamera
{
public:
	D3DCamera() : _position(Sisu::Vector3(0.0f, 0.0f, -15.0f)), _isDirty(true)
	{
	}

	void Update(const GameTimer& gt, 
				const Sisu::Vector3& inputAxes,
			    const Sisu::Vector3& inputEuler);

	DirectX::XMFLOAT3 Position() { return DirectX::XMFLOAT3(_position.x, _position.y, _position.z); }
	DirectX::XMFLOAT4X4* ViewMatrix() { return &_viewMatrix; }
	DirectX::XMFLOAT4X4* ProjectionMatrix() { return &_projectionMatrix; }

	void OnResize(const DirectX::XMMATRIX& newProjectionMatrix);

private:
	DirectX::XMMATRIX ToXMMatrix(const Sisu::Matrix4& m) const;

public:
	float speedUnitPerSecond = 2.0f;
	float rotationSensitivity = 2.0f;

private:
	DirectX::XMFLOAT4X4 _viewMatrix;
	DirectX::XMFLOAT4X4 _projectionMatrix;

	Sisu::Vector3 _position;
	Sisu::Matrix4 _transform;
	Sisu::Matrix4 _rotationMatrix;
	Sisu::Quat _rotation;

	bool _isDirty;
};