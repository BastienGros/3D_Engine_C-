#include "EngineCore.h"

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const wchar_t* windowClassName = L"DX12WindowClass";

    PowerEngine::Core::EngineCore::CommandLineParsing();
    PowerEngine::Core::EngineCore::Enable_DebugD3D12_Layer();

    g_TearingSupported = PowerEngine::Core::EngineCore::CheckTearingSupport();
    
    PowerEngine::Core::EngineCore::RegisterWindowClass(hInstance, windowClassName);
    g_WindowHandle = PowerEngine::Core::EngineCore::CreateWindow(windowClassName, hInstance, L"D3D12 ENGINE", g_ClientWidth, g_ClientHeight);

    ::GetWindowRect(g_WindowHandle, &g_WindowRect);

    PowerEngine::Core::EngineCore EngineCore;

    PowerEngine::Core::EngineCore::UpdateRenderTargetViews(EngineCore.Device(), EngineCore.SwapChain(), EngineCore.DescriptorHeap());

    g_IsInitialized = true;

    ::ShowWindow(g_WindowHandle, SW_SHOW);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    PowerEngine::Core::EngineCore::flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);

    ::CloseHandle(g_FenceEvent);

    return 0;
    
}

