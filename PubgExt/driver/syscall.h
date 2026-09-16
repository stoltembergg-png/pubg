#pragma once

// Build-only syscall stubs.
// TODO: provide the production syscall wrappers/assembly implementation.

#include <Windows.h>

inline NTSTATUS SyscallNtAllocateVirtualMemory(
    HANDLE /*processHandle*/,
    PVOID* /*baseAddress*/,
    ULONG_PTR /*zeroBits*/,
    PSIZE_T /*regionSize*/,
    ULONG /*allocationType*/,
    ULONG /*protect*/)
{
    return static_cast<NTSTATUS>(-1);
}

inline NTSTATUS SyscallNtFreeVirtualMemory(
    HANDLE /*processHandle*/,
    PVOID* /*baseAddress*/,
    PSIZE_T /*regionSize*/,
    ULONG /*freeType*/)
{
    return static_cast<NTSTATUS>(-1);
}
