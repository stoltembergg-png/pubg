#include "driver.h"
#include "../../tools/profiles/generated/profiles_generated.h"
#include "../ReadWriteDriver/payload_api.h"

#include <ntifs.h>
#include <ntimage.h>
#include <stdint.h>

NTSYSAPI PIMAGE_NT_HEADERS NTAPI RtlImageNtHeader(PVOID image);
NTSYSAPI PVOID NTAPI RtlImageDirectoryEntryToData(PVOID image,
    BOOLEAN mapped_as_image, USHORT directory, PULONG size);
NTSYSAPI PVOID NTAPI RtlPcToFileHeader(PVOID pc_value, PVOID* base_of_image);

#define PUBGEXT_MAP_PRINT(stage, format, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, \
        "[PubgExtMap][%s] " format "\n", stage, __VA_ARGS__)

/*
 * Diagnostic failure ABI.  0xE80Axxxx is always a failing NTSTATUS; the low
 * 16 bits identify the load stage and the original status is printed before
 * returning it.  STATUS_NOT_SUPPORTED and STATUS_REVISION_MISMATCH remain
 * public gate results.  Payload stages 0x31..0x35 are duplicated in the
 * payload source because payload_api.h is intentionally a stable ABI header.
 */
#define PUBGEXT_STAGE_STATUS_BASE ((NTSTATUS)0xE80A0000L)
typedef enum _PUBGEXT_MAP_STAGE {
    PUBGEXT_STAGE_KERNEL_RESOLVE = 0x01,
    PUBGEXT_STAGE_KERNEL_IDENTITY = 0x02,
    PUBGEXT_STAGE_HVCI = 0x03,
    PUBGEXT_STAGE_PROFILE = 0x04,
    PUBGEXT_STAGE_PROFILE_RVAS = 0x05,
    PUBGEXT_STAGE_PAYLOAD_HEADER = 0x06,
    PUBGEXT_STAGE_PAYLOAD_LAYOUT = 0x07,
    PUBGEXT_STAGE_ROUTINE_RESOLVE = 0x08,
    PUBGEXT_STAGE_ALLOCATE = 0x09,
    PUBGEXT_STAGE_FABRICATE = 0x0A,
    PUBGEXT_STAGE_PROTECT = 0x0B,
    PUBGEXT_STAGE_COPY = 0x0C,
    PUBGEXT_STAGE_RELOCATIONS = 0x0D,
    PUBGEXT_STAGE_IMPORTS = 0x0E,
    PUBGEXT_STAGE_PAYLOAD_ENTRY = 0x0F,
    PUBGEXT_STAGE_PAYLOAD_RESULT = 0x10,
    PUBGEXT_STAGE_PAYLOAD_VALIDATE = 0x31,
    PUBGEXT_STAGE_DEVICE_CREATE = 0x32,
    PUBGEXT_STAGE_DEVICE_OUTPUT = 0x33,
    PUBGEXT_STAGE_SYMBOLIC_LINK = 0x34,
    PUBGEXT_STAGE_DISPATCH_SETUP = 0x35
} PUBGEXT_MAP_STAGE;

static NTSTATUS StageFailure(PUBGEXT_MAP_STAGE stage, NTSTATUS original)
{
    PUBGEXT_MAP_PRINT("failure", "stage=0x%02X code=0x%08X original=0x%08X",
        stage, (ULONG)(PUBGEXT_STAGE_STATUS_BASE | (ULONG)stage), original);
    if (original == STATUS_NOT_SUPPORTED || original == STATUS_REVISION_MISMATCH)
        return original;
    return (NTSTATUS)(PUBGEXT_STAGE_STATUS_BASE | (ULONG)stage);
}

static BOOLEAN IsStageStatus(NTSTATUS status)
{
    return ((ULONG)status & 0xFFFF0000u) ==
        ((ULONG)PUBGEXT_STAGE_STATUS_BASE & 0xFFFF0000u);
}

