#include "stdafx.h"
#include "CameraService.h"
#include "IInputService.h"

void CameraService::OnResize()
{
	auto dimensions = _windowManager->Dimensions();
	auto width = static_cast<float>(dimensions.first);
	auto height = static_cast<float>(dimensions.second);

	for (auto& camera : _cameras)
	{
		camera.OnResize(width, height);
	}

	if (_guiCamera != nullptr)
	{
		_guiCamera->OnResize(width, height);
	}
}

void CameraService::CreateGUICamera(D3DCamera camera)
{
	_guiCamera = std::make_unique<D3DCamera>(camera);
	
	auto dimensions = _windowManager->Dimensions();
	auto width = static_cast<float>(dimensions.first);
	auto height = static_cast<float>(dimensions.second);

	_guiCamera->OnResize(width, height);
}

void CameraService::SetCameras(const std::vector<D3DCamera> cameras)
{
	_cameras = cameras;

	for (std::size_t i = 0; i < _cameras.size(); ++i)
	{
		_cameras[i].SetCameraIndex(i);
	}

	OnResize();
}

void CameraService::Update(const GameTimer& gt)
{
	static Sisu::Vector3 inputAxes;
	static Sisu::Vector3 inputEuler;
	static Sisu::Vector3 nullInput;

	auto a = _inputService->GetKey(KeyCode::A);
	auto d = _inputService->GetKey(KeyCode::D);
	auto s = _inputService->GetKey(KeyCode::S);
	auto w = _inputService->GetKey(KeyCode::W);
	auto q = _inputService->GetKey(KeyCode::Q);
	auto e = _inputService->GetKey(KeyCode::E);
	auto r = _inputService->GetKey(KeyCode::R);
	auto f = _inputService->GetKey(KeyCode::F);
	auto shift = _inputService->GetKey(KeyCode::Shift);

	inputAxes.x = (a ? -1 : 0) + (d ? 1 : 0);
	inputAxes.z = (s ? -1 : 0) + (w ? 1 : 0);
	inputAxes.y = (f ? -1 : 0) + (r ? 1 : 0);

	inputEuler.z = (e ? -1.0 : 0) + (q ? 1.0 : 0);

	if (_inputService->GetMouseButton(0))
	{
		auto mouseDelta = _inputService->GetMouseDelta();
		inputEuler.y = mouseDelta.x;
		inputEuler.x = mouseDelta.y;
	}
	else
	{
		inputEuler.x = 0.0f;
		inputEuler.y = 0.0f;
	}

	// Speed up if left shift is held

	if (shift) 
	{
		inputAxes = inputAxes * 3.0f;
		inputEuler = inputEuler * 3.0f;
	}

	_cameras[_activeCameraIndex].Update(gt, inputAxes, inputEuler);
	_guiCamera->Update(gt, nullInput, nullInput);
}