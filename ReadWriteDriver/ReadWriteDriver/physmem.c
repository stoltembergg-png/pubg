#include "physmem.h"

static NTSTATUS ReadPhysicalAddressKernel(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesRead);
static NTSTATUS WritePhysicalAddressKernel(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesWritten);
uint64_t TranslateLinearAddress(uint64_t directoryTableBase, uint64_t virtualAddress);

static BOOLEAN IsUserRange(PVOID Address, SIZE_T Size)
{
	ULONG_PTR start = (ULONG_PTR)Address;
	ULONG_PTR highest = (ULONG_PTR)MM_HIGHEST_USER_ADDRESS;

	if (!Address || Size == 0 || start > highest)
		return FALSE;
	return (Size - 1) <= (highest - start);
}

static NTSTATUS CopyKernelToUser(PVOID UserBuffer, PVOID KernelBuffer, SIZE_T Size)
{
	if (!IsUserRange(UserBuffer, Size) || !KernelBuffer)
		return STATUS_INVALID_PARAMETER;

	__try
	{
		ProbeForWrite(UserBuffer, Size, 1);
		RtlCopyMemory(UserBuffer, KernelBuffer, Size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}
	return STATUS_SUCCESS;
}

static NTSTATUS CopyUserToKernel(PVOID KernelBuffer, PVOID UserBuffer, SIZE_T Size)
{
	if (!KernelBuffer || !IsUserRange(UserBuffer, Size))
		return STATUS_INVALID_PARAMETER;

	__try
	{
		ProbeForRead(UserBuffer, Size, 1);
		RtlCopyMemory(KernelBuffer, UserBuffer, Size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}
	return STATUS_SUCCESS;
}

NTKERNELAPI
PVOID
PsGetProcessSectionBaseAddress(
	__in PEPROCESS Process
);

PVOID GetProcessBaseAddress(int pid)
{
	PEPROCESS pProcess = NULL;
	if (pid == 0) return NULL;

	NTSTATUS NtRet = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &pProcess);
	if (!NT_SUCCESS(NtRet)) return NULL;

	PVOID Base = PsGetProcessSectionBaseAddress(pProcess);
	ObDereferenceObject(pProcess);
	return Base;
}

//https://ntdiff.github.io/
#define WINDOWS_21H1 19043

unsigned long GetUserDirectoryTableBaseOffset()
{
	RTL_OSVERSIONINFOW ver = { 0 };
	NTSTATUS status;

	ver.dwOSVersionInfoSize = sizeof(ver);
	status = RtlGetVersion(&ver);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriver: RtlGetVersion failed: 0x%08X\r\n", status);
		return 0;
	}
	if (ver.dwBuildNumber != WINDOWS_21H1)
		return 0;

	switch (ver.dwBuildNumber)
	{
	case WINDOWS_21H1:
		return 0x0388;
		break;
	default:
		// TODO: PDB_OFFSETS - add the UserDirectoryTableBase offset for new builds.
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriver: unsupported build %lu; refusing DirectoryTableBase fallback\r\n",
			ver.dwBuildNumber);
		return 0;
	}
}

//check normal dirbase if 0 then get from UserDirectoryTableBas
ULONG_PTR GetProcessCr3(PEPROCESS pProcess)
{
	if (!pProcess)
		return 0;

	PUCHAR process = (PUCHAR)pProcess;
	ULONG_PTR process_dirbase = *(PULONG_PTR)(process + 0x28); //dirbase x64, 32bit is 0x18
	if (process_dirbase == 0)
	{
		unsigned long UserDirOffset = GetUserDirectoryTableBaseOffset();
		if (UserDirOffset == 0)
			return 0;

		ULONG_PTR process_userdirbase = *(PULONG_PTR)(process + UserDirOffset);
		return process_userdirbase;
	}
	return process_dirbase;
}

ULONG_PTR GetKernelDirBase()
{
	PUCHAR process = (PUCHAR)PsGetCurrentProcess();
	ULONG_PTR cr3 = *(PULONG_PTR)(process + 0x28); //dirbase x64, 32bit is 0x18
	return cr3;
}

