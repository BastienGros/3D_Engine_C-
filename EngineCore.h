#pragma once

#include "Header.h"
#include "HelperFile.h"


#ifndef EngineCore_h
#define EngineCore_H

// Global Variables to see the state of the engine //

const std::uint8_t g_NumFrames = 3;
bool g_UseWarp = false;

std::uint16_t g_ClientWidth = 1920;
std::uint16_t g_ClientHeight = 1080;

// By default, enable V-Sync.
// Can be toggled with the V key.

bool g_VSync = true;
bool g_IsInitialized = false;
bool g_TearingSupported = false;

HWND g_WindowHandle;
RECT g_WindowRect;

namespace PowerEngine {

	namespace Core {

		class EngineCore
		{
		public:

			EngineCore();
			~EngineCore();

			static void update();
			static void render();

			// helper function //

			static void CommandLineParsing();
			static void Enable_DebugD3D12_Layer();
			static HANDLE CreateEventHandle();

			static void SetFullScreen(bool fullscreen);

			static void RegisterWindowClass(HINSTANCE Inst, const wchar_t* windowClassName);
			static HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst, const wchar_t* windowTitle, std::uint16_t width, std::uint16_t height);

			static void ResizeWindow(std::uint32_t width, std::uint32_t height);	
			// Control Function //

			static std::uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, std::uint64_t& fenceValue);
			static void WaitFenceValue(ComPtr<ID3D12Fence> fence, std::uint64_t fenceValue, HANDLE fenceEvent, std::chrono::milliseconds duration = std::chrono::milliseconds::max());
			static void flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, std::uint64_t& fenceValue, HANDLE fenceEvent);

			static void UpdateRenderTargetViews(ComPtr<ID3D12Device2>device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap);
			static bool CheckTearingSupport();

			// Getters // 

			ComPtr<ID3D12Device2> Device();
			ComPtr<IDXGISwapChain4> SwapChain();
			ComPtr<ID3D12DescriptorHeap> DescriptorHeap();
			ComPtr<ID3D12CommandQueue> CommandQueue();
			ComPtr<ID3D12Fence> Fence();
			std::uint64_t FenceValue();


		private :
			// Device Creation Function //

			ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
			ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);
			ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
			ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
			ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type);
			ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device);

			ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue>commandQueue, std::uint32_t width, std::uint32_t height, std::uint32_t bufferCount);
			ComPtr<ID3D12DescriptorHeap> CreateDescriptor(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t numDescriptors);

			// CALLBACK //

			static LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);


		private :

			static ComPtr<ID3D12Device2> m_Device;
			static ComPtr<ID3D12CommandQueue> m_CommandQueue;
			static ComPtr<IDXGISwapChain4> m_SwapChain;
			static ComPtr<ID3D12GraphicsCommandList> m_CommandList;
			static ComPtr<ID3D12CommandAllocator> m_CommandAllocators[g_NumFrames];
			static ComPtr<ID3D12Resource> m_BackBuffers[g_NumFrames];
			static ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
			static UINT m_RTVDescriptorSize;
			static UINT m_CurrentBackBufferIndex;

			// Synchronization objects //

			static ComPtr<ID3D12Fence> m_Fence;
			static std::uint64_t m_FenceValue;
			static std::uint64_t m_FrameFenceValues[g_NumFrames];
			static HANDLE m_FenceEvent;

			// SwapChain Variables // 

			static bool m_Fullscreen;
			

		};

		


		
	}
}

#endif
