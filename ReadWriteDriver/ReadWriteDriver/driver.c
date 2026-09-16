#include "driver.h"
#include "physmem.h"

t_Win32FreePool Win32FreePool;
MmAllocateIndependentPages_t MmAllocateIndependentPages;

static HANDLE g_authorized_pid = NULL;
static PEPROCESS g_authorized_process = NULL;
static uint64_t g_session_token = 0;
static PVOID g_win32freepool_slot = NULL;
static BOOLEAN g_process_notify_registered = FALSE;
static EX_RUNDOWN_REF g_authorization_rundown;
static volatile LONG g_authorization_state = 0;
static volatile LONG g_revoke_requested = FALSE;
static volatile LONG g_rundown_completed = FALSE;
static volatile LONG g_rundown_initialized = FALSE;
static volatile LONG g_notify_unregister_status = STATUS_SUCCESS;
static WORK_QUEUE_ITEM g_revoke_work_item;
static KEVENT g_revoke_work_done;
static volatile LONG g_revoke_work_initialized = FALSE;
static volatile LONG g_revoke_work_queued = FALSE;
static volatile LONG g_teardown_status = STATUS_SUCCESS;

#define AUTH_STATE_IDLE       0
#define AUTH_STATE_INSTALLING 1
#define AUTH_STATE_ACTIVE     2
#define AUTH_STATE_REVOKING   3

static __int64 __fastcall hook(__int64 a, __int64 b, __int64 c);
static VOID ProcessNotify(PEPROCESS process, HANDLE process_id,
	PPS_CREATE_NOTIFY_INFO create_info);
static NTSTATUS ShutdownAuthorization(BOOLEAN from_work_item);
static VOID RevokeWorkItem(PVOID context);

static __int64 CallOriginal(t_Win32FreePool original, __int64 a, __int64 b, __int64 c)
{
	return original ? original(a, b, c) : 0;
}

static __int64 CallOriginalAndRelease(t_Win32FreePool original,
	__int64 a, __int64 b, __int64 c)
{
	__int64 result = CallOriginal(original, a, b, c);
	ExReleaseRundownProtection(&g_authorization_rundown);
	return result;
}

static VOID GenerateSessionToken(void)
{
	UUID uuid = { 0 };
	if (NT_SUCCESS(ExUuidCreate(&uuid)))
		RtlCopyMemory(&g_session_token, &uuid, sizeof(g_session_token));

	/* ExUuidCreate is the primary source; this is only a non-zero fallback. */
	if (g_session_token != 0)
		return;

	LARGE_INTEGER counter = KeQueryPerformanceCounter(NULL);
	g_session_token = (uint64_t)counter.QuadPart ^
		((uint64_t)(ULONG_PTR)PsGetCurrentProcessId() << 32) ^
		(uint64_t)(ULONG_PTR)&g_session_token;
	if (g_session_token == 0)
		g_session_token = (~(uint64_t)(ULONG_PTR)&g_session_token) | 1ULL;
}

static BOOLEAN IsUserRange(uint64_t address, uint64_t length)
{
	ULONG_PTR start;
	ULONG_PTR highest = (ULONG_PTR)MM_HIGHEST_USER_ADDRESS;

	if (address == 0 || length == 0 || address > (uint64_t)highest)
		return FALSE;
	start = (ULONG_PTR)address;
	return length - 1 <= (uint64_t)(highest - start);
}

static NTSTATUS CopyCommandFromKernel(PVOID address, Command* command)
{
	if (!address || !command)
		return STATUS_INVALID_PARAMETER;

	__try
	{
		RtlCopyMemory(command, address, sizeof(Command));
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}

	return STATUS_SUCCESS;
}

static NTSTATUS CompleteCommand(uint64_t user_result, NTSTATUS status,
	uint64_t result, BOOLEAN update_token)
{
	PVOID address = (PVOID)(ULONG_PTR)user_result;
	if (!IsUserRange(user_result, sizeof(Command)))
		return STATUS_INVALID_PARAMETER;

	__try
	{
		Command* command = (Command*)address;
		ProbeForWrite(command, sizeof(Command), __alignof(Command));
		command->status = (int32_t)status;
		command->result = result;
		if (update_token)
			command->auth_token = g_session_token;
		command->op = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}

	return STATUS_SUCCESS;
}

