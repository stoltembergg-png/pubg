#pragma once

#include <ntifs.h>
#include <stdint.h>

/* VirtualCopyProcess always consumes the caller's PsLookupProcessByProcessId
   reference, including parameter/IRQL early exits and partial-copy paths.
   Copies are attempted in PAGE_SIZE (4 KiB) chunks; transferred counts only
   chunks whose remote probe and copy completed. */
NTSTATUS VirtualCopyProcess(_In_ PEPROCESS process, _In_ uint64_t remote_va,
    _Inout_ PVOID buffer, _In_ ULONG length, _In_ BOOLEAN write,
    _Out_ PULONG transferred);
