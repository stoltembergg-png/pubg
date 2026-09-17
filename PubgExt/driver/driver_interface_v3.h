#pragma once

// Read/write transport for ReadWriteDriver's \\Device\\PubgExtRw interface.
// The DOS device is ACL'd to SYSTEM and built-in Administrators (SY/BA), so
// the application must run elevated. No legacy hook or session token is used.

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ioctl_protocol.h"

class DriverInterfaceV3
{
public:
    struct BatchReadEntry
    {
        uintptr_t address{};
        size_t size{};
        void* buffer{};
        bool success{ false };
        size_t transferred{ 0 };
    };

    bool Initialize();
    void Cleanup();

    DWORD GetProcessId(const wchar_t* processName) const;
    void SetCurrentPid(DWORD pid);
    uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) const;
    void SetBaseAddress(uintptr_t baseAddress);

    bool ReadMemory(DWORD pid, uintptr_t address, void* buffer, size_t size,
                    const char* debugName = nullptr,
                    size_t* bytesTransferred = nullptr) const;
    bool WriteMemory(DWORD pid, uintptr_t address, const void* buffer, size_t size,
                     const char* debugName = nullptr,
                     size_t* bytesTransferred = nullptr) const;
    bool BatchReadMemory(DWORD pid, BatchReadEntry* entries, size_t count,
                         size_t* successfulCount = nullptr) const;

    void InjectMouseMove(int moveX, int moveY) const;
    void TestRandomMouseMoveLoop(std::atomic<bool>& running) const;

private:
    DWORD currentPid_{ 0 };
    uintptr_t baseAddress_{ 0 };
    bool driverLoaded_{ false };
    HANDLE device_{ INVALID_HANDLE_VALUE };
    uint32_t maxTransfer_{ 0 };
    mutable std::atomic<uint64_t> nextRequestId_{ 1 };
};
