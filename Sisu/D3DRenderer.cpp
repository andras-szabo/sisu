#include "stdafx.h"
#include "D3DRenderer.h"
#include "ICameraService.h"
#include "IGUIService.h"

bool D3DRenderer::Init()
{
#if defined(DEBUG) || defined(_DEBUG)		// Enable D3D12 debug layer
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	Init_00_CreateDXGIFactory();
	Init_01_CreateDevice();
	Init_02_CreateFence();
	Init_03_QueryDescriptorSizes();
	Init_04_CheckMSAAQualitySupport();

#ifdef _DEBUG
	_logger = std::make_unique<D3DLogger>();
	_logger->LogAdapters(_dxgiFactory, _backBufferFormat);
#endif

	Init_05_CreateCommandObjects();
	Init_06_CreateSwapChain();
	Init_07_CreateRtvAndDsvDescriptorHeaps();

	Init_08_CreateUIHeap();

	OnResize();

	//TODO - can we actually fail here?
	return true;
}

ID3D12Resource* D3DRenderer::CurrentBackBuffer() const
{
	return _swapChainBuffer[_currentBackBufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DRenderer::CurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		_currentBackBufferIndex,
		_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DRenderer::DepthStencilView() const
{
	return _dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DRenderer::ResetCommandList()
{
	ThrowIfFailed(
		_commandList->Reset(_commandAllocator.Get(), nullptr)
	);
}

void D3DRenderer::CloseAndExecuteCommandList()
{
	ThrowIfFailed(_commandList->Close());
	ID3D12CommandList* commandLists[] = { _commandList.Get() };
	// TODO - why _countof(commandLists)? We know that here it's
	// always going to be 1, no?
	_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
}

void D3DRenderer::FlushCommandQueue()
{
	// Advance fence value, and then add a new command
	// to the queue to set a new fence point.
	_currentFence++;
	ThrowIfFailed(_commandQueue->Signal(_fence.Get(), _currentFence));

	// Wait until the GPU has completed commands up to the new fence point
	if (_fence->GetCompletedValue() < _currentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(_fence->SetEventOnCompletion(_currentFence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void D3DRenderer::OnResize()
{
	assert(_d3dDevice);
	assert(_swapChain);
	assert(_commandAllocator);

	auto dimensions = _windowManager->Dimensions();
	auto width = dimensions.first;
	auto height = dimensions.second;

	FlushCommandQueue();
	ThrowIfFailed(_commandList->Reset(_commandAllocator.Get(), nullptr));

	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		_swapChainBuffer[i].Reset();
	}

	_depthStencilBuffer.Reset();

	// Resize the swap chain
	ThrowIfFailed(_swapChain->ResizeBuffers(
		SwapChainBufferCount,
		width, height,
		_backBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)
	);

	_currentBackBufferIndex = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; ++i)
	{
		ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_swapChainBuffer[i])));
		_d3dDevice->CreateRenderTargetView(_swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, _RtvDescriptorSize);
	}

	CreateDepthBuffer();

	// Execute the resize commands

	CloseAndExecuteCommandList();
	FlushCommandQueue();

	// TODO - cleanup - do the scissorRects only
	SetupViewport();
	
	_cameraService->OnResize();
	_gui->OnResize();
}

void D3DRenderer::Init_00_CreateDXGIFactory()
{
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory)));
}

void D3DRenderer::Init_01_CreateDevice()
{
	auto hardwareResult = D3D12CreateDevice
	(
		nullptr,					// default adapter
		D3D_FEATURE_LEVEL_12_0,		// min feature level
		IID_PPV_ARGS(&_d3dDevice)	// used to retrive an interface id and corresponding interface pointer
	);
}

void D3DRenderer::Init_02_CreateFence()
{
	ThrowIfFailed
	(
		_d3dDevice->CreateFence(
		0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&_fence)
	)
	);
}