static NTSTATUS UnregisterProcessNotify(VOID)
{
	if (!g_process_notify_registered)
		return STATUS_SUCCESS;

	NTSTATUS status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, TRUE);
	if (NT_SUCCESS(status))
		g_process_notify_registered = FALSE;
	else
		InterlockedExchange(&g_notify_unregister_status, status);
	return status;
}

static VOID QueueRevocationWork(VOID)
{
	if (InterlockedCompareExchange(&g_revoke_work_queued, TRUE, FALSE) == FALSE)
	{
		KeResetEvent(&g_revoke_work_done);
		ExQueueWorkItem(&g_revoke_work_item, DelayedWorkQueue);
	}
}

static VOID UnpublishAuthorizationHook(VOID)
{
	PVOID hook_slot = InterlockedCompareExchangePointer(
		(PVOID volatile*)&g_win32freepool_slot, NULL, NULL);
	if (hook_slot && Win32FreePool)
	{
		/* Do not overwrite a third party that replaced our hook meanwhile. */
		InterlockedCompareExchangePointer((PVOID volatile*)hook_slot,
			(PVOID)Win32FreePool, (PVOID)&hook);
	}
	InterlockedExchangePointer((PVOID volatile*)&g_win32freepool_slot, NULL);
	Win32FreePool = NULL;
}

static VOID CompleteAuthorizationRundown(VOID)
{
	/* State REVOKING prevents new hook entrants before this drain. */
	ExWaitForRundownProtectionRelease(&g_authorization_rundown);
	if (InterlockedCompareExchange(&g_rundown_completed, TRUE, FALSE) == FALSE)
		ExRundownCompleted(&g_authorization_rundown);
}

static VOID CleanupAuthorizationResources(VOID)
{
	PEPROCESS authorized_process = (PEPROCESS)InterlockedExchangePointer(
		(PVOID volatile*)&g_authorized_process, NULL);

	g_authorized_pid = NULL;
	g_session_token = 0;

	if (authorized_process)
		ObDereferenceObject(authorized_process);
}

