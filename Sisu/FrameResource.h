#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "SisuUtilities.h"

struct FRObjectConstants
{
	FRObjectConstants(const DirectX::XMMATRIX& worldMatrixToInit)
	{
		// We need to transpose the world matrix, because "by default,
		// matrices in the constant buffer are expected to be in column
		// major format, whereas on the C++ side we're working with
		// row-major matrices." Because DirectX.
		DirectX::XMStoreFloat4x4(&worldMatrix, DirectX::XMMatrixTranspose(worldMatrixToInit));
	}

	DirectX::XMFLOAT4X4 worldMatrix = MathHelper::Identity4x4();
	DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT4 borderColor;
	DirectX::XMFLOAT3 localScale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
};

struct UIObjectConstants
{
	UIObjectConstants(const DirectX::XMMATRIX& worldMatrixToInit,
					  DirectX::XMFLOAT4 uvData)
	{
		DirectX::XMStoreFloat4x4(&worldMatrix, DirectX::XMMatrixTranspose(worldMatrixToInit));
		uvOffset = uvData;
	}

	DirectX::XMFLOAT4X4 worldMatrix = MathHelper::Identity4x4();
	DirectX::XMFLOAT4 uvOffset = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);
};

struct PassConstants
{
	DirectX::XMFLOAT4X4 view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 invView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 invProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 viewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 invViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT3 eyePosW = { 0.0f, 0.0f, 0.0f };

	float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 renderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 invRenderTargetSize = { 0.0f, 0.0f };
	float nearZ = 0.0f;
	float farZ = 0.0f;
	float totalTime = 0.0f;
	float deltaTime = 0.0f;
};

struct FrameResource
{
	FrameResource(ID3D12Device* device, UINT passCount, UINT cbObjectCount, 
				  UINT maxInstancedObjectCount, UINT maxUIObjectCount)
		: _objectCount (0)
	{
		ThrowIfFailed(
			device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(commandAllocator.GetAddressOf()))
		);

		// Create buffers for "normal" objects
		// ddevice*, count, isConstantBuffer
		passConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
		if (cbObjectCount > 0)
		{
			objectConstantBuffer = std::make_unique<UploadBuffer<FRObjectConstants>>(device, cbObjectCount, true);
		}
		instanceBuffer = std::make_unique<UploadBuffer<FRObjectConstants>>(device, maxInstancedObjectCount, false);

		// Create buffers for UI
		UIPassConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, 1, true);
		UIObjectConstantBuffer = std::make_unique<UploadBuffer<UIObjectConstants>>(device, maxUIObjectCount, true);
		UIInstanceBuffer = std::make_unique<UploadBuffer<UIObjectConstants>>(device, maxUIObjectCount, false);
}

	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource() {}

	bool UpdateConstantBufferIfNeeded(ID3D12Device* device, std::size_t objectCount)
	{
		if (objectCount != _objectCount)
		{
			_objectCount = objectCount;
			objectConstantBuffer = nullptr;
			objectConstantBuffer = std::make_unique<UploadBuffer<FRObjectConstants>>(device, objectCount, true);
			return true;
		}

		return false;
	}

public:
	// We cannot reset the command allocator until the GPU is done processing
	// the commands. => Each frame needs their own allocator, and also their
	// own constant buffers.
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;

	std::unique_ptr<UploadBuffer<PassConstants>> passConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<FRObjectConstants>> objectConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<FRObjectConstants>> instanceBuffer = nullptr;

	std::unique_ptr<UploadBuffer<PassConstants>> UIPassConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<UIObjectConstants>> UIObjectConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<UIObjectConstants>> UIInstanceBuffer = nullptr;

	UINT64 fence = 0;

private:
	UINT _objectCount;
};