void D3DRenderer::Init_03_QueryDescriptorSizes()
{
	_RtvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_DsvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	_CbvSrvUavDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3DRenderer::Init_04_CheckMSAAQualitySupport()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = _backBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;

	ThrowIfFailed(
		_d3dDevice->CheckFeatureSupport
		(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)
	)
	);

	_4xMsaaQuality = msQualityLevels.NumQualityLevels;
	// Could assert here that quality levels are fine
}

void D3DRenderer::Init_05_CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	// So for the command queue, it looks fine to release _commandQueue and then to
	// get a new one. But for cmd allocator and cmd list, we don't want to release it.
	// I don't really understand; it should be uninitialized in the beginning anyway, no?
	ThrowIfFailed(_d3dDevice->
		CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue)));

	ThrowIfFailed(_d3dDevice->
		CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_commandAllocator.GetAddressOf())));

	ThrowIfFailed(_d3dDevice->
		CreateCommandList(
		0,		// nodeMask
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_commandAllocator.Get(),			// associated command allocator
		nullptr,							// initial PipelineStateObject
		IID_PPV_ARGS(_commandList.GetAddressOf())
	));

	_commandList->Close();
}

void D3DRenderer::Init_06_CreateSwapChain()
{
	_swapChain.Reset();

	auto dimensions = _windowManager->Dimensions();
	auto width = dimensions.first;
	auto height = dimensions.second;

	DXGI_SWAP_CHAIN_DESC sd;

	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = _backBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	sd.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = _windowManager->MainWindowHandle();
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(
		_dxgiFactory->CreateSwapChain
		(
		_commandQueue.Get(),
		&sd,
		_swapChain.GetAddressOf()
	)
	);
}

void D3DRenderer::Init_07_CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;		// clearly, we need as many desc as there are buffers
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	ThrowIfFailed(
		_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(_rtvHeap.GetAddressOf()))
	);

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;		// clearly there's only 1 depth / stencil buffer we need
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;

	ThrowIfFailed(
		_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(_dsvHeap.GetAddressOf()))
	);

	// Q though: why do we have to have the dsv descriptor on a heap,
	// if theres is only one of it?... well, it seems that 'because we do' is the answer
}

void D3DRenderer::Init_08_CreateUIHeap()
{
	// For now: let's just have 1 camera, 1 object
	UINT numDescriptors = FrameResourceCount * 2;
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescriptor;

	cbvHeapDescriptor.NumDescriptors = numDescriptors;
	cbvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescriptor.NodeMask = 0;

	ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&cbvHeapDescriptor, IID_PPV_ARGS(&_uiHeap)));
}

void D3DRenderer::Init_09_BuildUIConstantBufferViews()
{
	auto passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// first 3 views on the heap: pertaining to the UI camera,
	// for each of the 3 frame resources
	// and create one extra, per FR, for our mock ui quad

	for (int frameIndex = 0; frameIndex < FrameResourceCount; ++frameIndex)
	{
		auto uiConstantBuffer = _frameResources[frameIndex]->UIPassConstantBuffer->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uiConstantBuffer->GetGPUVirtualAddress();
		auto heapIndex = frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(_uiHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, _CbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		_d3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}

	const int UI_OBJECT_COUNT = 1;
	auto passCBVoffset = FrameResourceCount;
	auto objectCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(FRObjectConstants));

	// Constant buffer view layout:
	// pass0, pass1, pass2; obj0-F0, obj1-F0 ... objn-F0, obj0-F1 ... objn-F1 ... etc.

	for (int frameIndex = 0; frameIndex < FrameResourceCount; ++frameIndex)
	{
		auto uiConstantBuffer = _frameResources[frameIndex]->UIObjectConstantBuffer->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = uiConstantBuffer->GetGPUVirtualAddress();

		for (int i = 0; i < UI_OBJECT_COUNT; ++i)
		{
			auto heapIndex = passCBVoffset + (UI_OBJECT_COUNT * frameIndex) + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(_uiHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, _CbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress + (i * objectCBByteSize);
			cbvDesc.SizeInBytes = objectCBByteSize;

			_d3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}
}

void D3DRenderer::Init_10_BuildUIRootSignature()
{
	//TODO - there's gotta be a better way of keeping track which
	//		register is used for what. :(
	CD3DX12_ROOT_PARAMETER slotRootParams[2];
	CD3DX12_DESCRIPTOR_RANGE cbvTablePerPass;
	cbvTablePerPass.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);		// per pass to register 0
	slotRootParams[0].InitAsDescriptorTable(1, &cbvTablePerPass);

	CD3DX12_DESCRIPTOR_RANGE cbvTablePerObject;
	cbvTablePerObject.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);		// per object to register 1
	slotRootParams[1].InitAsDescriptorTable(1, &cbvTablePerObject);
	
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParams, 0, nullptr, 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSignature.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);
	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSignature->GetBufferPointer(),
		serializedRootSignature->GetBufferSize(),
		IID_PPV_ARGS(_uiRootSignature.GetAddressOf()))
	);
}

