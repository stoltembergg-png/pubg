#include "driver.h"
#include "payload_api.h"
#include "virtual_copy.h"

#include "../../PubgExt/driver/ioctl_protocol.h"

#define PUBGEXT_DEVICE_NAME L"\\Device\\PubgExtRw"
#define PUBGEXT_DOS_NAME L"\\DosDevices\\PubgExtRw"

/* Must match the payload-stage entries documented in the mapper. */
#define PUBGEXT_STAGE_STATUS_BASE ((NTSTATUS)0xE80A0000L)
typedef enum _PUBGEXT_PAYLOAD_STAGE {
    PUBGEXT_STAGE_PAYLOAD_VALIDATE = 0x31,
    PUBGEXT_STAGE_DEVICE_CREATE = 0x32,
    PUBGEXT_STAGE_DEVICE_OUTPUT = 0x33,
    PUBGEXT_STAGE_SYMBOLIC_LINK = 0x34,
    PUBGEXT_STAGE_DISPATCH_SETUP = 0x35
} PUBGEXT_PAYLOAD_STAGE;

static NTSTATUS PayloadStageFailure(PUBGEXT_PAYLOAD_STAGE stage,
    NTSTATUS original)
{
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[PubgExtPayload][failure] stage=0x%02X code=0x%08X original=0x%08X\n",
        stage, (ULONG)(PUBGEXT_STAGE_STATUS_BASE | (ULONG)stage), original);
    return (NTSTATUS)(PUBGEXT_STAGE_STATUS_BASE | (ULONG)stage);
}

/* This kernel export is not declared by every WDK ntifs.h version. */
NTSYSAPI PVOID NTAPI PsGetProcessWow64Process(_In_ PEPROCESS Process);

/* Stable private device class GUID for this project. */
static const GUID g_device_class_guid =
    { 0x6f4e2f9b, 0x9c1d, 0x4e83, { 0x9b, 0x1d, 0x2a, 0x73, 0x8e, 0x4c, 0x11, 0x52 } };
static PDEVICE_OBJECT g_device_object;
static EX_PUSH_LOCK g_session_lock;

typedef struct _PUBGEXT_FILE_CONTEXT {
    PEPROCESS opener_process;
} PUBGEXT_FILE_CONTEXT;

static NTSTATUS CompleteCreate(PIRP irp, NTSTATUS status)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/*
 * Exact DOS image paths allowed to open the device on this machine. These
 * paths vary by machine and build directory; adding another executable
 * requires editing this single allowlist. There is deliberately no basename,
 * root, or suffix fallback.
 */
static const UNICODE_STRING g_allowed_image_paths[] = {
    RTL_CONSTANT_STRING(L"\\??\\D:\\PROJETOS\\PUBG\\BUILD-FIX2\\RELEASE\\RUNTIMEBROKER.EXE"),
    RTL_CONSTANT_STRING(L"\\??\\D:\\PROJETOS\\PUBG\\READWRITEDRIVER\\X64\\RELEASE\\READWRITEUSER.EXE")
};

