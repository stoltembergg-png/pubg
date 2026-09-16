#pragma once

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

extern void BaseLog(const char* fmt, ...);
#ifndef LOG
#define LOG(fmt, ...) BaseLog(fmt, ##__VA_ARGS__)
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace DiscordOverlay {

    inline HWND Overlay = nullptr;
    inline MSG Msg = { nullptr };

    inline ID3D11Device* Device = nullptr;
    inline IDXGISwapChain* SwapChain = nullptr;
    inline ID3D11DeviceContext* DeviceContext = nullptr;
    inline ID3D11RenderTargetView* TargetView = nullptr;

    inline int ScreenWidth = 0;
    inline int ScreenHeight = 0;

    inline bool ShowMenu = false;

    inline std::function<void()> RenderCallback = nullptr;

    inline bool HijackDiscord() {
        ScreenWidth = (GetSystemMetrics)(SM_CXSCREEN);
        ScreenHeight = (GetSystemMetrics)(SM_CYSCREEN);

        LOG("[DiscordOverlay] Screen: %dx%d\n", ScreenWidth, ScreenHeight);

        Overlay = FindWindowA(("Chrome_WidgetWin_1"), ("Discord Overlay"));
        if (!Overlay) {
            LOG("[-] Discord overlay not found.\n");
            return false;
        }

        LOG("[+] Found Discord overlay HWND: 0x%p\n", (void*)Overlay);

        LOG("[+] Discord overlay hijacked.\n");
        return true;
    }

    inline bool CreateDevice() {
        DXGI_RATIONAL refresh_rate{};
        ZeroMemory(&refresh_rate, sizeof(DXGI_RATIONAL));
        refresh_rate.Numerator = 0;
        refresh_rate.Denominator = 1;

        DXGI_MODE_DESC buffer_desc{};
        ZeroMemory(&buffer_desc, sizeof(DXGI_MODE_DESC));
        buffer_desc.Width = ScreenWidth;
        buffer_desc.Height = ScreenHeight;
        buffer_desc.RefreshRate = refresh_rate;
        buffer_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        buffer_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        buffer_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

        DXGI_SAMPLE_DESC sample_desc{};
        ZeroMemory(&sample_desc, sizeof(DXGI_SAMPLE_DESC));
        sample_desc.Count = 1;
        sample_desc.Quality = 0;

        DXGI_SWAP_CHAIN_DESC swapchain_desc{};
        ZeroMemory(&swapchain_desc, sizeof(DXGI_SWAP_CHAIN_DESC));
        swapchain_desc.BufferDesc = buffer_desc;
        swapchain_desc.SampleDesc = sample_desc;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.BufferCount = 2;
        swapchain_desc.OutputWindow = Overlay;
        swapchain_desc.Windowed = TRUE;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        swapchain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        auto ret = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL,
            0, 0, D3D11_SDK_VERSION,
            &swapchain_desc, &SwapChain, &Device, 0, &DeviceContext);

        if (FAILED(ret)) {
            LOG("[-] D3D11CreateDeviceAndSwapChain FAILED: 0x%08lX\n", ret);
            return false;
        }

        LOG("[+] D3D device created.\n");
        return true;
    }

    inline bool CreateTarget() {
        ID3D11Texture2D* render_buffer{ nullptr };
        auto result = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&render_buffer));
        if (FAILED(result)) {
            LOG("[-] GetBuffer FAILED: 0x%08lX\n", result);
            return false;
        }
        result = Device->CreateRenderTargetView(render_buffer, nullptr, &TargetView);
        if (FAILED(result)) {
            LOG("[-] CreateRenderTargetView FAILED: 0x%08lX\n", result);
            return false;
        }
        render_buffer->Release();
        LOG("[+] Render target created.\n");
        return true;
    }

    inline bool CreateImgui() {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = NULL;

        io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 18.0f, NULL, io.Fonts->GetGlyphRangesDefault());

        auto imgui_win32 = ImGui_ImplWin32_Init(Overlay);
        if (!imgui_win32) {
            LOG("[-] ImGui_ImplWin32_Init FAILED\n");
            return false;
        }
        auto imgui_dx11 = ImGui_ImplDX11_Init(Device, DeviceContext);
        if (!imgui_dx11) {
            LOG("[-] ImGui_ImplDX11_Init FAILED\n");
            return false;
        }
        Device->Release();

        DWORD assid = 0;
        (GetWindowThreadProcessId)(Overlay, &assid);

        LOG("[+] ImGui initialized.\n");
        return true;
    }

    inline void ReleaseObjects() {
        if (TargetView) { TargetView->Release(); TargetView = nullptr; }
        if (DeviceContext) { DeviceContext->Release(); DeviceContext = nullptr; }
        if (Device) { Device->Release(); Device = nullptr; }
        if (SwapChain) { SwapChain->Release(); SwapChain = nullptr; }
    }

    inline void RenderThread() {
        LOG("[+] Render thread started.\n");

        while (Msg.message != WM_QUIT) {
            if (PeekMessage(&Msg, Overlay, 0, 0, PM_REMOVE)) {
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
            }

            if (GetAsyncKeyState(VK_HOME) & 1) {
                ShowMenu = !ShowMenu;
            }

            ImGui_ImplDX11_NewFrame();
            POINT p;
            GetCursorPos(&p);
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = ImVec2((float)p.x, (float)p.y);
            io.MouseDown[0] = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
            io.MouseDown[1] = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (RenderCallback) {
                RenderCallback();
            }

            const float color[]{ 0, 0, 0, 0 };

            ImGui::Render();

            DeviceContext->OMSetRenderTargets(1, &TargetView, nullptr);
            DeviceContext->ClearRenderTargetView(TargetView, color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            SwapChain->Present(1, 0);
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        ReleaseObjects();
    }

    inline bool StartImgui() {
        if (!CreateDevice()) return false;
        if (!CreateTarget()) return false;
        if (!CreateImgui()) return false;
        return true;
    }

    inline void Run(std::function<void()> renderFn) {
        RenderCallback = renderFn;

        LOG("[*] Waiting for Discord overlay...\n");
        LOG("[!] Make sure Discord overlay is ENABLED and PUBG is in BORDERLESS mode.\n");

        while (!HijackDiscord()) {
            Sleep(1000);
        }

        if (!StartImgui()) {
            LOG("[-] Failed to start Discord overlay renderer!\n");
            return;
        }

        RenderThread();
    }

}
