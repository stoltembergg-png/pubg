#include "driver.h"
#include "../../tools/profiles/generated/profiles_generated.h"
#include "../ReadWriteDriver/payload_api.h"

#include <ntifs.h>
#include <ntimage.h>
#include <stdint.h>

NTSYSAPI PIMAGE_NT_HEADERS NTAPI RtlImageNtHeader(PVOID image);
NTSYSAPI PVOID NTAPI RtlImageDirectoryEntryToData(PVOID image,
    BOOLEAN mapped_as_image, USHORT directory, PULONG size);

typedef PVOID(__fastcall* MmAllocateIndependentPages_t)(SIZE_T, ULONG);
typedef VOID(__fastcall* MmFreeIndependentPages_t)(PVOID, SIZE_T);
typedef BOOLEAN(__fastcall* MmSetPageProtection_t)(PVOID, SIZE_T, ULONG);

static MmAllocateIndependentPages_t g_allocate_pages;
static MmSetPageProtection_t g_set_page_protection;
static MmFreeIndependentPages_t g_free_pages;
static PVOID g_allocated_memory;
static SIZE_T g_allocated_size;

typedef struct _SYSTEM_CODEINTEGRITY_INFORMATION_LOCAL {
    ULONG Length;
    ULONG CodeIntegrityOptions;
} SYSTEM_CODEINTEGRITY_INFORMATION_LOCAL;

typedef NTSTATUS(*ZwQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);

static NTSTATUS QueryCodeIntegrity(ULONG* options)
{
    SYSTEM_CODEINTEGRITY_INFORMATION_LOCAL info = { 0 };
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"ZwQuerySystemInformation");
    ZwQuerySystemInformation_t query;
    if (!options)
        return STATUS_INVALID_PARAMETER;
    query = (ZwQuerySystemInformation_t)MmGetSystemRoutineAddress(&name);
    if (!query)
        return STATUS_PROCEDURE_NOT_FOUND;
    info.Length = sizeof(info);
    if (!NT_SUCCESS(query(103, &info, sizeof(info), NULL)))
        return STATUS_NOT_SUPPORTED;
    *options = info.CodeIntegrityOptions;
    return STATUS_SUCCESS;
}

static NTSTATUS ValidateHvcIAndCi(VOID)
{
    ULONG options = 0;
    NTSTATUS status = QueryCodeIntegrity(&options);
    if (!NT_SUCCESS(status))
        return status;
    /* CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED. Audit-only is not execution. */
    return (options & 0x00000400u) ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static BOOLEAN GuidTextMatches(const GUID* guid, const char* text)
{
    ULONG d1 = 0;
    USHORT d2 = 0, d3 = 0;
    UCHAR d4[8] = { 0 };
    ULONG i;
    if (!guid || !text)
        return FALSE;
    for (i = 0; i < 8; ++i) { int h = HexValue(text[i]); int l = HexValue(text[i + 1]); if (h < 0 || l < 0) return FALSE; d1 = (d1 << 8) | (ULONG)((h << 4) | l); i++; }
    if (text[8] != '-') return FALSE;
    for (i = 9; i < 13; ++i) { int h = HexValue(text[i]); int l = HexValue(text[i + 1]); if (h < 0 || l < 0) return FALSE; d2 = (USHORT)((d2 << 8) | ((h << 4) | l)); i++; }
    if (text[13] != '-') return FALSE;
    for (i = 14; i < 18; ++i) { int h = HexValue(text[i]); int l = HexValue(text[i + 1]); if (h < 0 || l < 0) return FALSE; d3 = (USHORT)((d3 << 8) | ((h << 4) | l)); i++; }
    if (text[18] != '-') return FALSE;
    for (i = 0; i < 2; ++i) { int h = HexValue(text[19 + i * 2]); int l = HexValue(text[20 + i * 2]); if (h < 0 || l < 0) return FALSE; d4[i] = (UCHAR)((h << 4) | l); }
    if (text[23] != '-') return FALSE;
    for (i = 0; i < 6; ++i) { int h = HexValue(text[24 + i * 2]); int l = HexValue(text[25 + i * 2]); if (h < 0 || l < 0) return FALSE; d4[i + 2] = (UCHAR)((h << 4) | l); }
    return guid->Data1 == d1 && guid->Data2 == d2 && guid->Data3 == d3 &&
        RtlCompareMemory(guid->Data4, d4, sizeof(d4)) == sizeof(d4);
}

static BOOLEAN LoadedPdbMatches(PVOID image, const ModuleIdentity* identity)
{
    ULONG directory_size = 0;
    PIMAGE_DEBUG_DIRECTORY debug = (PIMAGE_DEBUG_DIRECTORY)
        RtlImageDirectoryEntryToData(image, TRUE, IMAGE_DIRECTORY_ENTRY_DEBUG, &directory_size);
    ULONG count;
    ULONG i;
    if (!debug || directory_size < sizeof(*debug) || !identity->pdb_guid)
        return FALSE;
    count = directory_size / sizeof(*debug);
    for (i = 0; i < count; ++i)
    {
        ULONG rva;
        PULONG signature;
        if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW)
            continue;
        rva = debug[i].AddressOfRawData;
        if (!rva)
            continue;
        signature = (PULONG)((PUCHAR)image + rva);
        if (*signature != 0x53445352u) /* RSDS */
            continue;
        {
            typedef struct _RSDS_LOCAL { ULONG signature; GUID guid; ULONG age; } RSDS_LOCAL;
            RSDS_LOCAL* rsds = (RSDS_LOCAL*)signature;
            return rsds->age == identity->pdb_age &&
                GuidTextMatches(&rsds->guid, identity->pdb_guid);
        }
    }
    return FALSE;
}