static NTSTATUS FinishAuthorizationTeardown(BOOLEAN from_work_item)
{
	/* The revocation owner unpublishes the hook before draining its entrants. */
	UnpublishAuthorizationHook();
	NTSTATUS status = UnregisterProcessNotify();
	CompleteAuthorizationRundown();
	CleanupAuthorizationResources();
	if (!from_work_item && InterlockedCompareExchange(&g_revoke_work_queued,
		FALSE, FALSE))
		KeWaitForSingleObject(&g_revoke_work_done, Executive, KernelMode, FALSE, NULL);
	if (NT_SUCCESS(status) && !NT_SUCCESS((NTSTATUS)g_notify_unregister_status))
		status = (NTSTATUS)g_notify_unregister_status;
	InterlockedExchange(&g_teardown_status, status);
	if (!NT_SUCCESS(status))
	{
		if (!from_work_item)
			KeSetEvent(&g_revoke_work_done, IO_NO_INCREMENT, FALSE);
		return status;
	}
	if (!from_work_item)
	{
		InterlockedExchange(&g_authorization_state, AUTH_STATE_IDLE);
		KeSetEvent(&g_revoke_work_done, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_SUCCESS;
}

static NTSTATUS ShutdownAuthorization(BOOLEAN from_work_item)
{
	LONG state = InterlockedCompareExchange(&g_authorization_state,
		AUTH_STATE_REVOKING, AUTH_STATE_ACTIVE);
	if (state == AUTH_STATE_INSTALLING)
	{
		InterlockedExchange(&g_revoke_requested, TRUE);
		return STATUS_DEVICE_BUSY;
	}
	if (state != AUTH_STATE_ACTIVE)
	{
		/* Another caller owns REVOKING; only that caller may teardown. */
		if (state == AUTH_STATE_REVOKING)
		{
			if (from_work_item)
				return STATUS_DEVICE_BUSY;
			KeWaitForSingleObject(&g_revoke_work_done, Executive,
				KernelMode, FALSE, NULL);
			NTSTATUS teardown_status = (NTSTATUS)g_teardown_status;
			if (NT_SUCCESS(teardown_status))
				InterlockedExchange(&g_authorization_state, AUTH_STATE_IDLE);
			return teardown_status;
		}
		if (state == AUTH_STATE_IDLE &&
			InterlockedCompareExchange(&g_revoke_work_initialized, TRUE, TRUE) &&
			!KeReadStateEvent(&g_revoke_work_done))
		{
			if (from_work_item)
				return STATUS_DEVICE_BUSY;
			KeWaitForSingleObject(&g_revoke_work_done, Executive,
				KernelMode, FALSE, NULL);
		}
		return STATUS_SUCCESS;
	}
	/* The ACTIVE->REVOKING winner is the sole teardown owner. */
	KeResetEvent(&g_revoke_work_done);
	return FinishAuthorizationTeardown(from_work_item);
}

static VOID RevokeWorkItem(PVOID context)
{
	UNREFERENCED_PARAMETER(context);
	(void)ShutdownAuthorization(TRUE);
	InterlockedExchange(&g_revoke_work_queued, FALSE);
	/* Final operation: no worker-owned state or image access follows this. */
	KeSetEvent(&g_revoke_work_done, IO_NO_INCREMENT, FALSE);
}

static VOID ProcessNotify(PEPROCESS process, HANDLE process_id,
	PPS_CREATE_NOTIFY_INFO create_info)
{
	UNREFERENCED_PARAMETER(process_id);

	if (!create_info && process == g_authorized_process)
	{
		InterlockedExchange(&g_revoke_requested, TRUE);
		/* Removal is deferred because a notify callback must not remove itself. */
		QueueRevocationWork();
	}
}

static NTSTATUS ValidateCommand(PVOID address, Command* command, BOOLEAN* handshake)
{
	NTSTATUS status;
	PEPROCESS caller_process = PsGetCurrentProcess();
	PEPROCESS authorized_process = g_authorized_process;

	if (!authorized_process || caller_process != authorized_process)
	{
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriver: unauthorized process %p (expected %p)\r\n",
			caller_process, authorized_process);
		return STATUS_ACCESS_DENIED;
	}

	status = CopyCommandFromKernel(address, command);
	if (!NT_SUCCESS(status))
		return status;

	*handshake = (command->op == COMMAND_ISLOADED && command->auth_token == 0);
	if (command->auth_token != g_session_token && !*handshake)
	{
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriver: invalid session token from %p\r\n", caller_process);
		return STATUS_ACCESS_DENIED;
	}

	if (command->magic != COMMAND_MAGIC)
		return STATUS_INVALID_PARAMETER;
	if (command->version != PROTOCOL_VERSION)
		return STATUS_REVISION_MISMATCH;
	if (command->size != sizeof(Command))
		return STATUS_INFO_LENGTH_MISMATCH;
	if (command->reserved != 0 || command->reserved_result != 0)
		return STATUS_INVALID_PARAMETER;
	if ((command->flags & ~COMMAND_FLAG_WRITE) != 0)
		return STATUS_INVALID_PARAMETER;
	if ((command->op == COMMAND_ISLOADED || command->op == COMMAND_GETPROCPID) &&
		command->flags != 0)
		return STATUS_INVALID_PARAMETER;
	if (command->op != COMMAND_READWRITE && command->op != COMMAND_GETPROCPID &&
		command->op != COMMAND_ISLOADED)
		return STATUS_INVALID_DEVICE_REQUEST;
	if (!IsUserRange(command->user_result, sizeof(Command)))
		return STATUS_ACCESS_VIOLATION;

	return STATUS_SUCCESS;
}

__int64 __fastcall hook(__int64 a, __int64 b, __int64 c)
{
	if (!ExAcquireRundownProtection(&g_authorization_rundown))
		return 0;

	/* Read the original only while rundown protects the mapped image. */
	t_Win32FreePool original = Win32FreePool;
	if (InterlockedCompareExchange(&g_authorization_state,
		AUTH_STATE_ACTIVE, AUTH_STATE_ACTIVE) != AUTH_STATE_ACTIVE)
		return CallOriginalAndRelease(original, a, b, c);

	PVOID command_address = (PVOID)(ULONG_PTR)a;
	Command command = { 0 };
	BOOLEAN handshake = FALSE;
	NTSTATUS status = ValidateCommand(command_address, &command, &handshake);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriver: command rejected: 0x%08X\r\n", status);
		return CallOriginalAndRelease(original, a, b, c);
	}

	if (handshake)
	{
		status = CompleteCommand(command.user_result, STATUS_SUCCESS, 0, TRUE);
		return CallOriginalAndRelease(original, a, b, c);
	}

	if (command.op == COMMAND_READWRITE)
	{
		SIZE_T length = 0;
		SIZE_T transferred = 0;

		if (command.flags != 0 && command.flags != COMMAND_FLAG_WRITE)
			status = STATUS_INVALID_PARAMETER;
		else if (command.len == 0 || command.len > READWRITE_MAX_OPERATION_SIZE ||
			command.len > (uint64_t)(SIZE_T)-1)
			status = STATUS_INVALID_BUFFER_SIZE;
		else if (command.flags & COMMAND_FLAG_WRITE)
		{
			length = (SIZE_T)command.len;
			status = IsUserRange(command.src, command.len) &&
				IsUserRange(command.dst, command.len)
				? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
		}
		else
		{
			length = (SIZE_T)command.len;
			status = IsUserRange(command.src, command.len) &&
				IsUserRange(command.dst, command.len)
				? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
		}

		if (NT_SUCCESS(status))
		{
			FixRegister();
			if (command.flags & COMMAND_FLAG_WRITE)
				status = WriteProcessMemory((HANDLE)(ULONG_PTR)command.pid,
					(PVOID)(ULONG_PTR)command.dst,
					(PVOID)(ULONG_PTR)command.src, length, &transferred);
			else
				status = ReadProcessMemory((HANDLE)(ULONG_PTR)command.pid,
					(PVOID)(ULONG_PTR)command.src,
					(PVOID)(ULONG_PTR)command.dst, length, &transferred);
		}

		NTSTATUS operation_status = status;
		status = CompleteCommand(command.user_result, operation_status, transferred, FALSE);
		return CallOriginalAndRelease(original, a, b, c);
	}

	if (command.op == COMMAND_GETPROCPID)
	{
		PEPROCESS process = NULL;
		PVOID peb = NULL;
		NTSTATUS lookup_status = PsLookupProcessByProcessId(
			(HANDLE)(ULONG_PTR)command.pid, &process);

		if (NT_SUCCESS(lookup_status))
		{
			peb = PsGetProcessPeb(process);
			ObDereferenceObject(process);
		}

		status = CompleteCommand(command.user_result, lookup_status,
			(uint64_t)(ULONG_PTR)peb, FALSE);
		return CallOriginalAndRelease(original, a, b, c);
	}

	status = CompleteCommand(command.user_result, STATUS_INVALID_DEVICE_REQUEST, 0, FALSE);
	return CallOriginalAndRelease(original, a, b, c);
}

