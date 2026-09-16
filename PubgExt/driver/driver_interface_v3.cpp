#include "driver_interface_v3.h"

// TODO: replace every stub below with the real driver implementation.

bool DriverInterfaceV3::Initialize()
{
    return false;
}

void DriverInterfaceV3::Cleanup()
{
    currentPid_ = 0;
    baseAddress_ = 0;
}

DWORD DriverInterfaceV3::GetProcessId(const wchar_t* /*processName*/) const
{
    return 0;
}

void DriverInterfaceV3::SetCurrentPid(DWORD pid)
{
    currentPid_ = pid;
}

uintptr_t DriverInterfaceV3::GetModuleBase(DWORD /*pid*/, const wchar_t* /*moduleName*/) const
{
    return 0;
}

void DriverInterfaceV3::SetBaseAddress(uintptr_t baseAddress)
{
    baseAddress_ = baseAddress;
}

uintptr_t DriverInterfaceV3::FindRealCr3(DWORD /*pid*/, uintptr_t /*baseAddress*/, uintptr_t /*dataAnchor*/) const
{
    return 0;
}

uintptr_t DriverInterfaceV3::GetProcessCr3(DWORD /*pid*/) const
{
    return 0;
}

bool DriverInterfaceV3::ReadMemory(DWORD /*pid*/, uintptr_t /*address*/, void* /*buffer*/,
                                   size_t /*size*/, const char* /*debugName*/) const
{
    return false;
}

bool DriverInterfaceV3::BatchReadMemory(DWORD /*pid*/, BatchReadEntry* /*entries*/, size_t /*count*/) const
{
    return false;
}

void DriverInterfaceV3::InjectMouseMove(int /*moveX*/, int /*moveY*/) const
{
    // TODO: implement driver-backed mouse input.
}

void DriverInterfaceV3::TestRandomMouseMoveLoop(std::atomic<bool>& running) const
{
    // The build stub must not generate input. Return immediately and leave
    // lifecycle control to the caller.
    running.store(false, std::memory_order_relaxed);
}
