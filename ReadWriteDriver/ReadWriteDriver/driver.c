#include "driver.h"
#include "physmem.h"

t_Win32FreePool Win32FreePool;
MmAllocateIndependentPages_t MmAllocateIndependentPages;

__int64 __fastcall hook(__int64 a, __int64 b, __int64 c)
{
	Command* cmd = (Command*)a;
	if (cmd->cmdId == COMMAND_READWRITE)
	{
		//__debugbreak();
		FixRegister(); // Fixes a register in xxxSetSysColors to prevent it from messing system colors up

#ifdef _DEBUG
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "R/W: %d | Process ID: %d | Source: 0x%X | Destination: 0x%X | Size: 0x%X\r\n",
			cmd->rw, cmd->pid, cmd->pSource, cmd->destination, cmd->size);

		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Return address: 0x%X\r\n", _ReturnAddress());
#endif

		NTSTATUS status = 0;
		SIZE_T Result;

		if (cmd->rw == 0)
		{
			status = ReadProcessMemory(cmd->pid, cmd->pSource, cmd->destination, cmd->size, &Result);
			//status = MmCopyVirtualMemory(Process, cmd->pSource, PsGetCurrentProcess(), cmd->destination, cmd->size, UserMode, &Result);
		}
		else if (cmd->rw == 1)
		{
			status = WriteProcessMemory(cmd->pid, cmd->destination, cmd->pSource, cmd->size, &Result);
			//status = MmCopyVirtualMemory(PsGetCurrentProcess(), cmd->pSource, Process, cmd->destination, cmd->size, UserMode, &Result);
		}

		Command* o_cmd = cmd->selfref;
		o_cmd->pid = status;

		cmd->cmdId = 0; // Reset this to prevent it from being called again since in NtUserSetSysColors the pointer is called twice

	}
	else if (cmd->cmdId == COMMAND_GETPROCPID)
	{
		FixRegister(); // Fixes a register in xxxSetSysColors to prevent it from messing system colors up

		PEPROCESS Process;
		PsLookupProcessByProcessId(cmd->pid, &Process);

		Command* o_cmd = cmd->selfref;
		o_cmd->pid = PsGetProcessPeb(Process);
#ifdef _DEBUG
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "PEB Address: 0x%p\r\n", o_cmd->pid);
#endif

		cmd->cmdId = 0; // Reset this to prevent it from being called again since in NtUserSetSysColors the pointer is called twice
	}

	return Win32FreePool(a, b, c);
}

uintptr_t GetPIDByName(char* imagename)
{
	NTSTATUS status;
	PRTL_PROCESS_MODULES ModuleInfo;

	if (!imagename)
		return 0;

	ModuleInfo = ExAllocatePool(PagedPool, 1024 * 1024); // Allocate memory for the module list

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

	for (int i = 0; i < ModuleInfo->NumberOfModules; i++)
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

#define WINDOWS_1909       18363
#define WINDOWS_21H1       19043
#define WINDOWS_11_22H2   22621

typedef struct _WIN32K_OFFSET_ENTRY
{
	ULONG BuildNumber;
	uintptr_t Win32FreePoolOffset;
} WIN32K_OFFSET_ENTRY;

static const WIN32K_OFFSET_ENTRY Win32kOffsets[] =
{
	{ WINDOWS_1909,     0x2B3C90 },
	{ WINDOWS_21H1,     0x2B3C90 },
	// TODO: PDB_OFFSETS - resolve the Win11 22H2 win32kbase symbol before enabling this build.
	{ WINDOWS_11_22H2,  0 }
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
#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Entry point of ReadWriteDriver %d\r\n", pid);
#endif

	PEPROCESS out = NULL;
	NTSTATUS status = PsLookupProcessByProcessId(pid, &out);
	if (!NT_SUCCESS(status) || !out)
		return NT_SUCCESS(status) ? STATUS_NOT_FOUND : status;

	KAPC_STATE state;
	KeStackAttachProcess(out, &state);

	uintptr_t win32k_imagebase = GetPIDByName("win32kbase.sys");
	if (!win32k_imagebase)
	{
		KeUnstackDetachProcess(&state);
		ObDereferenceObject(out);
		return STATUS_NOT_FOUND;
	}

	uintptr_t win32freepool_offset = 0;
	status = GetWin32FreePoolOffset(&win32freepool_offset);
	if (!NT_SUCCESS(status))
	{
		KeUnstackDetachProcess(&state);
		ObDereferenceObject(out);
		return status;
	}

	uintptr_t ptr_win32freepool = win32k_imagebase + win32freepool_offset; // See win32kbase!NtUserSetSysColors. The ptr we swap is a global that
	Win32FreePool = *(t_Win32FreePool*)ptr_win32freepool;      // holds the address to Win32FreePool
	if (!Win32FreePool)
	{
		KeUnstackDetachProcess(&state);
		ObDereferenceObject(out);
		return STATUS_NOT_FOUND;
	}

	*(t_Win32FreePool*)ptr_win32freepool = &hook;

	KeUnstackDetachProcess(&state);
	ObDereferenceObject(out);

	return STATUS_SUCCESS;
}