static NTSTATUS ValidateLoadedKernelIdentity(PVOID image, const ModuleIdentity* identity)
{
    PIMAGE_NT_HEADERS nt = RtlImageNtHeader(image);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;
    if (nt->FileHeader.TimeDateStamp != identity->time_date_stamp ||
        nt->OptionalHeader.SizeOfImage != identity->size_of_image ||
        nt->OptionalHeader.CheckSum != identity->check_sum ||
        !LoadedPdbMatches(image, identity))
        return STATUS_REVISION_MISMATCH;
    {
        UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\ntoskrnl.exe");
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK io_status = { 0 };
        FILE_STANDARD_INFORMATION file_info = { 0 };
        HANDLE file = NULL;
        InitializeObjectAttributes(&attributes, &path,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        if (!NT_SUCCESS(ZwOpenFile(&file, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            &attributes, &io_status, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_SYNCHRONOUS_IO_NONALERT)) ||
            !NT_SUCCESS(ZwQueryInformationFile(file, &io_status, &file_info,
                sizeof(file_info), FileStandardInformation)))
        {
            if (file) ZwClose(file);
            return STATUS_NOT_FOUND;
        }
        ZwClose(file);
        if (file_info.EndOfFile.QuadPart != identity->file_size)
            return STATUS_REVISION_MISMATCH;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN RvaIsExecutable(PIMAGE_NT_HEADERS nt, ULONG rva)
{
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    USHORT i;
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        ULONG size = section[i].Misc.VirtualSize > section[i].SizeOfRawData ?
            section[i].Misc.VirtualSize : section[i].SizeOfRawData;
        if (rva >= section[i].VirtualAddress &&
            rva - section[i].VirtualAddress < size &&
            (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            return TRUE;
    }
    return FALSE;
}

static NTSTATUS ValidateProfileRvas(PIMAGE_NT_HEADERS nt)
{
    if (!generated_profile.has_mm_allocate_independent_pages_rva ||
        !generated_profile.has_mm_set_page_protection_rva ||
        !generated_profile.has_mm_free_independent_pages_rva)
        return STATUS_NOT_SUPPORTED;
    if (!RvaIsExecutable(nt, generated_profile.mm_allocate_independent_pages_rva) ||
        !RvaIsExecutable(nt, generated_profile.mm_set_page_protection_rva) ||
        !RvaIsExecutable(nt, generated_profile.mm_free_independent_pages_rva))
        return STATUS_INVALID_IMAGE_FORMAT;
    return STATUS_SUCCESS;
}

static VOID ReleaseMappedImage(VOID)
{
    if (g_allocated_memory && g_free_pages)
        g_free_pages(g_allocated_memory, g_allocated_size);
    g_allocated_memory = NULL;
    g_allocated_size = 0;
}

static BOOLEAN RvaRangeValid(PIMAGE_NT_HEADERS nt, ULONG rva, ULONG size)
{
    return rva <= nt->OptionalHeader.SizeOfImage &&
        size <= nt->OptionalHeader.SizeOfImage - rva;
}

static NTSTATUS ValidateImageLayout(PVOID source, SIZE_T source_size,
    PIMAGE_NT_HEADERS nt)
{
    PIMAGE_SECTION_HEADER section;
    USHORT i;

    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage == 0 ||
        nt->OptionalHeader.SizeOfHeaders == 0 ||
        nt->OptionalHeader.FileAlignment == 0 ||
        nt->OptionalHeader.SectionAlignment == 0 ||
        nt->OptionalHeader.SectionAlignment < nt->OptionalHeader.FileAlignment ||
        nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage ||
        nt->OptionalHeader.SizeOfHeaders > source_size ||
        (nt->OptionalHeader.SizeOfHeaders % nt->OptionalHeader.FileAlignment) != 0 ||
        (nt->OptionalHeader.SizeOfImage % nt->OptionalHeader.SectionAlignment) != 0)
        return STATUS_INVALID_IMAGE_FORMAT;
    section = IMAGE_FIRST_SECTION(nt);
    if ((PUCHAR)section + nt->FileHeader.NumberOfSections * sizeof(*section) >
        (PUCHAR)source + source_size)
        return STATUS_INVALID_IMAGE_FORMAT;
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        ULONG span = section[i].Misc.VirtualSize > section[i].SizeOfRawData ?
            section[i].Misc.VirtualSize : section[i].SizeOfRawData;
        if (section[i].VirtualAddress % nt->OptionalHeader.SectionAlignment != 0 ||
            section[i].VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            span > nt->OptionalHeader.SizeOfImage - section[i].VirtualAddress ||
            (section[i].SizeOfRawData &&
                (section[i].PointerToRawData > source_size ||
                 section[i].SizeOfRawData > source_size - section[i].PointerToRawData)) ||
            (section[i].SizeOfRawData &&
                (section[i].PointerToRawData % nt->OptionalHeader.FileAlignment) != 0))
            return STATUS_INVALID_IMAGE_FORMAT;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS CopyHeadersAndSections(PVOID source, SIZE_T source_size,
    PVOID destination, PIMAGE_NT_HEADERS nt)
{
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    NTSTATUS status = ValidateImageLayout(source, source_size, nt);
    USHORT i;

    if (!NT_SUCCESS(status))
        return status;
    RtlZeroMemory(destination, nt->OptionalHeader.SizeOfImage);
    RtlCopyMemory(destination, source, nt->OptionalHeader.SizeOfHeaders);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        ULONG span = section[i].Misc.VirtualSize > section[i].SizeOfRawData ?
            section[i].Misc.VirtualSize : section[i].SizeOfRawData;
        if (section[i].SizeOfRawData)
            RtlCopyMemory((PUCHAR)destination + section[i].VirtualAddress,
                (PUCHAR)source + section[i].PointerToRawData,
                section[i].SizeOfRawData);
        if (span > section[i].SizeOfRawData)
            RtlZeroMemory((PUCHAR)destination + section[i].VirtualAddress +
                section[i].SizeOfRawData, span - section[i].SizeOfRawData);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS FixIat(PVOID destination, PIMAGE_NT_HEADERS nt)
{
    IMAGE_DATA_DIRECTORY directory;
    PIMAGE_IMPORT_DESCRIPTOR descriptor;
    ULONG descriptor_count;
    ULONG descriptor_index;

    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return STATUS_SUCCESS;
    directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !directory.Size)
        return STATUS_SUCCESS;
    if (!RvaRangeValid(nt, directory.VirtualAddress, directory.Size) ||
        directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
        return STATUS_INVALID_IMAGE_FORMAT;
    descriptor = (PIMAGE_IMPORT_DESCRIPTOR)((PUCHAR)destination +
        directory.VirtualAddress);
    descriptor_count = directory.Size / sizeof(*descriptor);
    for (descriptor_index = 0; descriptor_index < descriptor_count;
        ++descriptor_index, ++descriptor)
    {
        ULONG thunk_rva;
        ULONG address_thunk_rva;
        if (!descriptor->Name)
            break;
        if (!RvaRangeValid(nt, descriptor->Name, sizeof(UCHAR)) ||
            !descriptor->FirstThunk)
            return STATUS_INVALID_IMAGE_FORMAT;
        thunk_rva = descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk :
            descriptor->FirstThunk;
        address_thunk_rva = descriptor->FirstThunk;
        while (TRUE)
        {
            PVOID function = NULL;
            PIMAGE_THUNK_DATA thunk;
            PIMAGE_THUNK_DATA address_thunk;
            if (!RvaRangeValid(nt, thunk_rva, sizeof(IMAGE_THUNK_DATA)) ||
                !RvaRangeValid(nt, address_thunk_rva, sizeof(IMAGE_THUNK_DATA)))
                return STATUS_INVALID_IMAGE_FORMAT;
            thunk = (PIMAGE_THUNK_DATA)((PUCHAR)destination + thunk_rva);
            address_thunk = (PIMAGE_THUNK_DATA)((PUCHAR)destination +
                address_thunk_rva);
            if (!thunk->u1.AddressOfData)
                break;
            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
                return STATUS_NOT_SUPPORTED;
            {
                PIMAGE_IMPORT_BY_NAME import = (PIMAGE_IMPORT_BY_NAME)
                    ((PUCHAR)destination + thunk->u1.AddressOfData);
                UNICODE_STRING name = { 0 };
                WCHAR buffer[128];
                SIZE_T chars = 0;
                if (!RvaRangeValid(nt, thunk->u1.AddressOfData, sizeof(USHORT)))
                    return STATUS_INVALID_IMAGE_FORMAT;
                while (chars + 1 < RTL_NUMBER_OF(buffer) &&
                    RvaRangeValid(nt, thunk->u1.AddressOfData +
                        FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + (ULONG)chars,
                        sizeof(UCHAR)) && import->Name[chars])
                {
                    buffer[chars] = (WCHAR)import->Name[chars];
                    ++chars;
                }
                if (chars == 0 || !RvaRangeValid(nt, thunk->u1.AddressOfData +
                    FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + (ULONG)chars,
                    sizeof(UCHAR)) || import->Name[chars])
                    return STATUS_BUFFER_OVERFLOW;
                buffer[chars] = L'\0';
                name.Buffer = buffer;
                name.Length = (USHORT)(chars * sizeof(WCHAR));
                name.MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
                function = MmGetSystemRoutineAddress(&name);
            }
            if (!function)
                return STATUS_PROCEDURE_NOT_FOUND;
            address_thunk->u1.Function = (ULONGLONG)(ULONG_PTR)function;
            if (thunk_rva > MAXULONG - sizeof(IMAGE_THUNK_DATA) ||
                address_thunk_rva > MAXULONG - sizeof(IMAGE_THUNK_DATA))
                return STATUS_INVALID_IMAGE_FORMAT;
            thunk_rva += sizeof(IMAGE_THUNK_DATA);
            address_thunk_rva += sizeof(IMAGE_THUNK_DATA);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS FixRelocations(PVOID destination, PIMAGE_NT_HEADERS nt)
{
    IMAGE_DATA_DIRECTORY directory;
    ULONG directory_size;
    PIMAGE_BASE_RELOCATION table;
    ULONG_PTR delta = (ULONG_PTR)destination - nt->OptionalHeader.ImageBase;
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_BASERELOC)
        return STATUS_SUCCESS;
    directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!directory.VirtualAddress || !directory.Size)
        return STATUS_SUCCESS;
    if (!RvaRangeValid(nt, directory.VirtualAddress, directory.Size))
        return STATUS_INVALID_IMAGE_FORMAT;
    table = (PIMAGE_BASE_RELOCATION)((PUCHAR)destination + directory.VirtualAddress);
    directory_size = directory.Size;
    while (directory_size >= sizeof(*table))
    {
        if (table->SizeOfBlock < sizeof(*table) ||
            table->SizeOfBlock > directory_size ||
            ((table->SizeOfBlock - sizeof(*table)) % sizeof(USHORT)) != 0)
            return STATUS_INVALID_IMAGE_FORMAT;
        ULONG count = (table->SizeOfBlock - sizeof(*table)) / sizeof(USHORT);
        PUSHORT entries = (PUSHORT)((PUCHAR)table + sizeof(*table));
        ULONG i;
        for (i = 0; i < count; ++i)
        {
            ULONG type = entries[i] >> 12;
            ULONG offset = entries[i] & 0xfff;
            ULONG rva;
            if (table->VirtualAddress > MAXULONG - offset)
                return STATUS_INVALID_IMAGE_FORMAT;
            rva = table->VirtualAddress + offset;
            if (type == IMAGE_REL_BASED_DIR64)
            {
                if (!delta || !RvaRangeValid(nt, rva, sizeof(ULONGLONG)))
                {
                    if (delta)
                        return STATUS_INVALID_IMAGE_FORMAT;
                    continue;
                }
                *(PULONG_PTR)((PUCHAR)destination + rva) += delta;
            }
            else if (type != IMAGE_REL_BASED_ABSOLUTE)
                return STATUS_NOT_SUPPORTED;
        }
        directory_size -= table->SizeOfBlock;
        table = (PIMAGE_BASE_RELOCATION)((PUCHAR)table + table->SizeOfBlock);
    }
    if (directory_size != 0)
        return STATUS_INVALID_IMAGE_FORMAT;
    return STATUS_SUCCESS;
}

static NTSTATUS ManualMap(PDRIVER_OBJECT driver_object)
{
    RTL_OSVERSIONINFOW version = { 0 };
    PVOID kernel_base = NULL;
    PIMAGE_NT_HEADERS kernel_nt;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)&hexData;
    PIMAGE_NT_HEADERS payload_nt;
    PUBGEXT_PAYLOAD_INIT init = { 0 };
    PUBGEXT_PAYLOAD_INIT_RESULT result = { 0 };
    PUBGEXT_PAYLOAD_ENTRY entry;
    NTSTATUS status;

    /* (1) version supported: derive the build from the generated profile. */
    version.dwOSVersionInfoSize = sizeof(version);
    if (!NT_SUCCESS(RtlGetVersion(&version)) || !generated_profile.ntoskrnl.file_version)
        return STATUS_NOT_SUPPORTED;
    {
        ULONG profile_build = 0;
        const char* p = generated_profile.ntoskrnl.file_version;
        while (*p && *p != '.') ++p;
        if (*p) ++p; while (*p && *p != '.') ++p;
        if (*p) ++p; while (*p >= '0' && *p <= '9') { profile_build = profile_build * 10 + (ULONG)(*p - '0'); ++p; }
        if (!profile_build || version.dwBuildNumber != profile_build)
            return STATUS_NOT_SUPPORTED;
    }
    /* (2) fail closed when kernel-mode HVCI is active. */
    status = ValidateHvcIAndCi();
    if (!NT_SUCCESS(status))
        return status;
    if (!RtlPcToFileHeader((PVOID)&RtlPcToFileHeader, &kernel_base) || !kernel_base)
        return STATUS_NOT_FOUND;
    kernel_nt = RtlImageNtHeader(kernel_base);
    if (!kernel_nt)
        return STATUS_INVALID_IMAGE_FORMAT;
    /* (3) loaded ntoskrnl identity and (4) exact generated profile selection. */
    status = ValidateLoadedKernelIdentity(kernel_base, &generated_profile.ntoskrnl);
    if (!NT_SUCCESS(status))
        return status;
    if (!generated_profile.profile_id || generated_profile.ntoskrnl.file_size == 0)
        return STATUS_NOT_SUPPORTED;
    /* (5) every generated RVA must resolve to an executable section. */
    status = ValidateProfileRvas(kernel_nt);
    if (!NT_SUCCESS(status))
        return status;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        (SIZE_T)dos->e_lfanew > sizeof(hexData) - sizeof(IMAGE_NT_HEADERS))
        return STATUS_INVALID_IMAGE_FORMAT;
    payload_nt = (PIMAGE_NT_HEADERS)((PUCHAR)&hexData + dos->e_lfanew);
    if (payload_nt->Signature != IMAGE_NT_SIGNATURE ||
        payload_nt->OptionalHeader.SizeOfImage == 0)
        return STATUS_INVALID_IMAGE_FORMAT;
    status = ValidateImageLayout(&hexData, sizeof(hexData), payload_nt);
    if (!NT_SUCCESS(status))
        return status;
    if (!RvaRangeValid(payload_nt, payload_nt->OptionalHeader.AddressOfEntryPoint,
        sizeof(UCHAR)))
        return STATUS_INVALID_IMAGE_FORMAT;

    g_allocate_pages = (MmAllocateIndependentPages_t)((PUCHAR)kernel_base + generated_profile.mm_allocate_independent_pages_rva);
    g_set_page_protection = (MmSetPageProtection_t)((PUCHAR)kernel_base + generated_profile.mm_set_page_protection_rva);
    g_free_pages = (MmFreeIndependentPages_t)((PUCHAR)kernel_base + generated_profile.mm_free_independent_pages_rva);
    /* (6) map only after all preflight checks have passed. */
    g_allocated_size = payload_nt->OptionalHeader.SizeOfImage;
    g_allocated_memory = g_allocate_pages(g_allocated_size, (ULONG)-1);
    if (!g_allocated_memory)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!g_set_page_protection(g_allocated_memory, g_allocated_size, PAGE_EXECUTE_READWRITE))
    {
        status = STATUS_UNSUCCESSFUL;
        goto map_failure;
    }
    status = CopyHeadersAndSections(&hexData, sizeof(hexData),
        g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status)) goto map_failure;
    status = FixRelocations(g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status)) goto map_failure;
    status = FixIat(g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status)) goto map_failure;

    entry = (PUBGEXT_PAYLOAD_ENTRY)((PUCHAR)g_allocated_memory + payload_nt->OptionalHeader.AddressOfEntryPoint);
    init.struct_size = sizeof(init);
    init.abi_major = PUBGEXT_PAYLOAD_ABI_MAJOR;
    init.abi_minor = PUBGEXT_PAYLOAD_ABI_MINOR;
    init.driver_object = (uint64_t)(ULONG_PTR)driver_object;
    status = (NTSTATUS)entry(&init, &result);
    if (!NT_SUCCESS(status) || !result.device_created)
    {
        /* The payload creates the device atomically; failed init leaves none. */
        status = NT_SUCCESS(status) ? STATUS_UNSUCCESSFUL : status;
        goto map_failure;
    }
    return STATUS_SUCCESS;

map_failure:
    /* (7) a failed payload never leaves the mapped image or a device behind. */
    ReleaseMappedImage();
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    UNREFERENCED_PARAMETER(registry_path);
    /* Mapper unload is disabled because its mapped payload is intentionally resident. */
    driver_object->DriverUnload = NULL;
    return ManualMap(driver_object);
}
