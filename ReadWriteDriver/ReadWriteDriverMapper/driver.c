#include "driver.h"
#include <ntdef.h>
#include <ntifs.h>
#include <intrin.h>
#include <ntimage.h>
#include <minwindef.h>

typedef PVOID(__fastcall* MmAllocateIndependentPages_t)(IN  SIZE_T NumberOfBytes, IN  ULONG Node);
MmAllocateIndependentPages_t MmAllocateIndependentPages;
typedef VOID(__fastcall* MmFreeIndependentPages_t)(IN PVOID BaseAddress, IN SIZE_T NumberOfBytes);
MmFreeIndependentPages_t MmFreeIndependentPages;

typedef BOOLEAN(__fastcall* MmSetPageProtection_t)(__in_bcount(NumberOfBytes) PVOID VirtualAddress, __in SIZE_T NumberOfBytes, __in ULONG NewProtect);
MmSetPageProtection_t MmSetPageProtection;

extern NTSYSAPI PVOID RtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage);

PVOID allocated_memory;
SIZE_T allocated_image_size;
DWORD32 usermode_module_pid = 0;
typedef NTSTATUS(__fastcall* PayloadEntry_t)(DWORD32);
PayloadEntry_t PayloadEntry;

#define SUPPORTED_WINDOWS_BUILD 19043

static NTSTATUS ValidateSupportedBuild(VOID)
{
	RTL_OSVERSIONINFOW version = { 0 };
	version.dwOSVersionInfoSize = sizeof(version);
	if (!NT_SUCCESS(RtlGetVersion(&version)))
		return STATUS_NOT_SUPPORTED;
	return version.dwBuildNumber == SUPPORTED_WINDOWS_BUILD
		? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

static VOID ReleaseMappedImage(VOID)
{
	if (allocated_memory && MmFreeIndependentPages)
		MmFreeIndependentPages(allocated_memory, allocated_image_size);
	allocated_memory = NULL;
	allocated_image_size = 0;
	PayloadEntry = NULL;
}

static NTSTATUS ShutdownMappedPayload(VOID)
{
	return PayloadEntry ? PayloadEntry(0) : STATUS_SUCCESS;
}

void CopyHeadersAndSections(LPVOID source, LPVOID destination, PIMAGE_NT_HEADERS pNTHeader)
{
	// Don't copy PE header - could be detected
	//memcpy(destination, source, pNTHeader->OptionalHeader.SizeOfHeaders);

	PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNTHeader);
	for (WORD i = 0; i < pNTHeader->FileHeader.NumberOfSections; i++)
	{
		if (!pSectionHeader[i].SizeOfRawData)
			continue;

		memcpy((PBYTE)destination + pSectionHeader[i].VirtualAddress, (PBYTE)source + pSectionHeader[i].PointerToRawData, pSectionHeader[i].SizeOfRawData);
	}
}

UNICODE_STRING ASCIIToUnicode(char* ascii, wchar_t* memory, size_t memory_size)
{
	UNICODE_STRING str = { 0 };
	wchar_t* wchar_meme = memory;
	size_t used = 0;
	while (*ascii && used + 1 < memory_size)
		memory[used++] = (wchar_t)*ascii++;
	if (*ascii)
		return str;

	memory[used] = L'\0';
	str.Buffer = wchar_meme;
	str.Length = (USHORT)(used * sizeof(wchar_t));
	str.MaximumLength = (USHORT)((used + 1) * sizeof(wchar_t));
	return str;
}