uintptr_t GetPIDByName(char* imagename)
{
	NTSTATUS status;
	PRTL_PROCESS_MODULES ModuleInfo;

	if (!imagename)
		return 0;

	ModuleInfo = ExAllocatePool2(POOL_FLAG_PAGED, 1024 * 1024, 'mIpR');

	if (!ModuleInfo)
	{
#ifdef DEBUG
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Fail\r\n");
#endif
		return 0;
	}

	if (!NT_SUCCESS(status = ZwQuerySystemInformation(11, ModuleInfo, 1024 * 1024, NULL))) // 11 = SystemModuleInformation
	{
#ifdef _DEBUG
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Fail 2\r\n");
#endif
		ExFreePool(ModuleInfo);
		return 0;
	}

	for (ULONG i = 0; i < ModuleInfo->NumberOfModules; i++)
	{
		/*DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "ImageBase: 0x%p\r\n", ModuleInfo->Modules[i].ImageBase);
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Image Name: %s\r\n", ModuleInfo->Modules[i].FullPathName + ModuleInfo->Modules[i].OffsetToFileName);*/
		if (!strcmp(ModuleInfo->Modules[i].FullPathName + ModuleInfo->Modules[i].OffsetToFileName, imagename))
		{
#ifdef _DEBUG
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Found %s\r\n", imagename);
#endif
			uintptr_t imagebase = (uintptr_t)ModuleInfo->Modules[i].ImageBase;
			ExFreePool(ModuleInfo);
			return imagebase;
		}

	}

	ExFreePool(ModuleInfo);

	return 0;
}

