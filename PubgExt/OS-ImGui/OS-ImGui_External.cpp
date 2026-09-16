#include "OS-ImGui_External.h"
#include <iomanip>
#include <random>
#include <sstream>
#include <ctime>
#include "DiscordHijack.h"

// Forward-declare the global logging function (defined in Main.cpp)
extern void BaseLog(const char* fmt, ...);
#ifndef LOG
#define LOG(fmt, ...) BaseLog(fmt, ##__VA_ARGS__)
#endif

// Generate random window class name that looks like a system class
static std::string GenerateRandomClassName() {
  const char *prefixes[] = {"Windows", "DirectUI", "Shell",    "Mso",
                            "Afx",     "Static",   "Button",   "ComboBox",
                            "Edit",    "ListBox",  "ScrollBar", "MSCTLS",
                            "SysLink", "ToolTips", "Rebar",    "MSTask"};

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> prefixDist(
      0, sizeof(prefixes) / sizeof(prefixes[0]) - 1);
  std::uniform_int_distribution<> numDist(1000, 9999);

  std::stringstream ss;
  ss << prefixes[prefixDist(gen)] << numDist(gen);
  return ss.str();
}

// Generate random window title that looks like a system utility
static std::string GenerateRandomWindowName() {
  const char *names[] = {
    "Windows Shell Experience Host",
    "Microsoft Text Input Application",
    "Program Manager",
    "Desktop Window Manager",
    "Settings",
    "Windows Security",
    "Microsoft Store",
    "Windows Notification Platform"
  };
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, sizeof(names)/sizeof(names[0]) - 1);
  return names[dist(gen)];
}

// D3D11 Device
namespace RenderCore {
bool D3DDevice::CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_0,
  };
  HRESULT res = D3D11CreateDeviceAndSwapChain(
      NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
      featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
      &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software
                                     // driver if hardware is not available.
    res = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray,
        2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
        &g_pd3dDeviceContext);
  if (res != S_OK)
    return false;

  CreateRenderTarget();
  return true;
}

void D3DDevice::CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = NULL;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = NULL;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = NULL;
  }
}

void D3DDevice::CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  if (pBackBuffer == nullptr)
    return;
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL,
                                       &g_mainRenderTargetView);
  pBackBuffer->Release();
}

void D3DDevice::CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = NULL;
  }
}
} // namespace RenderCore

// OSImGui External
namespace RenderCore {

LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam,
                                LPARAM lParam);

void OSImGui_External::NewWindow(std::string WindowName, Vec2 WindowSize,
                                 std::function<void()> CallBack) {
  if (!CallBack)
    throw OSException("CallBack is empty");
  if (WindowName.empty())
    Window.Name = "Window";

  Window.Name = WindowName;
  Window.wName = StringToWstring(Window.Name);
  Window.ClassName = GenerateRandomClassName(); // Randomized window class name
  Window.wClassName = StringToWstring(Window.ClassName);
  Window.Size = WindowSize;

  Type = NEW;
  CallBackFn = CallBack;

  if (!CreateMyWindow())
    throw OSException("CreateMyWindow() call failed");

  try {
    InitImGui(g_Device.g_pd3dDevice, g_Device.g_pd3dDeviceContext);
  } catch (OSException &e) {
    throw e;
  }

  MainLoop();
}

void OSImGui_External::AttachAnotherWindow(std::string DestWindowName,
                                           std::string DestWindowClassName,
                                           std::function<void()> CallBack) {
  if (!CallBack)
    throw OSException("CallBack is empty");

  // Wait for and find Discord's overlay HWND with retry loop
  int retries = 0;
  while (true) {
      Window.hWnd = DiscordHijack::FindDiscordOverlay();
      if (Window.hWnd) {
          std::cout << "[+] Found Discord Overlay matching HWND.\n";
          break;
      }
      
      retries++;
      if (retries % 5 == 0) {
          std::cout << "[-] Waiting for Discord Overlay... Make sure it's enabled and game is in borderless mode.\n";
      }
      Sleep(1000);
  }

  // Diagnostics: print HWND info and window styles
  LONG exStyle = GetWindowLong(Window.hWnd, GWL_EXSTYLE);
  LONG style = GetWindowLong(Window.hWnd, GWL_STYLE);
  RECT wndRect;
  GetWindowRect(Window.hWnd, &wndRect);
  LOG("[DIAG] Discord HWND: 0x%p\n", (void*)Window.hWnd);
  LOG("[DIAG] Window Style: 0x%08lX | ExStyle: 0x%08lX\n", style, exStyle);
  LOG("[DIAG] Window Rect: %d,%d -> %d,%d\n", wndRect.left, wndRect.top, wndRect.right, wndRect.bottom);
  LOG("[DIAG] IsWindowVisible: %d\n", IsWindowVisible(Window.hWnd));

  // CRITICAL: Force Discord overlay visible (matches R6External Overlay.h)
  UpdateWindow(Window.hWnd);
  ShowWindow(Window.hWnd, SW_SHOW);

  // Remove WS_EX_LAYERED — it uses GDI layered bitmap compositing which
  // IGNORES D3D swap chain content. We need DWM glass compositing instead.
  exStyle &= ~WS_EX_LAYERED;
  SetWindowLong(Window.hWnd, GWL_EXSTYLE, exStyle);
  LOG("[DIAG] Removed WS_EX_LAYERED. New ExStyle: 0x%08lX\n", GetWindowLong(Window.hWnd, GWL_EXSTYLE));

  // Enable DWM glass composition — this makes D3D content visible with
  // per-pixel alpha transparency. {-1} = extend glass to entire client area.
  MARGINS margins = { -1, -1, -1, -1 };
  DwmExtendFrameIntoClientArea(Window.hWnd, &margins);
  LOG("[DIAG] DwmExtendFrameIntoClientArea applied.\n");

  LOG("[DIAG] After ShowWindow - IsWindowVisible: %d\n", IsWindowVisible(Window.hWnd));

  // Get screen dimensions — Discord overlay is fullscreen
  Window.Size = Vec2((float)GetSystemMetrics(SM_CXSCREEN), (float)GetSystemMetrics(SM_CYSCREEN));
  Window.Pos = Vec2(0, 0);
  Window.BgColor = ImColor(0, 0, 0, 0);
  LOG("[DIAG] Screen: %.0fx%.0f\n", Window.Size.x, Window.Size.y);

  Type = ATTACH;
  CallBackFn = CallBack;

  LOG("[DIAG] Creating D3D device on Discord HWND...\n");
  if (!g_Device.CreateDeviceD3D(Window.hWnd)) {
      LOG("[-] CreateDeviceD3D FAILED!\n");
      g_Device.CleanupDeviceD3D();
      throw OSException("Failed to attach D3D device to Discord Overlay");
  }
  LOG("[+] D3D device created successfully.\n");

  try {
    LOG("[DIAG] Initializing ImGui...\n");
    InitImGui(g_Device.g_pd3dDevice, g_Device.g_pd3dDeviceContext);
    LOG("[+] ImGui initialized successfully.\n");
  } catch (OSException &e) {
    LOG("[-] InitImGui FAILED: %s\n", e.what());
    throw e;
  }

  LOG("[+] Entering MainLoop...\n");
  MainLoop();
}