static BOOLEAN EqualCanonicalImagePath(PUNICODE_STRING image_name)
{
    UNICODE_STRING normalized = { 0 };
    UNICODE_STRING volume_name = { 0 };
    UNICODE_STRING dos_name = { 0 };
    UNICODE_STRING dos_normalized = { 0 };
    UNICODE_STRING canonical = { 0 };
    UNICODE_STRING native_prefix = RTL_CONSTANT_STRING(L"\\DEVICE\\HARDDISKVOLUME");
    UNICODE_STRING dos_devices_prefix = RTL_CONSTANT_STRING(L"\\DOSDEVICES\\");
    UNICODE_STRING dos_prefix = RTL_CONSTANT_STRING(L"\\??\\");
    PFILE_OBJECT file_object = NULL;
    PDEVICE_OBJECT volume_device = NULL;
    BOOLEAN equal = FALSE;
    NTSTATUS status;
    USHORT volume_length;
    USHORT suffix_length;
    USHORT dos_tail_length;
    USHORT canonical_length;
    PWCHAR dos_tail;
    USHORT index;
    ULONG i;

    if (!image_name || !image_name->Buffer || image_name->Length == 0)
        return FALSE;
    if (!NT_SUCCESS(RtlUpcaseUnicodeString(&normalized, image_name, TRUE)))
        return FALSE;
    for (index = 0; index < normalized.Length / sizeof(WCHAR); ++index)
        if (normalized.Buffer[index] == L'/')
            normalized.Buffer[index] = L'\\';

    /* Resolve the native volume to its DOS name before any authorization. */
    if (!RtlPrefixUnicodeString(&native_prefix, &normalized, FALSE))
        goto done;
    index = (USHORT)(native_prefix.Length / sizeof(WCHAR));
    while (index < normalized.Length / sizeof(WCHAR) &&
        normalized.Buffer[index] >= L'0' && normalized.Buffer[index] <= L'9')
        ++index;
    if (index == native_prefix.Length / sizeof(WCHAR) ||
        index >= normalized.Length / sizeof(WCHAR) ||
        normalized.Buffer[index] != L'\\')
        goto done;
    volume_length = (USHORT)(index * sizeof(WCHAR));
    suffix_length = normalized.Length - volume_length;
    if (suffix_length == 0)
        goto done;

    volume_name.Length = volume_length;
    volume_name.MaximumLength = volume_length + sizeof(WCHAR);
    volume_name.Buffer = ExAllocatePool2(POOL_FLAG_PAGED,
        volume_name.MaximumLength, 'pVwP');
    if (!volume_name.Buffer)
        goto done;
    RtlCopyMemory(volume_name.Buffer, normalized.Buffer, volume_length);
    volume_name.Buffer[volume_length / sizeof(WCHAR)] = L'\0';
    status = IoGetDeviceObjectPointer(&volume_name, FILE_READ_ATTRIBUTES,
        &file_object, &volume_device);
    if (!NT_SUCCESS(status))
        goto done;
    status = IoVolumeDeviceToDosName(volume_device, &dos_name);
    if (!NT_SUCCESS(status) || !dos_name.Buffer || dos_name.Length == 0)
        goto done;
    if (!NT_SUCCESS(RtlUpcaseUnicodeString(&dos_normalized, &dos_name, TRUE)))
        goto done;

    if (RtlPrefixUnicodeString(&dos_devices_prefix, &dos_normalized, FALSE))
    {
        dos_tail = dos_normalized.Buffer +
            dos_devices_prefix.Length / sizeof(WCHAR);
        dos_tail_length = dos_normalized.Length - dos_devices_prefix.Length;
    }
    else if (RtlPrefixUnicodeString(&dos_prefix, &dos_normalized, FALSE))
    {
        dos_tail = dos_normalized.Buffer + dos_prefix.Length / sizeof(WCHAR);
        dos_tail_length = dos_normalized.Length - dos_prefix.Length;
    }
    else
        goto done;
    if (dos_tail_length < 2 * sizeof(WCHAR) || dos_tail[1] != L':')
        goto done;
    if (suffix_length > MAXUSHORT - dos_prefix.Length ||
        dos_tail_length > MAXUSHORT - dos_prefix.Length - suffix_length)
        goto done;
    canonical_length = dos_prefix.Length + dos_tail_length + suffix_length;
    canonical.MaximumLength = canonical_length + sizeof(WCHAR);
    canonical.Length = canonical_length;
    canonical.Buffer = ExAllocatePool2(POOL_FLAG_PAGED,
        canonical.MaximumLength, 'cVwP');
    if (!canonical.Buffer)
        goto done;
    RtlCopyMemory(canonical.Buffer, dos_prefix.Buffer, dos_prefix.Length);
    RtlCopyMemory(canonical.Buffer + dos_prefix.Length / sizeof(WCHAR),
        dos_tail, dos_tail_length);
    RtlCopyMemory(canonical.Buffer +
        (dos_prefix.Length + dos_tail_length) / sizeof(WCHAR),
        normalized.Buffer + volume_length / sizeof(WCHAR), suffix_length);
    canonical.Buffer[canonical.Length / sizeof(WCHAR)] = L'\0';

    for (i = 0; i < RTL_NUMBER_OF(g_allowed_image_paths); ++i)
    {
        if (RtlEqualUnicodeString(&canonical, &g_allowed_image_paths[i], FALSE))
        {
            equal = TRUE;
            break;
        }
    }
done:
    if (file_object)
        ObDereferenceObject(file_object);
    if (dos_name.Buffer)
        ExFreePool(dos_name.Buffer);
    if (canonical.Buffer)
        ExFreePool(canonical.Buffer);
    if (dos_normalized.Buffer)
        RtlFreeUnicodeString(&dos_normalized);
    if (volume_name.Buffer)
        ExFreePool(volume_name.Buffer);
    if (normalized.Buffer)
        RtlFreeUnicodeString(&normalized);
    return equal;
}

