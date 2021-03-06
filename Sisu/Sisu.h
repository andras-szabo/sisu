#pragma once

#include "resource.h"
#include <string>
#include "WindowManager.h"
#include "GameTimer.h"
#include "IRenderer.h"
#include "IInputService.h"
#include "Arena.h"
#include "GameObject.h"
#include "TransformUpdateSystem.h"
#include "ICameraService.h"
#include "IGUIService.h"

class SisuApp
{
	friend class WindowManager;
	friend class std::unique_ptr<SisuApp>;

public:
	const static std::size_t MaxGameObjectCount = 4096;

	SisuApp(HINSTANCE hInstance) : _hAppInstance(hInstance) {}
	SisuApp(SisuApp&& other) = default;
	SisuApp& operator=(SisuApp&& other) = default;

	virtual ~SisuApp() {}

protected:
	SisuApp(const SisuApp&) = delete;								// copy ctor, copy assignment: not allowed
	SisuApp& operator=(const SisuApp&) = delete;

public:
	HINSTANCE GetAppInstanceHandle() const { PI; return _hAppInstance; }
	HWND MainWindowHandle() const { return _windowManager->MainWindowHandle(); }
	float AspectRatio() const { return _windowManager->AspectRatio(); }
	bool IsRendererSetup() const { return _renderer != nullptr && _renderer->IsSetup(); }

	virtual bool Init(int width, int height, const std::wstring& title);
	virtual void Pause(bool newState);

	int Run();

protected:
	virtual void OnResize();
	virtual void Update();
	virtual std::size_t Draw();	// return the number of draw calls
	virtual void PostDraw();

	bool InitArenas();
	bool InitGameTimer();

	bool InitInputService(GameTimer* const gt);
	bool InitWindowManager(IInputService* const inputService, int width, int height, const std::wstring& title);
	bool InitCameraService(IInputService* const inputService, WindowManager* const windowManager);
	bool InitRenderer(WindowManager* const windowManager, GameTimer* const gt,
						Arena<GameObject>* const arena, ICameraService* const cameraService);

	bool InitGUIService(IInputService* const inputService, WindowManager* const windowManager, 
						ICameraService* const camService, IRenderer* const renderer);

	bool InitTransformUpdateSystem();
	void CalculateFrameStats(std::size_t drawCallCount);

protected:
	HINSTANCE _hAppInstance = nullptr;

	std::unique_ptr<Arena<GameObject>> _gameObjects;

	std::unique_ptr<GameTimer> _gameTimer;
	std::unique_ptr<WindowManager> _windowManager;
	std::unique_ptr<IRenderer> _renderer;
	std::unique_ptr<IInputService> _inputService;
	std::unique_ptr<ICameraService> _cameraService;
	std::unique_ptr<IGUIService> _gui;

	std::unique_ptr<TransformUpdateSystem> _transformUpdateSystem;

	bool _isRendererSetup = false;
};