static BOOLEAN IsNonPagedKernelRange(PVOID Buffer, SIZE_T Size)
{
	ULONG_PTR start = (ULONG_PTR)Buffer;
	ULONG_PTR end;
	ULONG_PTR page;

	if (!Buffer || Size == 0 || Size > READWRITE_MAX_OPERATION_SIZE ||
		start <= (ULONG_PTR)MM_HIGHEST_USER_ADDRESS)
		return FALSE;
	end = start + Size - 1;
	if (end < start)
		return FALSE;

	for (page = start & ~(PAGE_SIZE - 1); ; page += PAGE_SIZE)
	{
		if (!MmIsNonPagedSystemAddressValid((PVOID)page))
			return FALSE;
		if (end - page < PAGE_SIZE)
			break;
	}
	return TRUE;
}

NTSTATUS ReadVirtual(uint64_t dirbase, uint64_t address, uint8_t* buffer, SIZE_T size, SIZE_T* read)
{
	if (!IsNonPagedKernelRange(buffer, size) || !read)
		return STATUS_INVALID_PARAMETER;
	uint64_t paddress = TranslateLinearAddress(dirbase, address);
	return ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)paddress, buffer, size, read);
}

NTSTATUS WriteVirtual(uint64_t dirbase, uint64_t address, uint8_t* buffer, SIZE_T size, SIZE_T* written)
{
	if (!IsNonPagedKernelRange(buffer, size) || !written)
		return STATUS_INVALID_PARAMETER;
	uint64_t paddress = TranslateLinearAddress(dirbase, address);
	return WritePhysicalAddressKernel((PVOID)(ULONG_PTR)paddress, buffer, size, written);
}

static NTSTATUS ReadPhysicalAddressKernel(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesRead)
{
	if (!TargetAddress || !lpBuffer || !BytesRead || Size == 0)
		return STATUS_INVALID_PARAMETER;

	*BytesRead = 0;
	MM_COPY_ADDRESS AddrToRead = { 0 };
	AddrToRead.PhysicalAddress.QuadPart = (LONGLONG)(ULONG_PTR)TargetAddress;

	NTSTATUS status = MmCopyMemory(lpBuffer, AddrToRead, Size,
		MM_COPY_MEMORY_PHYSICAL, BytesRead);
	if (!NT_SUCCESS(status))
		return status;

	if (*BytesRead != Size)
		return STATUS_PARTIAL_COPY;

	return STATUS_SUCCESS;
}

//MmMapIoSpaceEx limit is page 4096 byte
static NTSTATUS WritePhysicalAddressKernel(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesWritten)
{
	if (!TargetAddress || !lpBuffer || !BytesWritten || Size == 0)
		return STATUS_INVALID_PARAMETER;

	*BytesWritten = 0;

	PHYSICAL_ADDRESS AddrToWrite = { 0 };
	AddrToWrite.QuadPart = (LONGLONG)(ULONG_PTR)TargetAddress;

	PVOID pmapped_mem = MmMapIoSpaceEx(AddrToWrite, Size, PAGE_READWRITE);

	if (!pmapped_mem)
		return STATUS_INSUFFICIENT_RESOURCES;

	RtlCopyMemory(pmapped_mem, lpBuffer, Size);

	*BytesWritten = Size;
	MmUnmapIoSpace(pmapped_mem, Size);
	return STATUS_SUCCESS;
}

NTSTATUS ReadPhysicalAddress(PVOID TargetAddress, PVOID UserBuffer, SIZE_T Size, SIZE_T* BytesRead)
{
	if (!TargetAddress || !UserBuffer || !BytesRead || Size == 0 ||
		Size > READWRITE_MAX_OPERATION_SIZE || !IsUserRange(UserBuffer, Size))
		return STATUS_INVALID_PARAMETER;

	PVOID kernel_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, 'rWpr');
	if (!kernel_buffer)
		return STATUS_INSUFFICIENT_RESOURCES;

	NTSTATUS status = ReadPhysicalAddressKernel(TargetAddress, kernel_buffer, Size, BytesRead);
	if (*BytesRead != 0)
	{
		NTSTATUS copy_status = CopyKernelToUser(UserBuffer, kernel_buffer, *BytesRead);
		if (!NT_SUCCESS(copy_status))
			status = copy_status;
	}

	ExFreePool(kernel_buffer);
	return status;
}

