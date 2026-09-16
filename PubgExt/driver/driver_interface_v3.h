#pragma once

// Build-only driver interface stub.
// TODO: implement the real kernel-driver transport before using memory or
// input operations in a production build.

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

class DriverInterfaceV3
{
public:
    struct BatchReadEntry
    {
        uintptr_t address{};
        size_t size{};
        void* buffer{};
    };

    bool Initialize();
    void Cleanup();

    DWORD GetProcessId(const wchar_t* processName) const;
    void SetCurrentPid(DWORD pid);
    uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) const;
    void SetBaseAddress(uintptr_t baseAddress);

    uintptr_t FindRealCr3(DWORD pid, uintptr_t baseAddress, uintptr_t dataAnchor) const;
    uintptr_t GetProcessCr3(DWORD pid) const;

    bool ReadMemory(DWORD pid, uintptr_t address, void* buffer, size_t size,
                    const char* debugName = nullptr) const;
    bool BatchReadMemory(DWORD pid, BatchReadEntry* entries, size_t count) const;

    void InjectMouseMove(int moveX, int moveY) const;
    void TestRandomMouseMoveLoop(std::atomic<bool>& running) const;

private:
    DWORD currentPid_{ 0 };
    uintptr_t baseAddress_{ 0 };
};
