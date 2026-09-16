#pragma once

#include <ntdef.h>
#include <ntifs.h>
#include <intrin.h>
#include <stdint.h>

#define READWRITE_MAX_OPERATION_SIZE (16ULL * 1024ULL * 1024ULL)

NTSTATUS ReadProcessMemory(HANDLE pid, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* read);

NTSTATUS WriteProcessMemory(HANDLE pid, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* written);

/* ReadVirtual/WriteVirtual require a non-paged kernel destination/source. */