NTSTATUS FixIAT(LPVOID destination, PIMAGE_NT_HEADERS pNTHeader)
{
	if (pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress && pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)
	{
		PIMAGE_IMPORT_DESCRIPTOR pDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)((PBYTE)destination + pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		while (pDescriptor->Name) // There should only be one (ntoskrnl), so this loop isnt really needed i guess
		{
			LPCSTR pDllName = (LPCSTR)((PBYTE)destination + pDescriptor->Name);
#ifdef _DEBUG
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "pDescriptor name is: %s\r\n", pDllName);
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "pDescriptor value is: 0x%X\r\n", pDescriptor->Name);
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "pDescriptor address is: 0x%p\r\n", pDescriptor);
#endif

			PIMAGE_THUNK_DATA pThunk;
			PIMAGE_THUNK_DATA pAddrThunk;

			if (pDescriptor->OriginalFirstThunk)
			{
				pThunk = (PIMAGE_THUNK_DATA)((PBYTE)destination + pDescriptor->OriginalFirstThunk);
			}
			else
			{
				pThunk = (PIMAGE_THUNK_DATA)((PBYTE)destination + pDescriptor->FirstThunk);
			}

			pAddrThunk = (PIMAGE_THUNK_DATA)((PBYTE)destination + pDescriptor->FirstThunk);

			while (pThunk->u1.AddressOfData)
			{
				FARPROC lpFunction = NULL;

				if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal))
				{
					return STATUS_NOT_SUPPORTED;
#ifdef _DEBUG
					DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Function ptr: %p\r\n", lpFunction);
#endif
				}
				else
				{
					PIMAGE_IMPORT_BY_NAME pImport = (PIMAGE_IMPORT_BY_NAME)((PBYTE)destination + pThunk->u1.AddressOfData);

					wchar_t buffer[128];
					UNICODE_STRING unicode_str = ASCIIToUnicode(pImport->Name, buffer, sizeof(buffer) / sizeof(wchar_t));
					if (!unicode_str.Buffer || unicode_str.Length == 0)
						return STATUS_BUFFER_OVERFLOW;

					lpFunction = MmGetSystemRoutineAddress(&unicode_str);
#ifdef _DEBUG
					DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Function ptr: %p\r\n", lpFunction);
#endif
				}

				if (!lpFunction)
					return STATUS_PROCEDURE_NOT_FOUND;

				pAddrThunk->u1.Function = (UINT_PTR)lpFunction;

				pThunk++;
				pAddrThunk++;
			}

			pDescriptor++;
		}
	}
	return STATUS_SUCCESS;
}

