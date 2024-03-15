#include "EngineCore.h"

namespace PowerEngine
{
	namespace Core
	{
		EngineCore::EngineCore()
		{
			ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);

			m_Device = CreateDevice(dxgiAdapter4);
			m_CommandQueue = CreateCommandQueue(m_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
			m_SwapChain = CreateSwapChain(g_WindowHandle, m_CommandQueue, g_ClientWidth, g_ClientHeight, g_NumFrames);
			m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
			m_RTVDescriptorHeap = CreateDescriptor(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
			m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			for (int i = 0; i < g_NumFrames; ++i)
			{
				m_CommandAllocators[i] = CreateCommandAllocator(m_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
			}

			m_CommandList = CreateCommandList(m_Device, m_CommandAllocators[m_CurrentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);
			m_Fence = CreateFence(m_Device);
			m_FenceEvent = CreateEventHandle();

		}
		EngineCore::~EngineCore()
		{

		}
		void EngineCore::update()
		{
			static std::uint64_t frameCounter = 0;
			static double elapsedSeconds = 0.0;
			static std::chrono::high_resolution_clock clock;
			static auto t0 = clock.now();

			frameCounter++;
			auto t1 = clock.now();
			auto deltaTime = t1 - t0;
			t0 = t1;

			elapsedSeconds += deltaTime.count() * 1e-9; // convert the deltaTime from nanoseconds into seconds
			if (elapsedSeconds > 1.0)
			{
				char buffer[500];
				auto fps = frameCounter / elapsedSeconds;
				sprintf_s(buffer, 500, "FPS: %f\n", fps);
				OutputDebugString(buffer);

				frameCounter = 0;
				elapsedSeconds = 0.0;
			}
		}
		void EngineCore::render()
		{
			// Retrieve the current pointers of the command allocator and back buffer resource according to the current back buffer index. //

			auto commandAllocator = m_CommandAllocators[m_CurrentBackBufferIndex];
			auto backBuffer = m_BackBuffers[m_CurrentBackBufferIndex];

			// Preaparing the command allocator and the command list for the next frame. //

			commandAllocator->Reset();
			m_CommandList->Reset(commandAllocator.Get(), nullptr);

			// Clear the render target. //
			{
				/* 
				   To build correctly the transition barrier, 
				   the before and next state must be known before hand.
				   That's why it's hard coded here.
				*/
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), 
					D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

				m_CommandList->ResourceBarrier(1, &barrier);

				FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
					m_CurrentBackBufferIndex, m_RTVDescriptorSize);

				m_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

				 
				
				// Before presenting, the back buffer resource must be transitioned to the present state //

				
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						backBuffer.Get(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
				m_CommandList->ResourceBarrier(1, &barrier);
				
				ThrowIfFailed(m_CommandList->Close());

				ID3D12CommandList* const commandLists[] = {m_CommandList.Get()};
				m_CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

				UINT syncInterval = g_VSync ? 1 : 0;
				UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
				ThrowIfFailed(m_SwapChain->Present(syncInterval, presentFlags));

				m_FrameFenceValues[m_CurrentBackBufferIndex] = Signal(m_CommandQueue, m_Fence, m_FenceValue);

				m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

				WaitFenceValue(m_Fence, m_FrameFenceValues[m_CurrentBackBufferIndex], m_FenceEvent);
			
			}

		}
		void EngineCore::CommandLineParsing()
		{
			int argc;
			wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

			for (size_t i = 0; i < argc; i++)
			{
				if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
				{
					g_ClientWidth = ::wcstol(argv[++i], nullptr, 10);
				}
				if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
				{
					g_ClientHeight = ::wcstol(argv[++i], nullptr, 10);
				}
				if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
				{
					g_UseWarp = true;
				}
			}
		}

		void EngineCore::Enable_DebugD3D12_Layer()
		{
			#if defined(_DEBUG)
			ComPtr<ID3D12Debug> debugInterface;
			ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
			debugInterface->EnableDebugLayer();
			#endif
		}

		HANDLE EngineCore::CreateEventHandle()
		{
			HANDLE fenceEvent;

			fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
			assert(fenceEvent && "Failed to create fence event.");

			return fenceEvent;
		}

		void EngineCore::ResizeWindow(std::uint32_t width, std::uint32_t height)
		{
			if (g_ClientWidth != width || g_ClientHeight != height)
			{
				// Don't allow 0 size swap chain back buffers.

				g_ClientWidth = std::max(1u, width);
				g_ClientHeight = std::max(1u, height);

				// Flush the GPU queue to make sure the swap chain's back buffers
				// are not being referenced by an in-flight command list.

				flush(m_CommandQueue, m_Fence, m_FenceValue, m_FenceEvent);

				for (int i = 0; i < g_NumFrames; ++i)
				{
					// Any references to the back buffers must be released
					// before the swap chain can be resized.

					m_BackBuffers[i].Reset();
					m_FrameFenceValues[i] = m_FrameFenceValues[m_CurrentBackBufferIndex];
				}

				DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
				ThrowIfFailed(m_SwapChain->GetDesc(&swapChainDesc));
				ThrowIfFailed(m_SwapChain->ResizeBuffers(g_NumFrames, g_ClientWidth, g_ClientHeight,
					swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

				m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

				UpdateRenderTargetViews(m_Device, m_SwapChain, m_RTVDescriptorHeap);
			}
		}

		std::uint64_t EngineCore::Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, std::uint64_t& fenceValue)
		{
			std::uint64_t fenceValueForSignal = ++fenceValue;
			ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

			return fenceValueForSignal;
		}

		void EngineCore::WaitFenceValue(ComPtr<ID3D12Fence> fence, std::uint64_t fenceValue, HANDLE fenceEvent, std::chrono::milliseconds duration)
		{
			if (fence->GetCompletedValue() < fenceValue)
			{
				ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
				::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
			}
		}

		void EngineCore::flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, std::uint64_t& fenceValue, HANDLE fenceEvent)
		{
			uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
			WaitFenceValue(fence, fenceValueForSignal, fenceEvent);
		}

		ComPtr<IDXGIAdapter4> EngineCore::GetAdapter(bool useWarp)
		{
			ComPtr<IDXGIFactory4> dxgiFactory;
			UINT createFactoryFlags = 0;
			#if defined(_DEBUG)
			createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
			#endif

			ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

			ComPtr<IDXGIAdapter1> dxgiAdapter1;
			ComPtr<IDXGIAdapter4> dxgiAdapter4;

			if (useWarp)
			{
				ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
			else
			{
				SIZE_T maxDedicatedVideoMemory = 0;
				for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
				{
					DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
					dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

					// Check to see if the adapter can create a D3D12 device without actually 
					// creating it. The adapter with the largest dedicated video memory
					// is favored.
					if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
						SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
							D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
						dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
					{
						maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
						ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
					}
				}
			}
		}

		ComPtr<ID3D12Device2> EngineCore::CreateDevice(ComPtr<IDXGIAdapter4> adapter)
		{
			ComPtr<ID3D12Device2> d3d12Device2;
			ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

			#if defined(_DEBUG)
			ComPtr<ID3D12InfoQueue> pInfoQueue;
			if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
			{
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

				// Suppress messages based on their severity level
				D3D12_MESSAGE_SEVERITY Severities[] =
				{
					D3D12_MESSAGE_SEVERITY_INFO
				};

				// Suppress individual messages by their ID
				D3D12_MESSAGE_ID DenyIds[] = {
					D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   
					D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                        
					D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       
				};

				D3D12_INFO_QUEUE_FILTER NewFilter = {};
				//NewFilter.DenyList.NumCategories = _countof(Categories);
				//NewFilter.DenyList.pCategoryList = Categories;
				NewFilter.DenyList.NumSeverities = _countof(Severities);
				NewFilter.DenyList.pSeverityList = Severities;
				NewFilter.DenyList.NumIDs = _countof(DenyIds);
				NewFilter.DenyList.pIDList = DenyIds;

				ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
			}
			#endif

			return d3d12Device2;
		}

		ComPtr<ID3D12CommandQueue> EngineCore::CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
		{
			ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

			D3D12_COMMAND_QUEUE_DESC desc = {};
			desc.Type = type;
			desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			desc.NodeMask = 0;

			ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

			return d3d12CommandQueue;
		}

		ComPtr<ID3D12CommandAllocator> EngineCore::CreateCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
		{
			ComPtr<ID3D12CommandAllocator> commandAllocator;
			ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

			return commandAllocator;
		}

		ComPtr<ID3D12GraphicsCommandList> EngineCore::CreateCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
		{
			ComPtr<ID3D12GraphicsCommandList> commandList;
			ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

			ThrowIfFailed(commandList->Close());

			return commandList;
		}

		ComPtr<ID3D12Fence> EngineCore::CreateFence(ComPtr<ID3D12Device2> device)
		{
			ComPtr<ID3D12Fence> fence;

			ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

			return fence;
		}

		LRESULT EngineCore::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (g_IsInitialized)
			{
				switch (message)
				{
				case WM_PAINT:

					update();
					render();

					break;

				case WM_SYSKEYDOWN:
				case WM_KEYDOWN:
				{
					bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

					switch (wParam)
					{
					case 'V':
						g_VSync = !g_VSync;
						break;
					case VK_ESCAPE:
						::PostQuitMessage(0);
						break;
					case VK_RETURN:
						if (alt)
						{
					case VK_F11:
						SetFullScreen(!m_Fullscreen);
						}
						break;
					}
				}
				break;
				// The default window procedure will play a system notification sound 
				// when pressing the Alt+Enter keyboard combination if this message is 
				// not handled.
				case WM_SYSCHAR:
					break;

				case WM_SIZE:
				{
					RECT clientRect = {};
					::GetClientRect(g_WindowHandle, &clientRect);

					int width = clientRect.right - clientRect.left;
					int height = clientRect.bottom - clientRect.top;

					ResizeWindow(width, height);
				}
				break;

				case WM_DESTROY:
					::PostQuitMessage(0);
					break;
				default:
					return ::DefWindowProcW(hwnd, message, wParam, lParam);
				}
			}
			else
			{
				return ::DefWindowProcW(hwnd, message, wParam, lParam);
			}

			return 0;
		}

		void EngineCore::SetFullScreen(bool fullscreen)
		{
			if (m_Fullscreen != fullscreen)
			{
				m_Fullscreen = fullscreen;

				if (m_Fullscreen) // Switching to fullscreen.
				{
					// Store the current window dimensions so they can be restored when switching out of fullscreen state //
					::GetWindowRect(g_WindowHandle, &g_WindowRect);

					// Set the window style to a borderless window so the client area fills the entire screen //
					UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

					::SetWindowLongW(g_WindowHandle, GWL_STYLE, windowStyle);

					// Query the name of the nearest display device for the window.
					// This is required to set the fullscreen dimensions of the window
					// when using a multi-monitor setup

					HMONITOR hMonitor = ::MonitorFromWindow(g_WindowHandle, MONITOR_DEFAULTTONEAREST);
					MONITORINFOEX monitorInfo = {};
					monitorInfo.cbSize = sizeof(MONITORINFOEX);
					::GetMonitorInfo(hMonitor, &monitorInfo);

					::SetWindowPos(g_WindowHandle, HWND_TOP,
						monitorInfo.rcMonitor.left,
						monitorInfo.rcMonitor.top,
						monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
						monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
						SWP_FRAMECHANGED | SWP_NOACTIVATE);

					::ShowWindow(g_WindowHandle, SW_MAXIMIZE);
				}
				else
				{
					// Restore all the window decorators.
					::SetWindowLong(g_WindowHandle, GWL_STYLE, WS_OVERLAPPEDWINDOW);

					::SetWindowPos(g_WindowHandle, HWND_NOTOPMOST,
						g_WindowRect.left,
						g_WindowRect.top,
						g_WindowRect.right - g_WindowRect.left,
						g_WindowRect.bottom - g_WindowRect.top,
						SWP_FRAMECHANGED | SWP_NOACTIVATE);

					::ShowWindow(g_WindowHandle, SW_NORMAL);
				}
			}
		}

		void EngineCore::RegisterWindowClass(HINSTANCE Inst, const wchar_t* windowClassName)
		{
			// Register a window class for creating our render window with.
			WNDCLASSEXW windowClass = {};

			windowClass.cbSize = sizeof(WNDCLASSEX);
			windowClass.style = CS_HREDRAW | CS_VREDRAW;
			windowClass.lpfnWndProc = &WndProc;
			windowClass.cbClsExtra = 0;
			windowClass.cbWndExtra = 0;
			windowClass.hInstance = Inst;
			windowClass.hIcon = ::LoadIcon(Inst, NULL);
			windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
			windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
			windowClass.lpszMenuName = NULL;
			windowClass.lpszClassName = windowClassName;
			windowClass.hIconSm = ::LoadIcon(Inst, NULL);

			static ATOM atom = ::RegisterClassExW(&windowClass);
			assert(atom > 0);
		}

		HWND EngineCore::CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst, const wchar_t* windowTitle, std::uint16_t width, std::uint16_t height)
		{
			int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
			int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

			RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

			int windowWidth = windowRect.right - windowRect.left;
			int windowHeight = windowRect.bottom - windowRect.top;

			// Center the window within the screen. Clamp to 0, 0 for the top-left corner.
			int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
			int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

			HWND hWnd = ::CreateWindowExW(
				NULL,
				windowClassName,
				windowTitle,
				WS_OVERLAPPEDWINDOW,
				windowX,
				windowY,
				windowWidth,
				windowHeight,
				NULL,
				NULL,
				hInst,
				nullptr
			);

			assert(hWnd && "Failed to create window");

			return hWnd;
		}

		ComPtr<IDXGISwapChain4> EngineCore::CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, std::uint32_t width, std::uint32_t height, std::uint32_t bufferCount)
		{
			ComPtr<IDXGISwapChain4> dxgiSwapChain4;
			ComPtr<IDXGIFactory4> dxgiFactory4;
			UINT createFactoryFlags = 0;
#if defined(_DEBUG)
			createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

			ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChainDesc.Width = width;
			swapChainDesc.Height = height;
			swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.Stereo = FALSE;
			swapChainDesc.SampleDesc = { 1, 0 };
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.BufferCount = bufferCount;
			swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

			ComPtr<IDXGISwapChain1> swapChain1;
			ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
				commandQueue.Get(),
				hWnd,
				&swapChainDesc,
				nullptr,
				nullptr,
				&swapChain1));

			// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
			// will be handled manually.
			ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

			ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

			return dxgiSwapChain4;
		}

		ComPtr<ID3D12DescriptorHeap> EngineCore::CreateDescriptor(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t numDescriptors)
		{
			ComPtr<ID3D12DescriptorHeap> descriptorHeap;

			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.NumDescriptors = numDescriptors;
			desc.Type = type;

			ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

			return descriptorHeap;
		}

		void EngineCore::UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
		{
			auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

			for (int i = 0; i < g_NumFrames; ++i)
			{
				ComPtr<ID3D12Resource> backBuffer;
				ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

				device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

				m_BackBuffers[i] = backBuffer;

				rtvHandle.Offset(rtvDescriptorSize);
			}
		}

		bool EngineCore::CheckTearingSupport()
		{

			BOOL allowTearing = FALSE;

			ComPtr<IDXGIFactory4> factory4;
			if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
			{
				ComPtr<IDXGIFactory5> factory5;
				if (SUCCEEDED(factory4.As(&factory5)))
				{
					if (FAILED(factory5->CheckFeatureSupport(
						DXGI_FEATURE_PRESENT_ALLOW_TEARING,
						&allowTearing, sizeof(allowTearing))))
					{
						allowTearing = FALSE;
					}
				}
			}

			return allowTearing == TRUE;

		}

		ComPtr<ID3D12Device2> EngineCore::Device()
		{
			return m_Device;
		}

		ComPtr<IDXGISwapChain4> EngineCore::SwapChain()
		{
			return m_SwapChain;
		}

		ComPtr<ID3D12DescriptorHeap> EngineCore::DescriptorHeap()
		{
			return m_RTVDescriptorHeap;
		}

		ComPtr<ID3D12CommandQueue> EngineCore::CommandQueue()
		{
			return m_CommandQueue;
		}

		ComPtr<ID3D12Fence> EngineCore::Fence()
		{
			return m_Fence;
		}

		std::uint64_t EngineCore::FenceValue()
		{
			return std::uint64_t();
		}

	}
}

