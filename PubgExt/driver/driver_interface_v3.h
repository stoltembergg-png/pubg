#pragma once

// Read/write transport for ReadWriteDriver.  The implementation delegates to
// the driver's win32kbase NtUserSetSysColors hook rather than a device handle.
//
// ReadWriteDriver currently supports only Windows 10 21H1 build 19043 and
// hardcodes win32kbase+0x2B3C90 for that exact build.

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "common.h"

class DriverInterfaceV3
{
public:
    struct BatchReadEntry
    {
        uintptr_t address{};
        size_t size{};
        void* buffer{};
        bool success{ false };
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
    bool WriteMemory(DWORD pid, uintptr_t address, const void* buffer, size_t size,
                     const char* debugName = nullptr) const;
    // TODO: FindRealCr3/GetProcessCr3 remain public for compatibility, but
    // have no functional consumer until CR3 is exposed by the shared command.
    bool BatchReadMemory(DWORD pid, BatchReadEntry* entries, size_t count,
                         size_t* successfulCount = nullptr) const;

    void InjectMouseMove(int moveX, int moveY) const;
    void TestRandomMouseMoveLoop(std::atomic<bool>& running) const;

private:
    DWORD currentPid_{ 0 };
    uintptr_t baseAddress_{ 0 };
    bool driverLoaded_{ false };
    uint64_t sessionToken_{ 0 };
};