void D3DRenderer::Init_11_BuildUIRenderItems(MeshGeometry* geometries)
{
	//For now, just build 1.
	UIRenderItem quad;

	auto m = Sisu::Matrix4::Identity();
	// Oh bazmeg ez mennyire elbaszott mar

	DirectX::XMFLOAT4X4 xm(m.r0.x, m.r0.y, m.r0.z, m.r0.w,
		m.r1.x, m.r1.y, m.r1.z, m.r1.w,
		m.r2.x, m.r2.y, m.r2.z, m.r2.w,
		m.r3.x, m.r3.y, m.r3.z, m.r3.w);

	DirectX::XMStoreFloat4x4(&quad.World, DirectX::XMLoadFloat4x4(&xm));
	quad.SetCBVIndex(0);
	quad.Geo = geometries;
	quad.PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	SubmeshGeometry q = geometries->drawArgs["quad"];

	quad.IndexCount = q.indexCount;
	quad.StartIndexLocation = q.startIndexLocation;
	quad.BaseVertexLocation = q.baseVertexLocation;

	_uiRenderItems.push_back(quad);
}

void D3DRenderer::Init_12_BuildUIInputLayout()
{
	_uiShaders["VS"] = d3dUtil::CompileShader(L"Shaders\\ui.hlsl", nullptr, "VS", "vs_5_1");
	_uiShaders["PS"] = d3dUtil::CompileShader(L"Shaders\\ui.hlsl", nullptr, "PS", "ps_5_1");

	// _inputLayout is a std::vector<D3D12_INPUT_ELEMENT_DESC>; signature:
	// semantic name, semantic index, format, input slot, aligned byte offset, input slot class, instance data step rate 
	// about input slots: there are 16 of them (0-15), through which you can feed vertex data to the GPU
	// and notice that the order here doesn't matter; what matters is that the aligned byte offset
	// corresponds to the actual layout of the vertex struct (first position, then color)

	_uiInputLayout =
	{
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		}
	};
}

void D3DRenderer::Init_13_BuildUIPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC uiPSOdesc;
	ZeroMemory(&uiPSOdesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	uiPSOdesc.InputLayout = { _uiInputLayout.data(), (UINT)_uiInputLayout.size() };
	uiPSOdesc.pRootSignature = _uiRootSignature.Get();
	uiPSOdesc.VS =
	{
		reinterpret_cast<BYTE*>(_uiShaders["VS"]->GetBufferPointer()),
		_uiShaders["VS"]->GetBufferSize()
	};

	uiPSOdesc.PS =
	{
		reinterpret_cast<BYTE*>(_uiShaders["PS"]->GetBufferPointer()),
		_uiShaders["PS"]->GetBufferSize()
	};

	uiPSOdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	uiPSOdesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	uiPSOdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	uiPSOdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	uiPSOdesc.SampleMask = UINT_MAX;
	uiPSOdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	uiPSOdesc.NumRenderTargets = 1;
	uiPSOdesc.RTVFormats[0] = _backBufferFormat;
	uiPSOdesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	uiPSOdesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
	uiPSOdesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&uiPSOdesc, IID_PPV_ARGS(&_uiPSO)));
}