void FixRelocations(LPVOID destination, PIMAGE_NT_HEADERS pNTHeader)
{
	if ((UINT_PTR)destination != pNTHeader->OptionalHeader.ImageBase && pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress && pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size)
	{
		PIMAGE_BASE_RELOCATION pRelocTable = (PIMAGE_BASE_RELOCATION)((PBYTE)destination + pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
		UINT_PTR delta = (UINT_PTR)((PBYTE)destination - pNTHeader->OptionalHeader.ImageBase);

		while (pRelocTable->SizeOfBlock)
		{
			PWORD pRelocationData = (PWORD)((PBYTE)pRelocTable + sizeof(IMAGE_BASE_RELOCATION));
			DWORD NumberOfRelocationData = (pRelocTable->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

			for (DWORD i = 0; i < NumberOfRelocationData; i++)
			{
				DWORD relocationType = pRelocationData[i] >> 12;
				if (relocationType == IMAGE_REL_BASED_HIGHLOW)
				{
					PDWORD pAddress = (PDWORD)((PBYTE)destination + pRelocTable->VirtualAddress + (pRelocationData[i] & 0x0FFF));
					*pAddress += (DWORD)delta;
				}
				else if (relocationType == IMAGE_REL_BASED_DIR64)
				{
					PDWORD64 pAddress = (PDWORD64)((PBYTE)destination + pRelocTable->VirtualAddress + (pRelocationData[i] & 0x0FFF));
					*pAddress += delta;
				}
			}

			pRelocTable = (PIMAGE_BASE_RELOCATION)((PBYTE)pRelocTable + pRelocTable->SizeOfBlock);
		}
	}
}

NTSTATUS ManualMap()
{
	NTSTATUS status = ValidateSupportedBuild();
	if (!NT_SUCCESS(status))
		return status;

	PVOID ntoskrnl_base = NULL;
	if (!RtlPcToFileHeader((PVOID)&RtlPcToFileHeader, &ntoskrnl_base) || !ntoskrnl_base)
		return STATUS_NOT_FOUND;

	UNICODE_STRING free_pages_name = RTL_CONSTANT_STRING(L"MmFreeIndependentPages");
	MmFreeIndependentPages = (MmFreeIndependentPages_t)MmGetSystemRoutineAddress(
		&free_pages_name);
	if (!MmFreeIndependentPages)
		return STATUS_NOT_SUPPORTED;

	uintptr_t ntoskrnl_imagebase = (uintptr_t)ntoskrnl_base;

	PIMAGE_DOS_HEADER pDOSHeader = (PIMAGE_DOS_HEADER)&hexData;
	PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)((PBYTE)&hexData + pDOSHeader->e_lfanew);
#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "ntoskrnel.exe: 0x%p\r\n", ntoskrnl_imagebase);
#endif
	uintptr_t pMmAllocateIndependentPages = ntoskrnl_imagebase + 0x809420; // ntoskrnl!MmAllocateIndependentPages
	MmAllocateIndependentPages = (MmAllocateIndependentPages_t)(pMmAllocateIndependentPages);
	if (!MmAllocateIndependentPages)
		return STATUS_NOT_SUPPORTED;

	allocated_memory = MmAllocateIndependentPages(pNTHeader->OptionalHeader.SizeOfImage, -1);
	if (!allocated_memory)
		return STATUS_INSUFFICIENT_RESOURCES;
	allocated_image_size = pNTHeader->OptionalHeader.SizeOfImage;
#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Allocated at 0x%p\r\n", allocated_memory);
#endif

	uintptr_t pMmSetPageProtection = ntoskrnl_imagebase + 0x3B3B60; // ntoskrnl!MmSetPageProtection
	MmSetPageProtection = (MmSetPageProtection_t)(pMmSetPageProtection);
	if (!MmSetPageProtection)
	{
		status = STATUS_NOT_SUPPORTED;
		goto map_failure;
	}

	BOOLEAN result = MmSetPageProtection(allocated_memory, pNTHeader->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE);
	if (!result)
	{
		status = STATUS_UNSUCCESSFUL;
		goto map_failure;
	}
	
	CopyHeadersAndSections(&hexData, allocated_memory, pNTHeader);
	NTSTATUS import_status = FixIAT(allocated_memory, pNTHeader);
	if (!NT_SUCCESS(import_status))
	{
		status = import_status;
		goto map_failure;
	}
	FixRelocations(allocated_memory, pNTHeader);

	LPVOID dwEntryPoint = (PBYTE)allocated_memory + pNTHeader->OptionalHeader.AddressOfEntryPoint;
	PayloadEntry = (PayloadEntry_t)dwEntryPoint;
	status = PayloadEntry(usermode_module_pid);
	if (!NT_SUCCESS(status))
	{
		NTSTATUS teardown_status = ShutdownMappedPayload();
		if (!NT_SUCCESS(teardown_status))
			return teardown_status;
		goto map_failure;
	}
	return status;

map_failure:
	ReleaseMappedImage();
	return status;
}

void DriverUnload(PDRIVER_OBJECT pDriverObject)
{
	UNREFERENCED_PARAMETER(pDriverObject);
	NTSTATUS teardown_status = ShutdownMappedPayload();
	if (!NT_SUCCESS(teardown_status))
	{
#ifdef _DEBUG
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			"ReadWriteDriverMapper: payload teardown failed: 0x%08X\n",
			teardown_status);
#endif
		/* DriverUnload has no status return; retain the image on failure. */
		return;
	}
	ReleaseMappedImage();
#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Driver unload called!\n");
#endif
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT  DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	NTSTATUS regStatus = 0;
	RTL_QUERY_REGISTRY_TABLE query[2];
	if (!RegistryPath || !RegistryPath->Buffer)
		return STATUS_INVALID_PARAMETER;
	RtlZeroMemory(query, sizeof(query));

	query[0].Name = L"pid"; // L"" refers to the default value
	query[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	query[0].EntryContext = &usermode_module_pid;
	query[0].DefaultType = REG_DWORD;
	query[0].DefaultLength = sizeof(DWORD32);
	query[0].DefaultData = &usermode_module_pid;

	regStatus = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, RegistryPath->Buffer, query, NULL, NULL);
	if (!NT_SUCCESS(regStatus))
		return regStatus;
	if (usermode_module_pid == 0)
		return STATUS_INVALID_PARAMETER;

#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "regStatus: %lx\n", regStatus);
#endif

#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Path %wZ\n", *RegistryPath);
#endif

	NTSTATUS mapStatus = ManualMap();
	if (!NT_SUCCESS(mapStatus))
		return mapStatus;
#ifdef _DEBUG
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, -1, "Manual map done\n");
#endif
	DriverObject->DriverUnload = DriverUnload;
	return STATUS_CONNECTION_ACTIVE;
}
