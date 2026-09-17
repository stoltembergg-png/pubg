#include "virtual_copy.h"

NTSTATUS VirtualCopyProcess(PEPROCESS process, uint64_t remote_va, PVOID buffer,
    ULONG length, BOOLEAN write, PULONG transferred)
{
    KAPC_STATE apc_state;
    BOOLEAN attached = FALSE;
    ULONG done = 0;
    NTSTATUS status = STATUS_SUCCESS;

    if (transferred)
        *transferred = 0;
    /* A non-NULL process is a lookup reference owned by this callee. */
    if (!process)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        if (!buffer || !transferred || length == 0)
        {
            status = STATUS_INVALID_PARAMETER;
            __leave;
        }
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            status = STATUS_INVALID_DEVICE_STATE;
            __leave;
        }
        __try
        {
            KeStackAttachProcess(process, &apc_state);
            attached = TRUE;
            while (done < length)
            {
                ULONG chunk = length - done;
                PVOID remote;
                PVOID local;

                if (chunk > PAGE_SIZE)
                    chunk = PAGE_SIZE;
                remote = (PVOID)(ULONG_PTR)(remote_va + done);
                local = (PUCHAR)buffer + done;
                if (write)
                {
                    ProbeForWrite(remote, chunk, sizeof(UCHAR));
                    RtlCopyMemory(remote, local, chunk);
                }
                else
                {
                    ProbeForRead(remote, chunk, sizeof(UCHAR));
                    RtlCopyMemory(local, remote, chunk);
                }
                /* Count only a page whose probe and copy both completed. */
                done += chunk;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = GetExceptionCode();
        }
    }
    __finally
    {
        if (attached)
            KeUnstackDetachProcess(&apc_state);
        /* Always consume the PsLookupProcessByProcessId reference. */
        ObDereferenceObject(process);
    }

    if (transferred)
        *transferred = done;
    if (done != 0 && done != length)
        return STATUS_PARTIAL_COPY;
    return NT_SUCCESS(status) && done == length ? STATUS_SUCCESS : status;
}
