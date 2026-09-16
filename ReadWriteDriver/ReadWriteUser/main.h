#pragma once

#include <stdio.h>
#include <iostream>
#include <phnt_windows.h>
#include <phnt.h>
#include <tlhelp32.h>

#include "../../PubgExt/driver/command_protocol.h"

NTSTATUS KeWriteVirtualMemory(uintptr_t pid, unsigned char* source, uintptr_t destination, SIZE_T size);
NTSTATUS KeReadVirtualMemory(uintptr_t pid, unsigned char* source, uintptr_t destination, SIZE_T size);
uintptr_t GetPIDByName(const WCHAR* name);
uintptr_t KeGetProcessPEB(uintptr_t pid);
uintptr_t GetModuleBase(uintptr_t input_pid, uintptr_t input_peb, const wchar_t* name);

typedef BOOL(__fastcall* NtUserSetSysColors_t)(unsigned int cElements, char* lpaElements, char* lpaRgbValues, int Flags);
