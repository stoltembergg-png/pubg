#include "overlay.h"
#include "../OS-ImGui/imgui/imgui.h"
#include "../OS-ImGui/imgui/imgui_impl_dx11.h"
#include "../OS-ImGui/imgui/imgui_impl_win32.h"
#include <d3d11.h>
#include <dxgi.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <TlHelp32.h>
#include <winternl.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ntdll.lib")

extern void BaseLog(const char* fmt, ...);
#define OLOG(...) BaseLog(__VA_ARGS__)

#ifndef DIRECTORY_QUERY
#define DIRECTORY_QUERY    0x0001
#endif
#ifndef DIRECTORY_TRAVERSE
#define DIRECTORY_TRAVERSE 0x0002
#endif

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;

typedef NTSTATUS(NTAPI* pfnNtOpenDirectoryObject)(
    PHANDLE DirectoryHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
typedef NTSTATUS(NTAPI* pfnNtQueryDirectoryObject)(
    HANDLE DirectoryHandle, PVOID Buffer, ULONG Length,
    BOOLEAN ReturnSingleEntry, BOOLEAN RestartScan, PULONG Context, PULONG ReturnLength);

namespace DiscordOverlay {
    namespace Core {

        ID3D11Device*            Device       = nullptr;
        ID3D11DeviceContext*     Context      = nullptr;
        ID3D11Texture2D*         OffscreenRT  = nullptr;
        ID3D11Texture2D*         StagingTex   = nullptr;
        ID3D11RenderTargetView*  RTView       = nullptr;

        HANDLE   FileMapping    = nullptr;
        void*    MappedAddress  = nullptr;
        HANDLE   Mutex          = nullptr;
        DWORD    DiscordPid     = 0;
        uint32_t FrameCount     = 0;

        UINT Width  = 0;
        UINT Height = 0;

        static std::vector<DWORD> FindAllDiscordPids() {
            std::vector<DWORD> pids;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) return pids;

            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"discord.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"Discord.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"discordcanary.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"discordptb.exe") == 0) {
                        pids.push_back(pe.th32ProcessID);
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            return pids;
        }

        static std::vector<std::string> EnumerateDiscordSharedMemory() {
            std::vector<std::string> found;

            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (!ntdll) return found;

            auto NtOpenDirectoryObject = (pfnNtOpenDirectoryObject)GetProcAddress(ntdll, "NtOpenDirectoryObject");
            auto NtQueryDirectoryObject = (pfnNtQueryDirectoryObject)GetProcAddress(ntdll, "NtQueryDirectoryObject");
            if (!NtOpenDirectoryObject || !NtQueryDirectoryObject) return found;

            UNICODE_STRING dirName;
            dirName.Buffer = (PWSTR)L"\\BaseNamedObjects";
            dirName.Length = (USHORT)(wcslen(dirName.Buffer) * sizeof(WCHAR));
            dirName.MaximumLength = dirName.Length + sizeof(WCHAR);

            OBJECT_ATTRIBUTES oa;
            InitializeObjectAttributes(&oa, &dirName, OBJ_CASE_INSENSITIVE, NULL, NULL);

            HANDLE dirHandle = nullptr;
            NTSTATUS status = NtOpenDirectoryObject(&dirHandle, DIRECTORY_QUERY | DIRECTORY_TRAVERSE, &oa);
            if (status != 0 || !dirHandle) {
                OLOG("[*] Could not open BaseNamedObjects (0x%08lX)\n", status);
                return found;
            }

            BYTE buffer[8192];
            ULONG context = 0;
            ULONG returnLen = 0;
            BOOLEAN restartScan = TRUE;

            while (true) {
                status = NtQueryDirectoryObject(dirHandle, buffer, sizeof(buffer), FALSE, restartScan, &context, &returnLen);
                restartScan = FALSE;

                if (status != 0) break; 

                auto* info = (POBJECT_DIRECTORY_INFORMATION)buffer;
                while (info->Name.Buffer != nullptr) {
                    int len = info->Name.Length / sizeof(WCHAR);
                    if (len > 0 && len < 512) {
                        char narrow[512];
                        int converted = WideCharToMultiByte(CP_ACP, 0, info->Name.Buffer, len, narrow, sizeof(narrow) - 1, nullptr, nullptr);
                        if (converted > 0) {
                            narrow[converted] = '\0';
                            std::string name(narrow);

                            if (name.find("DiscordOverlay") != std::string::npos &&
                                name.find("Framebuffer") != std::string::npos &&
                                name.find("Memory") != std::string::npos) {
                                int typeLen = info->TypeName.Length / sizeof(WCHAR);
                                char typeNarrow[128];
                                WideCharToMultiByte(CP_ACP, 0, info->TypeName.Buffer, typeLen, typeNarrow, sizeof(typeNarrow) - 1, nullptr, nullptr);
                                typeNarrow[typeLen] = '\0';

                                OLOG("[*] Found named section: %s (type: %s)\n", narrow, typeNarrow);
                                found.push_back(name);
                            }
                        }
                    }
                    info++;
                }
            }

            CloseHandle(dirHandle);
            return found;
        }

        static bool TryOpenMappingSingle(const std::string& name, const std::string& mutexName) {
            FileMapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
            if (!FileMapping) {
                DWORD err = GetLastError();
                OLOG("[*]   OpenFileMapping failed: %s (error %d: %s)\n",
                    name.c_str(), err,
                    err == 2 ? "NOT_FOUND" :
                    err == 5 ? "ACCESS_DENIED" :
                    err == 161 ? "BAD_PATHNAME" : "OTHER");
                return false;
            }

            MappedAddress = MapViewOfFile(FileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (!MappedAddress) {
                DWORD err = GetLastError();
                OLOG("[*]   MapViewOfFile failed (error %d)\n", err);
                CloseHandle(FileMapping);
                FileMapping = nullptr;
                return false;
            }

            Mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, mutexName.c_str());
            return true;
        }

        static bool TryAllMappings(const std::vector<DWORD>& discordPids, DWORD gamePid) {
            for (DWORD pid : discordPids) {
                std::string mapName   = "DiscordOverlay_Framebuffer_Memory_" + std::to_string(pid);
                std::string mutexName = "DiscordOverlay_Framebuffer_Mutex_"  + std::to_string(pid);
                OLOG("[*] Trying Discord PID %d: %s\n", pid, mapName.c_str());
                if (TryOpenMappingSingle(mapName, mutexName)) {
                    OLOG("[+] SUCCESS with Discord PID %d\n", pid);
                    DiscordPid = pid;
                    return true;
                }
            }

            if (gamePid) {
                std::string mapName   = "DiscordOverlay_Framebuffer_Memory_" + std::to_string(gamePid);
                std::string mutexName = "DiscordOverlay_Framebuffer_Mutex_"  + std::to_string(gamePid);
                OLOG("[*] Trying game PID %d: %s\n", gamePid, mapName.c_str());
                if (TryOpenMappingSingle(mapName, mutexName)) {
                    OLOG("[+] SUCCESS with game PID %d\n", gamePid);
                    return true;
                }
            }

            OLOG("[*] Enumerating BaseNamedObjects for Discord shared memory...\n");
            auto discovered = EnumerateDiscordSharedMemory();
            for (const auto& name : discovered) {
                std::string mutexName = name;
                size_t pos = mutexName.find("Memory");
                if (pos != std::string::npos) {
                    mutexName.replace(pos, 6, "Mutex");
                }
                OLOG("[*] Trying discovered name: %s\n", name.c_str());
                if (TryOpenMappingSingle(name, mutexName)) {
                    OLOG("[+] SUCCESS with discovered name: %s\n", name.c_str());
                    return true;
                }
            }

            for (DWORD pid : discordPids) {
                std::string mapName   = "Global\\DiscordOverlay_Framebuffer_Memory_" + std::to_string(pid);
                std::string mutexName = "Global\\DiscordOverlay_Framebuffer_Mutex_"  + std::to_string(pid);
                OLOG("[*] Trying Global\\ prefix, PID %d\n", pid);
                if (TryOpenMappingSingle(mapName, mutexName)) {
                    OLOG("[+] SUCCESS with Global\\ prefix, PID %d\n", pid);
                    DiscordPid = pid;
                    return true;
                }
            }

            return false;
        }

        static bool CreateOffscreenResources(UINT w, UINT h) {
            if (RTView)      { RTView->Release();      RTView = nullptr; }
            if (OffscreenRT) { OffscreenRT->Release();  OffscreenRT = nullptr; }
            if (StagingTex)  { StagingTex->Release();   StagingTex = nullptr; }

            D3D11_TEXTURE2D_DESC rtDesc = {};
            rtDesc.Width            = w;
            rtDesc.Height           = h;
            rtDesc.MipLevels        = 1;
            rtDesc.ArraySize        = 1;
            rtDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
            rtDesc.SampleDesc.Count = 1;
            rtDesc.Usage            = D3D11_USAGE_DEFAULT;
            rtDesc.BindFlags        = D3D11_BIND_RENDER_TARGET;

            if (FAILED(Device->CreateTexture2D(&rtDesc, nullptr, &OffscreenRT)))
                return false;

            if (FAILED(Device->CreateRenderTargetView(OffscreenRT, nullptr, &RTView)))
                return false;

            D3D11_TEXTURE2D_DESC stageDesc = rtDesc;
            stageDesc.Usage          = D3D11_USAGE_STAGING;
            stageDesc.BindFlags      = 0;
            stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            if (FAILED(Device->CreateTexture2D(&stageDesc, nullptr, &StagingTex)))
                return false;

            Width = w;
            Height = h;
            return true;
        }
    }

    void Initialize(HWND gameHwnd, UINT width, UINT height) {
   
        auto discordPids = Core::FindAllDiscordPids();
        if (discordPids.empty()) {
            throw std::runtime_error("Discord.exe not found. Make sure Discord is running.");
        }
        OLOG("[+] Found %zu Discord processes: ", discordPids.size());
        for (DWORD pid : discordPids) {
            OLOG("%d ", pid);
        }
        OLOG("\n");
        Core::DiscordPid = discordPids[0];

        // Get game PID
        DWORD gamePid = 0;
        if (gameHwnd) {
            GetWindowThreadProcessId(gameHwnd, &gamePid);
            OLOG("[*] Game PID: %d\n", gamePid);
        }

        bool connected = false;
        const int maxRetries = 15;
        for (int retry = 0; retry < maxRetries; retry++) {
            if (retry > 0) {
                OLOG("[*] Retry %d/%d — waiting 2s for Discord overlay to initialize...\n", retry, maxRetries);
                Sleep(2000);

                discordPids = Core::FindAllDiscordPids();
                if (discordPids.empty()) {
                    OLOG("[-] Discord processes disappeared!\n");
                    continue;
                }
            }

            if (Core::TryAllMappings(discordPids, gamePid)) {
                connected = true;
                break;
            }

            if (retry == 0) {
                OLOG("[!] First attempt failed. Will retry for ~30 seconds...\n");
                OLOG("[!] Make sure the game is focused so Discord's overlay activates.\n");
            }
        }

        if (!connected) {
            throw std::runtime_error(
                "Failed to open Discord's framebuffer shared memory after 30 seconds.\n"
                "Make sure:\n"
                "  1. Discord is running with overlay enabled\n"
                "  2. Legacy Overlay is enabled in Discord settings\n"
                "  3. The game is registered in Discord and overlay is ON for it\n"
                "  4. The game has been focused/running for Discord to inject\n"
                "  5. Try toggling the overlay off and on in Discord settings");
        }

        OLOG("[+] Connected to Discord framebuffer!\n");
        if (Core::Mutex)
            OLOG("[+] Framebuffer mutex acquired.\n");
        else
            OLOG("[*] No mutex found (optional, continuing without sync).\n");

 
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, 2, D3D11_SDK_VERSION,
            &Core::Device, nullptr, &Core::Context);

        if (FAILED(hr)) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, 2, D3D11_SDK_VERSION,
                &Core::Device, nullptr, &Core::Context);
        }

        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create DX11 device for offscreen rendering.");
        }

        OLOG("[+] DX11 offscreen device created.\n");


        if (!Core::CreateOffscreenResources(width, height)) {
            throw std::runtime_error("Failed to create offscreen render textures.");
        }

        OLOG("[+] Offscreen textures created: %dx%d\n", width, height);

        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

        ImFontAtlas* fontAtlas = new ImFontAtlas();
        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;
        fontAtlas->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 20.0f, &fontConfig, io.Fonts->GetGlyphRangesAll());
        fontAtlas->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 18.0f, &fontConfig, io.Fonts->GetGlyphRangesAll());
        io.Fonts = fontAtlas;

        ImGui::AimStarDefaultStyle();
        io.LogFilename = nullptr;

        ImGui_ImplWin32_Init(gameHwnd);
        ImGui_ImplDX11_Init(Core::Device, Core::Context);

        OLOG("[+] ImGui initialized for offscreen rendering.\n");
    }

    void Cleanup() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (Core::RTView)      { Core::RTView->Release();      Core::RTView = nullptr; }
        if (Core::OffscreenRT) { Core::OffscreenRT->Release();  Core::OffscreenRT = nullptr; }
        if (Core::StagingTex)  { Core::StagingTex->Release();   Core::StagingTex = nullptr; }
        if (Core::Context)     { Core::Context->Release();      Core::Context = nullptr; }
        if (Core::Device)      { Core::Device->Release();       Core::Device = nullptr; }

        if (Core::MappedAddress) {
            UnmapViewOfFile(Core::MappedAddress);
            Core::MappedAddress = nullptr;
        }
        if (Core::FileMapping) {
            CloseHandle(Core::FileMapping);
            Core::FileMapping = nullptr;
        }
        if (Core::Mutex) {
            CloseHandle(Core::Mutex);
            Core::Mutex = nullptr;
        }
    }

    void BeginFrame() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame(UINT width, UINT height) {
        if (!Core::MappedAddress)
            return;

        if (width != Core::Width || height != Core::Height) {
            Core::CreateOffscreenResources(width, height);
            ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        }

        ImGui::Render();

        Core::Context->OMSetRenderTargets(1, &Core::RTView, nullptr);
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        Core::Context->ClearRenderTargetView(Core::RTView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        Core::Context->CopyResource(Core::StagingTex, Core::OffscreenRT);

        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = Core::Context->Map(Core::StagingTex, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            auto* header = static_cast<FramebufferHeader*>(Core::MappedAddress);

            if (Core::Mutex)
                WaitForSingleObject(Core::Mutex, 16); // 16ms timeout

  
            header->Width  = width;
            header->Height = height;

            uint8_t* dst = header->Buffer;
            uint8_t* src = static_cast<uint8_t*>(mapped.pData);
            UINT rowBytes = width * 4;

            for (UINT y = 0; y < height; y++) {
                memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
            }

            header->FrameCount = ++Core::FrameCount;

            if (Core::Mutex)
                ReleaseMutex(Core::Mutex);

            Core::Context->Unmap(Core::StagingTex, 0);
        }
    }

    void HandleInput() {
        ImGuiIO& io = ImGui::GetIO();
        POINT mousePos;
        GetCursorPos(&mousePos);
        io.MousePos = ImVec2(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));

        io.MouseDown[0] = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
        io.MouseDown[1] = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
    }
}