void D3DRenderer::CreateDepthBuffer()
{
	auto dimensions = _windowManager->Dimensions();
	auto width = dimensions.first;
	auto height = dimensions.second;

	D3D12_RESOURCE_DESC bufferDesc;
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = width;
	bufferDesc.Height = height;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;

	// We need typeless format because SSAO requires an SRV to read from
	// the depth buffer (also shadow mapping, right?). So we'll need
	// two views to the same resource => the resource should have a
	// typeless format.

	bufferDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	bufferDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	bufferDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = _depthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(_depthStencilBuffer.GetAddressOf())
	)
	);

	// Create descriptor to mip level 0 of entire resource, using the format of the resource
	// Because the resource is typeless, we need dsvDesc; otherwise in the call
	// to CreateDepthStencilView the 2nd parameter could be nullptr
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = _depthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	_d3dDevice->CreateDepthStencilView(_depthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	// Transition the resource from its initial state to be used as a depth buffer

	_commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(_depthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

void D3DRenderer::SetupViewport()
{
	auto dimensions = _windowManager->Dimensions();
	auto width = dimensions.first;
	auto height = dimensions.second;

	_screenViewport.TopLeftX = 0;
	_screenViewport.TopLeftY = 0;
	_screenViewport.Width = static_cast<float>(width) / 2.0f;
	_screenViewport.Height = static_cast<float>(height);
	_screenViewport.MinDepth = 0.0f;
	_screenViewport.MaxDepth = 1.0f;

	_viewports.push_back(_screenViewport);
	_viewports.push_back(_screenViewport);

	_viewports[1].TopLeftX = static_cast<float>(width / 2.0f);

	_scissorRect = { 0, 0, width, height };
}

void D3DRenderer::UpdateUIPassBuffer(const GameTimer& gt, const D3DCamera& uiCamera)
{
	DirectX::XMMATRIX viewMatrix = DirectX::XMLoadFloat4x4(&uiCamera.ViewMatrix());
	DirectX::XMMATRIX projectionMatrix = DirectX::XMLoadFloat4x4(&uiCamera.ProjectionMatrix());

	DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(viewMatrix, projectionMatrix);
	DirectX::XMMATRIX inverseView = DirectX::XMMatrixInverse(&DirectX::XMMatrixDeterminant(viewMatrix), viewMatrix);
	DirectX::XMMATRIX inverseProj = DirectX::XMMatrixInverse(&DirectX::XMMatrixDeterminant(projectionMatrix), projectionMatrix);
	DirectX::XMMATRIX inverseViewProj = DirectX::XMMatrixInverse(&DirectX::XMMatrixDeterminant(viewProj), viewProj);

	DirectX::XMStoreFloat4x4(&_uiPassConstants.view, DirectX::XMMatrixTranspose(viewMatrix));
	DirectX::XMStoreFloat4x4(&_uiPassConstants.invView, DirectX::XMMatrixTranspose(inverseView));
	DirectX::XMStoreFloat4x4(&_uiPassConstants.proj, DirectX::XMMatrixTranspose(projectionMatrix));
	DirectX::XMStoreFloat4x4(&_uiPassConstants.invProj, DirectX::XMMatrixTranspose(inverseProj));
	DirectX::XMStoreFloat4x4(&_uiPassConstants.viewProj, DirectX::XMMatrixTranspose(viewProj));
	DirectX::XMStoreFloat4x4(&_uiPassConstants.invViewProj, DirectX::XMMatrixTranspose(inverseViewProj));

	_uiPassConstants.eyePosW = uiCamera.Position();

	auto windowDimensions = _windowManager->Dimensions();
	_uiPassConstants.renderTargetSize = DirectX::XMFLOAT2((float)windowDimensions.first, (float)windowDimensions.second);
	_uiPassConstants.invRenderTargetSize = DirectX::XMFLOAT2(1.0f / windowDimensions.first, 1.0f / windowDimensions.second);
	_uiPassConstants.nearZ = 1.0f;
	_uiPassConstants.farZ = 1000.0f;
	_uiPassConstants.totalTime = gt.SecondsSinceReset();
	_uiPassConstants.deltaTime = gt.DeltaTimeSeconds();

	auto currPassCB = _currentFrameResource->UIPassConstantBuffer.get();
	currPassCB->CopyData(uiCamera.CbvIndex(), _uiPassConstants);
}

void D3DRenderer::UpdateUIInstanceData()
{
	//TODO - keeping track of dirty frames
	auto currentInstanceBuffer = _currentFrameResource->UIObjectConstantBuffer.get();
	UINT bufferIndex = 0;
	for (auto& renderItem : _uiRenderItems)
	{
		DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&renderItem.World);
		FRObjectConstants objConstants(worldMatrix);
		objConstants.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		objConstants.localScale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
		currentInstanceBuffer->CopyData(bufferIndex++, objConstants);
	}
}

void D3DRenderer::DrawUI(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetPipelineState(_uiPSO.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { _uiHeap.Get() };
	_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	_commandList->SetGraphicsRootSignature(_uiRootSignature.Get());

	auto guiCamera = _gui->GetCamera();
	auto cameraCBVhandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(_uiHeap->GetGPUDescriptorHandleForHeapStart());
	cameraCBVhandle.Offset(guiCamera.CbvIndex(), _CbvSrvUavDescriptorSize);

	_commandList->RSSetViewports(1, &guiCamera.viewport);
	_commandList->SetGraphicsRootDescriptorTable(0, cameraCBVhandle);	// 0-> per pass => camera.

	// ... this is where we'd call "DrawAllUIRenderItems". But for now:
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(FRObjectConstants));
	auto objectCB = _currentFrameResource->UIObjectConstantBuffer->Resource();

	auto mockUI = _uiRenderItems[0];

	_commandList->IASetVertexBuffers(0, 1, &mockUI.Geo->GetVertexBufferView());
	_commandList->IASetIndexBuffer(&mockUI.Geo->GetIndexBufferView());
	_commandList->IASetPrimitiveTopology(mockUI.PrimitiveType);

	auto objectCBVindex = mockUI.GetCBVIndex();
	auto perPassOffset = FrameResourceCount;
	auto perFrameOffset = _uiRenderItems.size() * _currentFrameResourceIndex;

	auto cbvIndex = perPassOffset + perFrameOffset + objectCBVindex;
	auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(_uiHeap->GetGPUDescriptorHandleForHeapStart());
	cbvHandle.Offset(cbvIndex, _CbvSrvUavDescriptorSize);

	_commandList->SetGraphicsRootDescriptorTable(1, cbvHandle);			// 1-> per object stuff
	_commandList->DrawIndexedInstanced(mockUI.IndexCount, 1, mockUI.StartIndexLocation,
									   mockUI.BaseVertexLocation, 0);
}

void D3DRenderer::WaitForNextFrameResource()
{
	_currentFrameResourceIndex = (_currentFrameResourceIndex + 1) % FrameResourceCount;
	_currentFrameResource = _frameResources[_currentFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current
	// frame resource? If not, wait until the GPU has completed commands
	// up to this fence point.

	if (_currentFrameResource->fence != 0 && _fence->GetCompletedValue() < _currentFrameResource->fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(
			_fence->SetEventOnCompletion(_currentFrameResource->fence, eventHandle)
		);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}