static BOOLEAN IsAllowedOpener(PEPROCESS process)
{
    PUNICODE_STRING image_name = NULL;
    BOOLEAN allowed = FALSE;
    if (!process || !NT_SUCCESS(SeLocateProcessImageName(process, &image_name)))
        return FALSE;
    /* A basename is deliberately never used as an authorization fallback. */
    allowed = EqualCanonicalImagePath(image_name);
    if (image_name)
        ExFreePool(image_name);
    return allowed;
}

static NTSTATUS ValidateRequestHeader(const PUBGEXT_REQUEST_HEADER* header,
    ULONG expected_size)
{
    if (!header)
        return STATUS_INVALID_PARAMETER;
    if (header->magic != PUBGEXT_IOCTL_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (header->major != PUBGEXT_PROTOCOL_MAJOR ||
        header->minor != PUBGEXT_PROTOCOL_MINOR)
        return STATUS_REVISION_MISMATCH;
    if (header->struct_size != expected_size)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (header->flags != 0 || header->reserved != 0)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static BOOLEAN IsUserRange(uint64_t address, uint64_t length)
{
    uint64_t highest = (uint64_t)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS;
    if (address == 0 || length == 0 || address > highest)
        return FALSE;
    return length - 1 <= highest - address;
}

static VOID FillResponse(PUBGEXT_RESPONSE_HEADER* response,
    uint64_t request_id, NTSTATUS operation_status, uint64_t transferred,
    uint32_t struct_size)
{
    RtlZeroMemory(response, sizeof(*response));
    response->magic = PUBGEXT_IOCTL_MAGIC;
    response->major = PUBGEXT_PROTOCOL_MAJOR;
    response->minor = PUBGEXT_PROTOCOL_MINOR;
    response->struct_size = struct_size;
    response->request_id = request_id;
    response->operation_status = (int32_t)operation_status;
    response->transferred = transferred;
}

static NTSTATUS LookupTarget(const PUBGEXT_READ_REQUEST* request,
    PEPROCESS* process)
{
    NTSTATUS status;
    if (!request->pid || !IsUserRange(request->remote_va, request->length))
        return STATUS_INVALID_PARAMETER;
    status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)request->pid, process);
    if (!NT_SUCCESS(status))
        return status;
    /* Protocol v2 intentionally has no WOW64 address ABI. */
    if (PsGetProcessWow64Process(*process))
    {
        ObDereferenceObject(*process);
        *process = NULL;
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS DispatchCreate(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file_object = IoGetCurrentIrpStackLocation(irp)->FileObject;
    PEPROCESS opener;
    PUBGEXT_FILE_CONTEXT* context;
    NTSTATUS status = STATUS_ACCESS_DENIED;
    UNREFERENCED_PARAMETER(device);

    if (irp->RequestorMode != UserMode)
        return CompleteCreate(irp, STATUS_ACCESS_DENIED);
    opener = IoGetRequestorProcess(irp);
    if (!opener || !IsAllowedOpener(opener))
        return CompleteCreate(irp, STATUS_ACCESS_DENIED);
    context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context), 'sRwP');
    if (!context)
        return CompleteCreate(irp, STATUS_INSUFFICIENT_RESOURCES);
    ObReferenceObject(opener);
    context->opener_process = opener;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_session_lock);
    if (!g_session_object)
    {
        g_session_object = context;
        file_object->FsContext = context;
        status = STATUS_SUCCESS;
    }
    ExReleasePushLockExclusive(&g_session_lock);
    KeLeaveCriticalRegion();
    if (!NT_SUCCESS(status))
    {
        ObDereferenceObject(context->opener_process);
        ExFreePool(context);
    }
    return CompleteCreate(irp, status);
}