NTSTATUS WritePhysicalAddress(PVOID TargetAddress, PVOID UserBuffer, SIZE_T Size, SIZE_T* BytesWritten)
{
	if (!TargetAddress || !UserBuffer || !BytesWritten || Size == 0 ||
		Size > READWRITE_MAX_OPERATION_SIZE || !IsUserRange(UserBuffer, Size))
		return STATUS_INVALID_PARAMETER;

	PVOID kernel_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, 'rWpw');
	if (!kernel_buffer)
		return STATUS_INSUFFICIENT_RESOURCES;

	NTSTATUS status = CopyUserToKernel(kernel_buffer, UserBuffer, Size);
	if (NT_SUCCESS(status))
		status = WritePhysicalAddressKernel(TargetAddress, kernel_buffer, Size, BytesWritten);
	else
		*BytesWritten = 0;

	ExFreePool(kernel_buffer);
	return status;
}

#define PAGE_OFFSET_SIZE 12
static const uint64_t PMASK = 0x000FFFFFFFFFF000ULL;

uint64_t TranslateLinearAddress(uint64_t directoryTableBase, uint64_t virtualAddress) {
	directoryTableBase &= ~0xfffULL;

	uint64_t pageOffset = virtualAddress & ~(~0ul << PAGE_OFFSET_SIZE);
	uint64_t pte = ((virtualAddress >> 12) & (0x1ffll));
	uint64_t pt = ((virtualAddress >> 21) & (0x1ffll));
	uint64_t pd = ((virtualAddress >> 30) & (0x1ffll));
	uint64_t pdp = ((virtualAddress >> 39) & (0x1ffll));

	SIZE_T readsize = 0;
	uint64_t pdpte = 0;
	if (!NT_SUCCESS(ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)(directoryTableBase + 8 * pdp), &pdpte, sizeof(pdpte), &readsize)) || readsize != sizeof(pdpte))
		return 0;
	if (~pdpte & 1)
		return 0;

	/* PS is tested on the PDPTE; the 1 GiB mask clears the PAT/30-bit offset. */
	if (pdpte & 0x80)
		return (pdpte & 0x000FFFFFC0000000ULL) + (virtualAddress & 0x3FFFFFFFULL);

	uint64_t pde = 0;
	if (!NT_SUCCESS(ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)((pdpte & PMASK) + 8 * pd), &pde, sizeof(pde), &readsize)) || readsize != sizeof(pde))
		return 0;
	if (~pde & 1)
		return 0;

	uint64_t pteAddr = 0;
	if (!NT_SUCCESS(ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)((pde & PMASK) + 8 * pt), &pteAddr, sizeof(pteAddr), &readsize)) || readsize != sizeof(pteAddr))
		return 0;
	if (~pteAddr & 1)
		return 0;

	/* 2MB large page; mask the PAT bit and all 21 offset bits. */
	if (pteAddr & 0x80)
		return (pteAddr & 0x000FFFFFFFE00000ULL) + (virtualAddress & 0x1FFFFFULL);

	uint64_t final_pte = 0;
	if (!NT_SUCCESS(ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)((pteAddr & PMASK) + 8 * pte), &final_pte, sizeof(final_pte), &readsize)) || readsize != sizeof(final_pte))
		return 0;
	if (!(final_pte & 1))
		return 0;

	return (final_pte & PMASK) + pageOffset;
}