typedef PVOID(__fastcall* MmAllocateIndependentPages_t)(SIZE_T, ULONG);
typedef VOID(__fastcall* MmFreeIndependentPages_t)(PVOID, SIZE_T);
typedef BOOLEAN(__fastcall* MmSetPageProtection_t)(PVOID, SIZE_T, ULONG);

#define PUBGEXT_FABRICATED_DRIVER_TAG 'dRwP'

static MmAllocateIndependentPages_t g_allocate_pages;
static MmSetPageProtection_t g_set_page_protection;
static MmFreeIndependentPages_t g_free_pages;
static PVOID g_allocated_memory;
static SIZE_T g_allocated_size;
static PDRIVER_OBJECT g_fabricated_driver_object;

typedef struct _PUBGEXT_FABRICATED_DRIVER_STORAGE {
    DRIVER_OBJECT driver_object;
    DRIVER_EXTENSION driver_extension;
} PUBGEXT_FABRICATED_DRIVER_STORAGE;

static PUBGEXT_FABRICATED_DRIVER_STORAGE* g_fabricated_driver_storage;

static NTSTATUS FabricateDriverObject(PVOID image, SIZE_T image_size,
    PDRIVER_OBJECT* driver_object)
{
    PUBGEXT_FABRICATED_DRIVER_STORAGE* storage;
    PDRIVER_OBJECT fabricated;

    if (!image || !driver_object || image_size == 0 || image_size > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    storage = (PUBGEXT_FABRICATED_DRIVER_STORAGE*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*storage), PUBGEXT_FABRICATED_DRIVER_TAG);
    if (!storage)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(storage, sizeof(*storage));
    fabricated = &storage->driver_object;
    fabricated->Type = IO_TYPE_DRIVER;
    fabricated->Size = (CSHORT)sizeof(*fabricated);
    fabricated->DriverUnload = NULL;
    fabricated->DriverStart = image;
    fabricated->DriverSize = (ULONG)image_size;
    fabricated->DriverSection = NULL;
    fabricated->DriverExtension = &storage->driver_extension;
    fabricated->DeviceObject = NULL;
    storage->driver_extension.DriverObject = fabricated;
    /* DriverName remains an empty UNICODE_STRING from the zeroed object. */

    g_fabricated_driver_object = fabricated;
    g_fabricated_driver_storage = storage;
    *driver_object = fabricated;
    return STATUS_SUCCESS;
}

static VOID ReleaseFabricatedDriverObject(VOID)
{
    if (g_fabricated_driver_object)
    {
        ExFreePool(g_fabricated_driver_storage);
        g_fabricated_driver_storage = NULL;
        g_fabricated_driver_object = NULL;
    }
}

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
    {
        NTSTATUS status = query(103, &info, sizeof(info), NULL);
        if (!NT_SUCCESS(status))
            return status;
    }
    *options = info.CodeIntegrityOptions;
    return STATUS_SUCCESS;
}

