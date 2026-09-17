#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <TlHelp32.h>

#include <cstddef>
#include <cstdint>
#include "../../PubgExt/driver/ioctl_protocol.h"

bool InitializeDevice();
void CleanupDevice();
bool RunDriverSmokeTest();

NTSTATUS KeWriteVirtualMemory(uintptr_t pid, unsigned char* source,
                              uintptr_t destination, SIZE_T size,
                              SIZE_T* transferred = nullptr);
NTSTATUS KeReadVirtualMemory(uintptr_t pid, unsigned char* source,
                             uintptr_t destination, SIZE_T size,
                             SIZE_T* transferred = nullptr);

uintptr_t GetPIDByName(const wchar_t* name);
uintptr_t GetModuleBase(uintptr_t input_pid, const wchar_t* name);