static NTSTATUS DispatchCleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file_object = IoGetCurrentIrpStackLocation(irp)->FileObject;
    PUBGEXT_FILE_CONTEXT* context;
    UNREFERENCED_PARAMETER(device);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_session_lock);
    context = (PUBGEXT_FILE_CONTEXT*)file_object->FsContext;
    file_object->FsContext = NULL;
    if (context && g_session_object == (PVOID)context)
        g_session_object = NULL;
    ExReleasePushLockExclusive(&g_session_lock);
    KeLeaveCriticalRegion();
    if (context)
    {
        ObDereferenceObject(context->opener_process);
        ExFreePool(context);
    }
    return CompleteCreate(irp, STATUS_SUCCESS);
}

static NTSTATUS DispatchClose(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    return CompleteCreate(irp, STATUS_SUCCESS);
}

static NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG input_length = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG output_length = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = irp->AssociatedIrp.SystemBuffer;
    PFILE_OBJECT file_object = stack->FileObject;
    PUBGEXT_FILE_CONTEXT* context;
    PEPROCESS requestor;
    NTSTATUS status = STATUS_SUCCESS;
    NTSTATUS operation_status;
    PEPROCESS target = NULL;
    ULONG transferred = 0;
    uint64_t response_length;
    BOOLEAN session_lock_held = FALSE;
    BOOLEAN response_valid = FALSE;

    /* Known IOCTL check intentionally precedes every other check. */
    if (code != PUBGEXT_IOCTL_AUTH && code != PUBGEXT_IOCTL_QUERY_CAPS &&
        code != PUBGEXT_IOCTL_READ && code != PUBGEXT_IOCTL_WRITE)
    {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto reject;
    }
    if (irp->RequestorMode != UserMode)
        goto reject_access;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_session_lock);
    session_lock_held = TRUE;
    context = (PUBGEXT_FILE_CONTEXT*)file_object->FsContext;
    requestor = IoGetRequestorProcess(irp);
    if (!context || (PVOID)context != g_session_object || !context->opener_process ||
        requestor != context->opener_process)
    {
        goto reject_access;
    }
    if (!buffer || input_length < PUBGEXT_REQUEST_FIXED_SIZE)
    {
        goto reject_length;
    }

    if (code == PUBGEXT_IOCTL_AUTH)
    {
        status = ValidateRequestHeader((PUBGEXT_REQUEST_HEADER*)buffer,
            sizeof(PUBGEXT_REQUEST_HEADER));
        if (NT_SUCCESS(status) && (input_length != sizeof(PUBGEXT_REQUEST_HEADER) ||
            output_length != sizeof(PUBGEXT_RESPONSE_HEADER)))
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status))
        {
            FillResponse((PUBGEXT_RESPONSE_HEADER*)buffer,
                ((PUBGEXT_REQUEST_HEADER*)buffer)->request_id, STATUS_SUCCESS, 0,
                sizeof(PUBGEXT_RESPONSE_HEADER));
            irp->IoStatus.Information = sizeof(PUBGEXT_RESPONSE_HEADER);
        }
        goto finish;
    }
    if (code == PUBGEXT_IOCTL_QUERY_CAPS)
    {
        PUBGEXT_QUERY_CAPS_RESPONSE* response = (PUBGEXT_QUERY_CAPS_RESPONSE*)buffer;
        status = ValidateRequestHeader((PUBGEXT_REQUEST_HEADER*)buffer,
            sizeof(PUBGEXT_REQUEST_HEADER));
        if (NT_SUCCESS(status) && (input_length != sizeof(PUBGEXT_REQUEST_HEADER) ||
            output_length != sizeof(*response)))
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status))
        {
            FillResponse(&response->header,
                ((PUBGEXT_REQUEST_HEADER*)buffer)->request_id, STATUS_SUCCESS, 0,
                sizeof(*response));
            response->caps = PUBGEXT_CAP_AUTH | PUBGEXT_CAP_QUERY |
                PUBGEXT_CAP_READ | PUBGEXT_CAP_WRITE | PUBGEXT_CAP_VIRTUAL;
            response->max_transfer = PUBGEXT_MAX_TRANSFER;
            irp->IoStatus.Information = sizeof(*response);
        }
        goto finish;
    }

    if (code == PUBGEXT_IOCTL_READ)
    {
        PUBGEXT_READ_REQUEST* request = (PUBGEXT_READ_REQUEST*)buffer;
        if (input_length != sizeof(*request))
            status = STATUS_INFO_LENGTH_MISMATCH;
        else
            status = ValidateRequestHeader(&request->header, sizeof(*request));
        if (NT_SUCCESS(status) && (output_length < sizeof(PUBGEXT_RESPONSE_HEADER) ||
            request->length == 0 || request->length > PUBGEXT_MAX_TRANSFER ||
            (uint64_t)sizeof(PUBGEXT_RESPONSE_HEADER) + request->length > MAXULONG ||
            output_length != sizeof(PUBGEXT_RESPONSE_HEADER) + request->length))
            status = STATUS_INVALID_BUFFER_SIZE;
        if (NT_SUCCESS(status))
        {
            if (request->reserved0 || request->reserved1 || request->pid == 0 ||
                !IsUserRange(request->remote_va, request->length))
                status = STATUS_INVALID_PARAMETER;
            else
            {
                response_valid = TRUE;
                status = LookupTarget(request, &target);
            }
        }
        if (NT_SUCCESS(status))
        {
            operation_status = VirtualCopyProcess(target, request->remote_va,
                (PUCHAR)buffer + sizeof(PUBGEXT_RESPONSE_HEADER), request->length,
                FALSE, &transferred);
            target = NULL; /* VirtualCopyProcess consumes the lookup reference. */
        }
        else
            operation_status = status;
        if (response_valid)
        {
            FillResponse((PUBGEXT_RESPONSE_HEADER*)buffer, request->header.request_id,
                operation_status, transferred, sizeof(PUBGEXT_RESPONSE_HEADER));
            irp->IoStatus.Information = sizeof(PUBGEXT_RESPONSE_HEADER) + transferred;
            status = STATUS_SUCCESS;
        }
        goto finish;
    }

    /* WRITE: validate fixed header before checking fixed-size-plus-data. */
    {
        PUBGEXT_WRITE_REQUEST* request = (PUBGEXT_WRITE_REQUEST*)buffer;
        if (input_length < PUBGEXT_WRITE_FIXED_SIZE)
            status = STATUS_INFO_LENGTH_MISMATCH;
        else
            status = ValidateRequestHeader(&request->header,
                PUBGEXT_WRITE_FIXED_SIZE);
        if (NT_SUCCESS(status) && (request->length == 0 ||
            request->length > PUBGEXT_MAX_TRANSFER ||
            request->length > MAXULONG - PUBGEXT_WRITE_FIXED_SIZE ||
            input_length != PUBGEXT_WRITE_FIXED_SIZE + request->length ||
            output_length != sizeof(PUBGEXT_RESPONSE_HEADER)))
            status = STATUS_INVALID_BUFFER_SIZE;
        if (NT_SUCCESS(status))
        {
            if (request->reserved0 || request->reserved1 || request->pid == 0 ||
                !IsUserRange(request->remote_va, request->length))
                status = STATUS_INVALID_PARAMETER;
            else
            {
                response_valid = TRUE;
                status = LookupTarget((PUBGEXT_READ_REQUEST*)request, &target);
            }
        }
        if (NT_SUCCESS(status))
        {
            operation_status = VirtualCopyProcess(target, request->remote_va,
                request->data, request->length, TRUE, &transferred);
            target = NULL; /* VirtualCopyProcess consumes the lookup reference. */
        }
        else
            operation_status = status;
        if (response_valid)
        {
            FillResponse((PUBGEXT_RESPONSE_HEADER*)buffer, request->header.request_id,
                operation_status, transferred, sizeof(PUBGEXT_RESPONSE_HEADER));
            irp->IoStatus.Information = sizeof(PUBGEXT_RESPONSE_HEADER);
            status = STATUS_SUCCESS;
        }
    }