bool OSImGui_External::PeekEndMessage() {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    if (msg.message == WM_QUIT)
      return true;
  }
  return false;
}

void OSImGui_External::MainLoop() {
  static int frameCount = 0;
  while (!EndFlag) {
    if (PeekEndMessage())
      break;
    if (Type == ATTACH) {
      if (!UpdateWindowData())
        break;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    this->CallBackFn();

    ImGui::Render();
    const float clear_color_with_alpha[4] = {0, 0, 0, 0}; // Fully transparent
    g_Device.g_pd3dDeviceContext->OMSetRenderTargets(
        1, &g_Device.g_mainRenderTargetView, NULL);
    g_Device.g_pd3dDeviceContext->ClearRenderTargetView(
        g_Device.g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = g_Device.g_pSwapChain->Present(1, 0);
    frameCount++;
    if (frameCount == 1 || frameCount == 60) {
      LOG("[DIAG] Frame %d rendered. Present() returned: 0x%08lX\n", frameCount, hr);
    }
  }
  LOG("[DIAG] MainLoop exited after %d frames.\n", frameCount);
  CleanImGui();
}
/*
bool OSImGui_External::CreateMyWindow() {
  WNDCLASSEXW wc = {sizeof(wc),
                    CS_CLASSDC,
                    WndProc_External,
                    0L,
                    0L,
                    GetModuleHandle(NULL),
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    Window.wClassName.c_str(),
                    NULL};
  RegisterClassExW(&wc);
  if (Type == ATTACH) {
    // Hardened overlay: avoid WS_EX_TOOLWINDOW + WS_EX_TRANSPARENT combo
    // Use WS_EX_LAYERED + WS_EX_NOACTIVATE — same visual result, different fingerprint
    Window.hWnd =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                        Window.wClassName.c_str(), Window.wName.c_str(),
                        WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL,
                        NULL, GetModuleHandle(NULL), NULL);
    // Use color-key transparency (black = transparent) instead of full alpha
    SetLayeredWindowAttributes(Window.hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
  } else {
    Window.hWnd =
        CreateWindowW(Window.wClassName.c_str(), Window.wName.c_str(),
                      WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU,
                      (int)Window.Pos.x, (int)Window.Pos.y, (int)Window.Size.x,
                      (int)Window.Size.y, NULL, NULL, wc.hInstance, NULL);
  }
  Window.hInstance = wc.hInstance;

  if (!g_Device.CreateDeviceD3D(Window.hWnd)) {
    g_Device.CleanupDeviceD3D();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return false;
  }

  ShowWindow(Window.hWnd, SW_SHOWDEFAULT);
  UpdateWindow(Window.hWnd);


  return Window.hWnd != NULL;
}
*/
bool OSImGui_External::UpdateWindowData() {
  // Discord overlay hijack: DON'T reposition or restyle the window.
  // Discord manages its own overlay window — we just piggyback on it.
  // Only update mouse position for ImGui input (matching R6External pattern).

  POINT MousePos;
  GetCursorPos(&MousePos);
  ImGui::GetIO().MousePos = ImVec2((float)MousePos.x, (float)MousePos.y);
  ImGui::GetIO().MouseDown[0] = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
  ImGui::GetIO().MouseDown[1] = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;

  return true;
}
/*
LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;

  switch (msg) {

  case WM_CREATE: {
    // DwmExtendFrameIntoClientArea({-1}) removed — this is the #1 BattlEye
    // overlay signature. Color-key transparency via SetLayeredWindowAttributes
    // achieves the same visual effect without the DWM fingerprint.
    break;
  }
  case WM_SIZE:
    if (g_Device.g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
      g_Device.CleanupRenderTarget();
      g_Device.g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam),
                                           (UINT)HIWORD(lParam),
                                           DXGI_FORMAT_UNKNOWN, 0);
      g_Device.CreateRenderTarget();
    }
    return 0;
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU)
      return 0;
    break;
  case WM_DESTROY:
    ::PostQuitMessage(0);
    return 0;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
*/
} // namespace RenderCore