static NTSTATUS ValidateHvcIAndCi(VOID)
{
    ULONG options = 0;
    NTSTATUS status = QueryCodeIntegrity(&options);
    if (!NT_SUCCESS(status))
        return status;
    PUBGEXT_MAP_PRINT("hvci-ci", "observed options=0x%08X", options);
    /* CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED. Audit-only is not execution. */
    if (options & 0x00000400u)
    {
        PUBGEXT_MAP_PRINT("hvci-ci", "fail HVCI_KMCI_ENABLED options=0x%08X", options);
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
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

typedef struct _RSDS_LOCAL {
    ULONG signature;
    GUID guid;
    ULONG age;
} RSDS_LOCAL;

static BOOLEAN GetLoadedPdbIdentity(PVOID image, GUID* guid, ULONG* age)
{
    PIMAGE_NT_HEADERS nt;
    ULONG directory_size = 0;
    PIMAGE_DEBUG_DIRECTORY debug;
    ULONG count;
    ULONG i;
    if (!image || !guid || !age)
        return FALSE;
    nt = RtlImageNtHeader(image);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
        return FALSE;
    debug = (PIMAGE_DEBUG_DIRECTORY)RtlImageDirectoryEntryToData(
        image, TRUE, IMAGE_DIRECTORY_ENTRY_DEBUG, &directory_size);
    if (!debug || directory_size < sizeof(*debug))
        return FALSE;
    if ((ULONG_PTR)debug < (ULONG_PTR)image ||
        (ULONG_PTR)debug - (ULONG_PTR)image > nt->OptionalHeader.SizeOfImage ||
        directory_size > nt->OptionalHeader.SizeOfImage -
            ((ULONG_PTR)debug - (ULONG_PTR)image))
        return FALSE;
    count = directory_size / sizeof(*debug);
    for (i = 0; i < count; ++i)
    {
        ULONG rva;
        RSDS_LOCAL* rsds;
        if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW)
            continue;
        rva = debug[i].AddressOfRawData;
        if (!rva || nt->OptionalHeader.SizeOfImage < sizeof(*rsds) ||
            rva > nt->OptionalHeader.SizeOfImage - sizeof(*rsds))
            continue;
        rsds = (RSDS_LOCAL*)((PUCHAR)image + rva);
        if (rsds->signature != 0x53445352u) /* RSDS */
            continue;
        *guid = rsds->guid;
        *age = rsds->age;
        return TRUE;
    }
    return FALSE;
}

static NTSTATUS ValidateLoadedKernelIdentity(PVOID image, const ModuleIdentity* identity)
{
    PIMAGE_NT_HEADERS nt;
    GUID loaded_guid = { 0 };
    ULONG loaded_age = 0;
    BOOLEAN loaded_pdb = FALSE;

    if (!image || !identity)
    {
        PUBGEXT_MAP_PRINT("identity", "fail invalid input%s", "");
        return STATUS_INVALID_PARAMETER;
    }
    nt = RtlImageNtHeader(image);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
    {
        PUBGEXT_MAP_PRINT("identity", "fail invalid nt header%s", "");
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (nt->FileHeader.TimeDateStamp != identity->time_date_stamp)
    {
        PUBGEXT_MAP_PRINT("identity", "fail TDS expected=0x%08X observed=0x%08X",
            identity->time_date_stamp, nt->FileHeader.TimeDateStamp);
        return STATUS_NOT_SUPPORTED;
    }
    if (nt->OptionalHeader.SizeOfImage != identity->size_of_image)
    {
        PUBGEXT_MAP_PRINT("identity", "fail SizeOfImage expected=0x%08X observed=0x%08X",
            identity->size_of_image, nt->OptionalHeader.SizeOfImage);
        return STATUS_NOT_SUPPORTED;
    }
    if (nt->OptionalHeader.CheckSum != identity->check_sum)
    {
        PUBGEXT_MAP_PRINT("identity", "fail checksum expected=0x%08X observed=0x%08X",
            identity->check_sum, nt->OptionalHeader.CheckSum);
        return STATUS_NOT_SUPPORTED;
    }
    loaded_pdb = GetLoadedPdbIdentity(image, &loaded_guid, &loaded_age);
    if (!loaded_pdb || loaded_age != identity->pdb_age ||
        !GuidTextMatches(&loaded_guid, identity->pdb_guid))
    {
        if (loaded_pdb)
        {
            PUBGEXT_MAP_PRINT("identity",
                "fail GUID expected=%s age=%u observed=%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X age=%u",
                identity->pdb_guid ? identity->pdb_guid : "<missing>", identity->pdb_age, loaded_guid.Data1,
                loaded_guid.Data2, loaded_guid.Data3, loaded_guid.Data4[0],
                loaded_guid.Data4[1], loaded_guid.Data4[2], loaded_guid.Data4[3],
                loaded_guid.Data4[4], loaded_guid.Data4[5], loaded_age);
        }
        else
        {
            PUBGEXT_MAP_PRINT("identity", "fail GUID expected=%s age=%u observed=<unavailable>",
                identity->pdb_guid ? identity->pdb_guid : "<missing>", identity->pdb_age);
        }
        return STATUS_REVISION_MISMATCH;
    }
    {
        UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\ntoskrnl.exe");
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK io_status = { 0 };
        FILE_STANDARD_INFORMATION file_info = { 0 };
        HANDLE file = NULL;
        NTSTATUS status;
        InitializeObjectAttributes(&attributes, &path,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        status = ZwOpenFile(&file, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            &attributes, &io_status, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_SYNCHRONOUS_IO_NONALERT);
        if (!NT_SUCCESS(status))
        {
            PUBGEXT_MAP_PRINT("identity", "fail ntoskrnl file open status=0x%08X", status);
            return STATUS_NOT_FOUND;
        }
        status = ZwQueryInformationFile(file, &io_status, &file_info,
            sizeof(file_info), FileStandardInformation);
        if (!NT_SUCCESS(status))
        {
            PUBGEXT_MAP_PRINT("identity", "fail ntoskrnl file query status=0x%08X", status);
            ZwClose(file);
            return status;
        }
        ZwClose(file);
        if (file_info.EndOfFile.QuadPart != identity->file_size)
        {
            PUBGEXT_MAP_PRINT("identity", "fail file_size expected=%I64u observed=%I64u",
                (ULONGLONG)identity->file_size,
                (ULONGLONG)file_info.EndOfFile.QuadPart);
            return STATUS_REVISION_MISMATCH;
        }
    }
    PUBGEXT_MAP_PRINT("identity", "ok TDS=0x%08X SizeOfImage=0x%08X checksum=0x%08X GUID=%s age=%u",
        nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage,
        nt->OptionalHeader.CheckSum, identity->pdb_guid ? identity->pdb_guid : "<missing>", identity->pdb_age);
    return STATUS_SUCCESS;
}

static BOOLEAN RvaIsExecutable(PIMAGE_NT_HEADERS nt, ULONG rva)
{
    PIMAGE_SECTION_HEADER section;
    USHORT i;
    if (!nt)
        return FALSE;
    section = IMAGE_FIRST_SECTION(nt);
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
    if (!nt)
        return STATUS_INVALID_IMAGE_FORMAT;
    if (!generated_profile.has_mm_allocate_independent_pages_rva ||
        !generated_profile.has_mm_set_page_protection_rva ||
        !generated_profile.has_mm_free_independent_pages_rva)
    {
        PUBGEXT_MAP_PRINT("rvas", "fail missing generated RVA flags alloc=%u protect=%u free=%u",
            generated_profile.has_mm_allocate_independent_pages_rva,
            generated_profile.has_mm_set_page_protection_rva,
            generated_profile.has_mm_free_independent_pages_rva);
        return STATUS_NOT_SUPPORTED;
    }
    if (!RvaIsExecutable(nt, generated_profile.mm_allocate_independent_pages_rva))
    {
        PUBGEXT_MAP_PRINT("rvas", "fail allocate RVA=0x%08X is not executable",
            generated_profile.mm_allocate_independent_pages_rva);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (!RvaIsExecutable(nt, generated_profile.mm_set_page_protection_rva))
    {
        PUBGEXT_MAP_PRINT("rvas", "fail protect RVA=0x%08X is not executable",
            generated_profile.mm_set_page_protection_rva);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    if (!RvaIsExecutable(nt, generated_profile.mm_free_independent_pages_rva))
    {
        PUBGEXT_MAP_PRINT("rvas", "fail free RVA=0x%08X is not executable",
            generated_profile.mm_free_independent_pages_rva);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
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
    if (!nt)
        return FALSE;
    return rva <= nt->OptionalHeader.SizeOfImage &&
        size <= nt->OptionalHeader.SizeOfImage - rva;
}

static NTSTATUS ValidateImageLayout(PVOID source, SIZE_T source_size,
    PIMAGE_NT_HEADERS nt)
{
    PIMAGE_SECTION_HEADER section;
    USHORT i;

    if (!source || !nt || nt->Signature != IMAGE_NT_SIGNATURE ||
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
    NTSTATUS status = ValidateImageLayout(source, source_size, nt);
    PIMAGE_SECTION_HEADER section;
    USHORT i;

    if (!NT_SUCCESS(status) || !destination)
    {
        if (NT_SUCCESS(status))
            status = STATUS_INVALID_PARAMETER;
        return status;
    }
    section = IMAGE_FIRST_SECTION(nt);
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

    if (!destination || !nt)
        return STATUS_INVALID_PARAMETER;
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
                    thunk->u1.AddressOfData <= MAXULONG -
                        FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) - (ULONG)chars &&
                    RvaRangeValid(nt, thunk->u1.AddressOfData +
                        FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + (ULONG)chars,
                        sizeof(UCHAR)) && import->Name[chars])
                {
                    buffer[chars] = (WCHAR)import->Name[chars];
                    ++chars;
                }
                if (chars == 0 || thunk->u1.AddressOfData > MAXULONG -
                    FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) - (ULONG)chars ||
                    !RvaRangeValid(nt, thunk->u1.AddressOfData +
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
    ULONG_PTR delta;
    if (!destination || !nt)
        return STATUS_INVALID_PARAMETER;
    delta = (ULONG_PTR)destination - nt->OptionalHeader.ImageBase;
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
    PIMAGE_NT_HEADERS kernel_nt = NULL;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)&hexData;
    PIMAGE_NT_HEADERS payload_nt;
    PUBGEXT_PAYLOAD_INIT init = { 0 };
    PUBGEXT_PAYLOAD_INIT_RESULT result = { 0 };
    PUBGEXT_PAYLOAD_ENTRY entry;
    PDRIVER_OBJECT effective_driver_object = driver_object;
    NTSTATUS status;

    PUBGEXT_MAP_PRINT("manual-map", "entry driver_object=%p", driver_object);

    /* The OS build is informational. Enablement packages can report SO=26200
       while the loaded ntoskrnl file identity remains 26100. */
    version.dwOSVersionInfoSize = sizeof(version);
    status = RtlGetVersion(&version);
    if (NT_SUCCESS(status))
        PUBGEXT_MAP_PRINT("version", "ok os_build=%lu profile_file_version=%s",
            version.dwBuildNumber, generated_profile.ntoskrnl.file_version ?
                generated_profile.ntoskrnl.file_version : "<missing>");
    else
        PUBGEXT_MAP_PRINT("version", "fail informational RtlGetVersion status=0x%08X; identity gate continues", status);

    PUBGEXT_MAP_PRINT("kernel-image", "begin resolving loaded ntoskrnl%s", "");
    if (!RtlPcToFileHeader((PVOID)&RtlPcToFileHeader, &kernel_base) || !kernel_base)
    {
        PUBGEXT_MAP_PRINT("kernel-image", "fail RtlPcToFileHeader status=0x%08X",
            STATUS_NOT_FOUND);
        return StageFailure(PUBGEXT_STAGE_KERNEL_RESOLVE, STATUS_NOT_FOUND);
    }
    kernel_nt = RtlImageNtHeader(kernel_base);
    if (!kernel_nt)
    {
        PUBGEXT_MAP_PRINT("kernel-image", "fail RtlImageNtHeader status=0x%08X",
            STATUS_INVALID_IMAGE_FORMAT);
        return StageFailure(PUBGEXT_STAGE_KERNEL_RESOLVE,
            STATUS_INVALID_IMAGE_FORMAT);
    }
    PUBGEXT_MAP_PRINT("kernel-image", "ok base=%p", kernel_base);

    /* Fail closed on the loaded image identity before resolving any profile RVA. */
    status = ValidateLoadedKernelIdentity(kernel_base, &generated_profile.ntoskrnl);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("identity", "fail status=0x%08X", status);
        return StageFailure(PUBGEXT_STAGE_KERNEL_IDENTITY, status);
    }

    /* Fail closed when kernel-mode HVCI is active. */
    PUBGEXT_MAP_PRINT("hvci-ci", "begin%s", "");
    status = ValidateHvcIAndCi();
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("hvci-ci", "fail status=0x%08X", status);
        return StageFailure(PUBGEXT_STAGE_HVCI, status);
    }
    PUBGEXT_MAP_PRINT("hvci-ci", "ok%s", "");

    /* Exact generated profile selection. */
    if (!generated_profile.profile_id || generated_profile.ntoskrnl.file_size == 0)
    {
        PUBGEXT_MAP_PRINT("profile", "fail missing profile identity status=0x%08X",
            STATUS_NOT_SUPPORTED);
        return StageFailure(PUBGEXT_STAGE_PROFILE, STATUS_NOT_SUPPORTED);
    }
    PUBGEXT_MAP_PRINT("profile", "ok id=%s file_size=%lu", generated_profile.profile_id,
        generated_profile.ntoskrnl.file_size);

    /* Every generated RVA must resolve to an executable section. */
    PUBGEXT_MAP_PRINT("rvas", "begin%s", "");
    status = ValidateProfileRvas(kernel_nt);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("rvas", "fail status=0x%08X", status);
        return StageFailure(PUBGEXT_STAGE_PROFILE_RVAS, status);
    }
    PUBGEXT_MAP_PRINT("rvas", "ok allocate=0x%08X protect=0x%08X free=0x%08X",
        generated_profile.mm_allocate_independent_pages_rva,
        generated_profile.mm_set_page_protection_rva,
        generated_profile.mm_free_independent_pages_rva);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        (SIZE_T)dos->e_lfanew > sizeof(hexData) - sizeof(IMAGE_NT_HEADERS))
    {
        PUBGEXT_MAP_PRINT("payload-header", "fail DOS header status=0x%08X",
            STATUS_INVALID_IMAGE_FORMAT);
        return StageFailure(PUBGEXT_STAGE_PAYLOAD_HEADER,
            STATUS_INVALID_IMAGE_FORMAT);
    }
    payload_nt = (PIMAGE_NT_HEADERS)((PUCHAR)&hexData + dos->e_lfanew);
    if (payload_nt->Signature != IMAGE_NT_SIGNATURE ||
        payload_nt->OptionalHeader.SizeOfImage == 0)
    {
        PUBGEXT_MAP_PRINT("payload-header", "fail NT header status=0x%08X",
            STATUS_INVALID_IMAGE_FORMAT);
        return StageFailure(PUBGEXT_STAGE_PAYLOAD_HEADER,
            STATUS_INVALID_IMAGE_FORMAT);
    }
    PUBGEXT_MAP_PRINT("payload-header", "ok image_size=0x%08X entry=0x%08X",
        payload_nt->OptionalHeader.SizeOfImage,
        payload_nt->OptionalHeader.AddressOfEntryPoint);
    status = ValidateImageLayout(&hexData, sizeof(hexData), payload_nt);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("payload-layout", "fail status=0x%08X", status);
        return StageFailure(PUBGEXT_STAGE_PAYLOAD_LAYOUT, status);
    }
    if (!RvaRangeValid(payload_nt, payload_nt->OptionalHeader.AddressOfEntryPoint,
        sizeof(UCHAR)))
    {
        PUBGEXT_MAP_PRINT("payload-layout", "fail entry RVA status=0x%08X",
            STATUS_INVALID_IMAGE_FORMAT);
        return StageFailure(PUBGEXT_STAGE_PAYLOAD_LAYOUT,
            STATUS_INVALID_IMAGE_FORMAT);
    }
    if (!RvaIsExecutable(payload_nt, payload_nt->OptionalHeader.AddressOfEntryPoint))
    {
        PUBGEXT_MAP_PRINT("payload-layout", "fail entry RVA is not executable status=0x%08X",
            STATUS_INVALID_IMAGE_FORMAT);
        return StageFailure(PUBGEXT_STAGE_PAYLOAD_LAYOUT,
            STATUS_INVALID_IMAGE_FORMAT);
    }
    PUBGEXT_MAP_PRINT("payload-layout", "ok%s", "");

    g_allocate_pages = (MmAllocateIndependentPages_t)((PUCHAR)kernel_base + generated_profile.mm_allocate_independent_pages_rva);
    g_set_page_protection = (MmSetPageProtection_t)((PUCHAR)kernel_base + generated_profile.mm_set_page_protection_rva);
    g_free_pages = (MmFreeIndependentPages_t)((PUCHAR)kernel_base + generated_profile.mm_free_independent_pages_rva);
    if (!g_allocate_pages || !g_set_page_protection || !g_free_pages)
    {
        PUBGEXT_MAP_PRINT("rvas", "fail resolved routine pointer status=0x%08X",
            STATUS_PROCEDURE_NOT_FOUND);
        return StageFailure(PUBGEXT_STAGE_ROUTINE_RESOLVE,
            STATUS_PROCEDURE_NOT_FOUND);
    }

    /* Map only after all preflight checks have passed. */
    g_allocated_size = payload_nt->OptionalHeader.SizeOfImage;
    PUBGEXT_MAP_PRINT("allocation", "begin size=0x%Ix", g_allocated_size);
    g_allocated_memory = g_allocate_pages(g_allocated_size, (ULONG)-1);
    if (!g_allocated_memory)
    {
        PUBGEXT_MAP_PRINT("allocation", "fail status=0x%08X",
            STATUS_INSUFFICIENT_RESOURCES);
        return StageFailure(PUBGEXT_STAGE_ALLOCATE,
            STATUS_INSUFFICIENT_RESOURCES);
    }
    PUBGEXT_MAP_PRINT("allocation", "ok base=%p", g_allocated_memory);

    if (!effective_driver_object)
    {
        status = FabricateDriverObject(g_allocated_memory, g_allocated_size,
            &effective_driver_object);
        if (!NT_SUCCESS(status))
        {
            PUBGEXT_MAP_PRINT("driver-object",
                "fail fabricate status=0x%08X tag=0x%08X", status,
                PUBGEXT_FABRICATED_DRIVER_TAG);
            status = StageFailure(PUBGEXT_STAGE_FABRICATE, status);
            goto map_failure;
        }
        PUBGEXT_MAP_PRINT("driver-object",
            "ok fabricated=%p type=%u size=%u start=%p driver_size=%lu",
            effective_driver_object, effective_driver_object->Type,
            effective_driver_object->Size,
            effective_driver_object->DriverStart,
            effective_driver_object->DriverSize);
    }
    else
    {
        PUBGEXT_MAP_PRINT("driver-object", "ok received=%p", effective_driver_object);
    }

    PUBGEXT_MAP_PRINT("protection", "begin%s", "");
    if (!g_set_page_protection(g_allocated_memory, g_allocated_size, PAGE_EXECUTE_READWRITE))
    {
        status = STATUS_UNSUCCESSFUL;
        PUBGEXT_MAP_PRINT("protection", "fail status=0x%08X", status);
        status = StageFailure(PUBGEXT_STAGE_PROTECT, status);
        goto map_failure;
    }
    PUBGEXT_MAP_PRINT("protection", "ok%s", "");
    PUBGEXT_MAP_PRINT("copy", "begin%s", "");
    status = CopyHeadersAndSections(&hexData, sizeof(hexData),
        g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("copy", "fail status=0x%08X", status);
        status = StageFailure(PUBGEXT_STAGE_COPY, status);
        goto map_failure;
    }
    PUBGEXT_MAP_PRINT("copy", "ok%s", "");
    PUBGEXT_MAP_PRINT("relocations", "begin%s", "");
    status = FixRelocations(g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("relocations", "fail status=0x%08X", status);
        status = StageFailure(PUBGEXT_STAGE_RELOCATIONS, status);
        goto map_failure;
    }
    PUBGEXT_MAP_PRINT("relocations", "ok%s", "");
    PUBGEXT_MAP_PRINT("imports", "begin%s", "");
    status = FixIat(g_allocated_memory, payload_nt);
    if (!NT_SUCCESS(status))
    {
        PUBGEXT_MAP_PRINT("imports", "fail status=0x%08X", status);
        status = StageFailure(PUBGEXT_STAGE_IMPORTS, status);
        goto map_failure;
    }
    PUBGEXT_MAP_PRINT("imports", "ok%s", "");

    entry = (PUBGEXT_PAYLOAD_ENTRY)((PUCHAR)g_allocated_memory + payload_nt->OptionalHeader.AddressOfEntryPoint);
    init.struct_size = sizeof(init);
    init.abi_major = PUBGEXT_PAYLOAD_ABI_MAJOR;
    init.abi_minor = PUBGEXT_PAYLOAD_ABI_MINOR;
    init.driver_object = (uint64_t)(ULONG_PTR)effective_driver_object;
    PUBGEXT_MAP_PRINT("payload-init", "begin entry=%p", entry);
    status = (NTSTATUS)entry(&init, &result);
    PUBGEXT_MAP_PRINT("payload-init", "returned status=0x%08X payload_status=0x%08X device_created=%lu",
        status, (NTSTATUS)result.status, result.device_created);
    if (!NT_SUCCESS(status) || !result.device_created)
    {
        /* The payload creates the device atomically; failed init leaves none. */
        status = NT_SUCCESS(status) ? STATUS_UNSUCCESSFUL : status;
        if (!IsStageStatus(status))
            status = StageFailure(result.device_created ?
                PUBGEXT_STAGE_PAYLOAD_ENTRY : PUBGEXT_STAGE_PAYLOAD_RESULT, status);
        PUBGEXT_MAP_PRINT("payload-init", "fail status=0x%08X", status);
        PUBGEXT_MAP_PRINT("device", "fail payload_status=0x%08X created=%lu",
            (NTSTATUS)result.status, result.device_created);
        goto map_failure;
    }
    PUBGEXT_MAP_PRINT("device", "ok created=1 status=0x%08X", STATUS_SUCCESS);
    PUBGEXT_MAP_PRINT("manual-map", "ok status=0x%08X", STATUS_SUCCESS);
    return STATUS_SUCCESS;

map_failure:
    /* (7) a failed payload never leaves the mapped image or a device behind. */
    PUBGEXT_MAP_PRINT("cleanup", "release mapped image status=0x%08X", status);
    ReleaseFabricatedDriverObject();
    ReleaseMappedImage();
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    UNREFERENCED_PARAMETER(registry_path);
    PUBGEXT_MAP_PRINT("entry", "begin driver_object=%p", driver_object);
    if (driver_object)
    {
        /* Mapper unload is disabled because its mapped payload is intentionally resident. */
        driver_object->DriverUnload = NULL;
        PUBGEXT_MAP_PRINT("entry", "received DriverUnload=NULL%s", "");
    }
    else
    {
        PUBGEXT_MAP_PRINT("entry", "kdmapper path: driver_object=NULL; factory path%s", "");
    }
    return ManualMap(driver_object);
}