finish:
    if (session_lock_held)
    {
        ExReleasePushLockShared(&g_session_lock);
        KeLeaveCriticalRegion();
    }
    irp->IoStatus.Status = status;
    if (!NT_SUCCESS(status))
        irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;

reject_access:
    status = STATUS_ACCESS_DENIED;
    if (session_lock_held)
        goto finish;
    goto reject;
reject_length:
    status = STATUS_INFO_LENGTH_MISMATCH;
    if (session_lock_held)
        goto finish;
reject:
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS PayloadInitialize(const PUBGEXT_PAYLOAD_INIT* init,
    PUBGEXT_PAYLOAD_INIT_RESULT* result)
{
    UNICODE_STRING device_name = RTL_CONSTANT_STRING(PUBGEXT_DEVICE_NAME);
    UNICODE_STRING dos_name = RTL_CONSTANT_STRING(PUBGEXT_DOS_NAME);
    UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    PDRIVER_OBJECT driver_object;
    NTSTATUS status;

    if (!init || !result)
        return STATUS_INVALID_PARAMETER;
    if (init->struct_size != sizeof(*init) || init->driver_object == 0)
        return STATUS_INVALID_PARAMETER;
    if (init->abi_major != PUBGEXT_PAYLOAD_ABI_MAJOR ||
        init->abi_minor != PUBGEXT_PAYLOAD_ABI_MINOR)
        return STATUS_REVISION_MISMATCH;

    RtlZeroMemory(result, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->abi_major = PUBGEXT_PAYLOAD_ABI_MAJOR;
    result->abi_minor = PUBGEXT_PAYLOAD_ABI_MINOR;
    driver_object = (PDRIVER_OBJECT)(ULONG_PTR)init->driver_object;
    if (driver_object->Type != IO_TYPE_DRIVER ||
        driver_object->Size < sizeof(*driver_object) ||
        !driver_object->DriverStart || driver_object->DriverSize == 0 ||
        !driver_object->DriverExtension ||
        driver_object->DriverExtension->DriverObject != driver_object)
    {
        status = STATUS_INVALID_PARAMETER;
        status = PayloadStageFailure(PUBGEXT_STAGE_PAYLOAD_VALIDATE, status);
        goto done;
    }

    ExInitializePushLock(&g_session_lock);
    if (g_device_object)
    {
        status = STATUS_DEVICE_BUSY;
        status = PayloadStageFailure(PUBGEXT_STAGE_DEVICE_CREATE, status);
        goto done;
    }
    g_device_object = NULL;
    status = IoCreateDeviceSecure(driver_object, 0, &device_name,
        (DEVICE_TYPE)PUBGEXT_IOCTL_DEVICE_TYPE, FILE_DEVICE_SECURE_OPEN, FALSE,
        &sddl, &g_device_class_guid, &g_device_object);
    if (!NT_SUCCESS(status))
    {
        status = PayloadStageFailure(PUBGEXT_STAGE_DEVICE_CREATE, status);
        goto done;
    }
    if (!g_device_object)
    {
        status = STATUS_UNSUCCESSFUL;
        status = PayloadStageFailure(PUBGEXT_STAGE_DEVICE_OUTPUT, status);
        goto done;
    }
    status = IoCreateSymbolicLink(&dos_name, &device_name);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(g_device_object);
        g_device_object = NULL;
        status = PayloadStageFailure(PUBGEXT_STAGE_SYMBOLIC_LINK, status);
        goto done;
    }
    driver_object->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    driver_object->MajorFunction[IRP_MJ_CLEANUP] = DispatchCleanup;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    /* Conservative lifetime policy: hot-unload is not supported by design.
       DriverUnload stays NULL and the mapped image remains until reboot. */
    driver_object->DriverUnload = NULL;
    g_device_object->Flags &= ~DO_DEVICE_INITIALIZING;
    result->device_created = 1;

done:
    result->status = (int32_t)status;
    return status;
}
