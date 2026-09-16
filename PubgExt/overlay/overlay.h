#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

namespace DiscordOverlay {

    struct FramebufferHeader {
        uint32_t Magic;
        uint32_t FrameCount;
        uint32_t NoClue;
        uint32_t Width;
        uint32_t Height;
        uint8_t  Buffer[1]; // BGRA8 pixel data follows
    };

    namespace Core {
        // DX11 offscreen rendering resources
        extern ID3D11Device*            Device;
        extern ID3D11DeviceContext*      Context;
        extern ID3D11Texture2D*          OffscreenRT;      // Render target texture
        extern ID3D11Texture2D*          StagingTex;       // CPU-readable copy
        extern ID3D11RenderTargetView*   RTView;

        // Discord shared memory
        extern HANDLE   FileMapping;
        extern void*    MappedAddress;
        extern HANDLE   Mutex;
        extern DWORD    DiscordPid;
        extern uint32_t FrameCount;

        // Cached dimensions
        extern UINT     Width;
        extern UINT     Height;
    }

    void Initialize(HWND gameHwnd, UINT width, UINT height);

    // Cleanup all resources
    void Cleanup();

    // Begin a new ImGui frame
    void BeginFrame();

    // End frame: render offscreen, copy to Discord's framebuffer
    void EndFrame(UINT width, UINT height);

    // Handle mouse input for ImGui
    void HandleInput();
}