#define WINDOWS_21H1       19043

typedef struct _WIN32K_OFFSET_ENTRY
{
	ULONG BuildNumber;
	uintptr_t Win32FreePoolOffset;
} WIN32K_OFFSET_ENTRY;

static const WIN32K_OFFSET_ENTRY Win32kOffsets[] =
{
	{ WINDOWS_21H1,     0x2B3C90 },
};

static NTSTATUS GetWin32FreePoolOffset(uintptr_t* offset)
{
	RTL_OSVERSIONINFOW version = { 0 };
	ULONG i;

	if (!offset)
		return STATUS_INVALID_PARAMETER;

	*offset = 0;
	version.dwOSVersionInfoSize = sizeof(version);
	if (!NT_SUCCESS(RtlGetVersion(&version)))
		return STATUS_NOT_SUPPORTED;
	if (version.dwBuildNumber < WINDOWS_21H1)
		return STATUS_NOT_SUPPORTED;

	for (i = 0; i < RTL_NUMBER_OF(Win32kOffsets); ++i)
	{
		if (Win32kOffsets[i].BuildNumber != version.dwBuildNumber)
			continue;

		if (!Win32kOffsets[i].Win32FreePoolOffset)
			return STATUS_NOT_SUPPORTED;

		*offset = Win32kOffsets[i].Win32FreePoolOffset;
		return STATUS_SUCCESS;
	}

	// Do not apply an offset from another kernel build.
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EntryPoint(DWORD32 pid)
{
	if (pid == 0)
		return ShutdownAuthorization(FALSE);

	for (;;)
	{
		LONG state = InterlockedCompareExchange(&g_authorization_state,
			AUTH_STATE_IDLE, AUTH_STATE_IDLE);
		if (state == AUTH_STATE_IDLE)
		{
			if (InterlockedCompareExchange(&g_revoke_work_initialized,
				TRUE, TRUE) && !KeReadStateEvent(&g_revoke_work_done))
			{
				KeWaitForSingleObject(&g_revoke_work_done, Executive,
					KernelMode, FALSE, NULL);
				continue;
			}
			if (InterlockedCompareExchange(&g_authorization_state,
				AUTH_STATE_INSTALLING, AUTH_STATE_IDLE) == AUTH_STATE_IDLE)
				break;
			continue;
		}
		if (state != AUTH_STATE_REVOKING)
			return STATUS_DEVICE_BUSY;

		KeWaitForSingleObject(&g_revoke_work_done, Executive,
			KernelMode, FALSE, NULL);
		NTSTATUS teardown_status = (NTSTATUS)g_teardown_status;
		if (!NT_SUCCESS(teardown_status))
			return teardown_status;
		InterlockedCompareExchange(&g_authorization_state,
			AUTH_STATE_IDLE, AUTH_STATE_REVOKING);
	}

	/* IDLE is reached only after completion; reinitialize subsequent cycles. */
	if (InterlockedCompareExchange(&g_rundown_initialized, TRUE, FALSE) == FALSE)
		ExInitializeRundownProtection(&g_authorization_rundown);
	else
		ExReInitializeRundownProtection(&g_authorization_rundown);
	InterlockedExchange(&g_rundown_completed, FALSE);
	InterlockedExchange(&g_notify_unregister_status, STATUS_SUCCESS);
	InterlockedExchange(&g_revoke_requested, FALSE);
	InterlockedExchange(&g_revoke_work_queued, FALSE);
	if (InterlockedCompareExchange(&g_revoke_work_initialized, TRUE, FALSE) == FALSE)
		KeInitializeEvent(&g_revoke_work_done, NotificationEvent, TRUE);
	ExInitializeWorkItem(&g_revoke_work_item, RevokeWorkItem, NULL);
	if (!ExAcquireRundownProtection(&g_authorization_rundown))
	{
		if (InterlockedCompareExchange(&g_authorization_state,
			AUTH_STATE_REVOKING, AUTH_STATE_INSTALLING) == AUTH_STATE_INSTALLING)
		{
			KeResetEvent(&g_revoke_work_done);
			return FinishAuthorizationTeardown(FALSE);
		}
		return STATUS_DEVICE_BUSY;
	}

	g_authorized_pid = (HANDLE)(ULONG_PTR)pid;
	GenerateSessionToken();

#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Entry point of ReadWriteDriver %d\r\n", pid);
#endif

	PEPROCESS out = NULL;
	NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &out);
	if (!NT_SUCCESS(status) || !out)
		goto install_failure;

	g_authorized_process = out;
	if (!g_process_notify_registered)
	{
		status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
			goto install_failure;
		g_process_notify_registered = TRUE;
	}

	KAPC_STATE state;
	KeStackAttachProcess(out, &state);

	uintptr_t win32k_imagebase = GetPIDByName("win32kbase.sys");
	if (!win32k_imagebase)
	{
		status = STATUS_NOT_FOUND;
		KeUnstackDetachProcess(&state);
		goto install_failure;
	}

	uintptr_t win32freepool_offset = 0;
	status = GetWin32FreePoolOffset(&win32freepool_offset);
	if (!NT_SUCCESS(status))
	{
		KeUnstackDetachProcess(&state);
		goto install_failure;
	}

	uintptr_t ptr_win32freepool = win32k_imagebase + win32freepool_offset; // See win32kbase!NtUserSetSysColors. The ptr we swap is a global that
	PVOID original_pool = InterlockedCompareExchangePointer(
		(PVOID volatile*)ptr_win32freepool, NULL, NULL);
	if (!original_pool)
	{
		KeUnstackDetachProcess(&state);
		status = STATUS_NOT_FOUND;
		goto install_failure;
	}

	Win32FreePool = (t_Win32FreePool)original_pool;
	InterlockedExchangePointer((PVOID volatile*)&g_win32freepool_slot,
		(PVOID)ptr_win32freepool);
	PVOID exchanged_pool = InterlockedExchangePointer(
		(PVOID volatile*)ptr_win32freepool, (PVOID)&hook);
	if (exchanged_pool != original_pool)
	{
		InterlockedExchangePointer((PVOID volatile*)ptr_win32freepool, exchanged_pool);
		KeUnstackDetachProcess(&state);
		status = STATUS_DEVICE_BUSY;
		goto install_failure;
	}

	KeUnstackDetachProcess(&state);
	if (InterlockedCompareExchange(&g_revoke_requested, FALSE, FALSE))
	{
		status = STATUS_PROCESS_IS_TERMINATING;
		goto install_failure;
	}

	if (InterlockedCompareExchange(&g_authorization_state,
		AUTH_STATE_ACTIVE, AUTH_STATE_INSTALLING) != AUTH_STATE_INSTALLING)
	{
		status = STATUS_PROCESS_IS_TERMINATING;
		goto install_failure;
	}
	ExReleaseRundownProtection(&g_authorization_rundown);

	/* A notify can set the flag while INSTALLING; re-check after publishing ACTIVE. */
	if (InterlockedCompareExchange(&g_revoke_requested, FALSE, FALSE) ||
		InterlockedCompareExchange(&g_authorization_state,
			AUTH_STATE_ACTIVE, AUTH_STATE_ACTIVE) != AUTH_STATE_ACTIVE)
	{
		status = STATUS_PROCESS_IS_TERMINATING;
		{
			NTSTATUS cleanup_status = ShutdownAuthorization(FALSE);
			return NT_SUCCESS(cleanup_status) ? status : cleanup_status;
		}
	}

	return STATUS_SUCCESS;

install_failure:
	ExReleaseRundownProtection(&g_authorization_rundown);
	if (InterlockedCompareExchange(&g_authorization_state,
		AUTH_STATE_REVOKING, AUTH_STATE_INSTALLING) != AUTH_STATE_INSTALLING)
		return status;
	{
		KeResetEvent(&g_revoke_work_done);
		NTSTATUS teardown_status = FinishAuthorizationTeardown(FALSE);
		return NT_SUCCESS(teardown_status) ? status : teardown_status;
	}
}