//
NTSTATUS ReadProcessMemory(HANDLE pid, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* read)
{
	PEPROCESS pProcess = NULL;
	if (pid == 0 || !Address || !AllocatedBuffer || !read || size == 0 ||
		size > READWRITE_MAX_OPERATION_SIZE || !IsUserRange(Address, size) ||
		!IsUserRange(AllocatedBuffer, size)) return STATUS_INVALID_PARAMETER;
	*read = 0;

	NTSTATUS NtRet = PsLookupProcessByProcessId(pid, &pProcess);
	if (NtRet != STATUS_SUCCESS) return NtRet;

	ULONG_PTR process_dirbase = GetProcessCr3(pProcess);
	if (!process_dirbase)
	{
		ObDereferenceObject(pProcess);
		return STATUS_UNSUCCESSFUL;
	}

	PUCHAR kernel_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, 'rWrm');
	if (!kernel_buffer)
	{
		ObDereferenceObject(pProcess);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	SIZE_T CurOffset = 0;
	SIZE_T TotalSize = size;
	NtRet = STATUS_SUCCESS;
	while (TotalSize)
	{

		uint64_t CurPhysAddr = TranslateLinearAddress(process_dirbase, (ULONG64)Address + CurOffset);
		if (!CurPhysAddr)
		{
			NtRet = STATUS_UNSUCCESSFUL;
			break;
		}

		ULONG64 ReadSize = min(PAGE_SIZE - (CurPhysAddr & 0xFFF), TotalSize);
		SIZE_T BytesRead = 0;
		NtRet = ReadPhysicalAddressKernel((PVOID)(ULONG_PTR)CurPhysAddr, kernel_buffer, ReadSize, &BytesRead);
		if (NT_SUCCESS(NtRet) && BytesRead != 0)
			NtRet = CopyKernelToUser((PVOID)((ULONG64)AllocatedBuffer + CurOffset), kernel_buffer, BytesRead);
		TotalSize -= BytesRead;
		CurOffset += BytesRead;
		if (!NT_SUCCESS(NtRet)) break;
		if (BytesRead == 0) break;
	}

	ExFreePool(kernel_buffer);
	ObDereferenceObject(pProcess);
	*read = CurOffset;
	return NtRet;
}

NTSTATUS WriteProcessMemory(HANDLE pid, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* written)
{
	PEPROCESS pProcess = NULL;
	if (pid == 0 || !Address || !AllocatedBuffer || !written || size == 0 ||
		size > READWRITE_MAX_OPERATION_SIZE || !IsUserRange(Address, size) ||
		!IsUserRange(AllocatedBuffer, size)) return STATUS_INVALID_PARAMETER;
	*written = 0;

	NTSTATUS NtRet = PsLookupProcessByProcessId(pid, &pProcess);
	if (NtRet != STATUS_SUCCESS) return NtRet;

	ULONG_PTR process_dirbase = GetProcessCr3(pProcess);
	if (!process_dirbase)
	{
		ObDereferenceObject(pProcess);
		return STATUS_UNSUCCESSFUL;
	}

	PUCHAR kernel_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, 'rWwm');
	if (!kernel_buffer)
	{
		ObDereferenceObject(pProcess);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	SIZE_T CurOffset = 0;
	SIZE_T TotalSize = size;
	NtRet = STATUS_SUCCESS;
	while (TotalSize)
	{
		uint64_t CurPhysAddr = TranslateLinearAddress(process_dirbase, (ULONG64)Address + CurOffset);
		if (!CurPhysAddr)
		{
			NtRet = STATUS_UNSUCCESSFUL;
			break;
		}

		ULONG64 WriteSize = min(PAGE_SIZE - (CurPhysAddr & 0xFFF), TotalSize);
		NtRet = CopyUserToKernel(kernel_buffer,
			(PVOID)((ULONG64)AllocatedBuffer + CurOffset), WriteSize);
		if (!NT_SUCCESS(NtRet))
			break;

		SIZE_T BytesWritten = 0;
		NtRet = WritePhysicalAddressKernel((PVOID)(ULONG_PTR)CurPhysAddr,
			kernel_buffer, WriteSize, &BytesWritten);
		TotalSize -= BytesWritten;
		CurOffset += BytesWritten;
		if (NtRet != STATUS_SUCCESS) break;
		if (BytesWritten == 0) break;
	}

	ExFreePool(kernel_buffer);
	ObDereferenceObject(pProcess);
	*written = CurOffset;
	return NtRet;
}
