#include "driver_interface_v3.h"

#include "common.h"

#include <winternl.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace
{
    // This is the user-mode signature used by ReadWriteUser/main.h.
    using NtUserSetSysColors_t = BOOL(__fastcall*)(unsigned int, char*, char*, int);
    using NtLoadDriver_t = NTSTATUS(NTAPI*)(PUNICODE_STRING);
    using NtQuerySystemInformation_t = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

    NtUserSetSysColors_t g_ntUserSetSysColors = nullptr;
    HMODULE g_win32u = nullptr;
    DWORD g_newColors[24] = { RGB(0x80, 0x00, 0x80) };

    constexpr NTSTATUS kStatusInvalidCid = static_cast<NTSTATUS>(0xC000000B);
    constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004);
    constexpr NTSTATUS kStatusConnectionActive = static_cast<NTSTATUS>(0xC0000100);
    constexpr NTSTATUS kStatusAlreadyCommitted = static_cast<NTSTATUS>(0xC0000021);
    constexpr NTSTATUS kStatusImageAlreadyLoaded = static_cast<NTSTATUS>(0xC000010E);

    void DebugLog(const char* format, ...)
    {
        char message[512] = {};
        va_list args;
        va_start(args, format);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
        va_end(args);
        OutputDebugStringA(message);
    }

    bool NtSucceeded(NTSTATUS status)
    {
        return status >= 0;
    }

    // The mapper can return these benign/informational statuses after the
    // driver is usable (for example, when it is already active or committed).
    bool IsBenignLoadStatus(NTSTATUS status)
    {
        return status == kStatusConnectionActive ||
               status == kStatusAlreadyCommitted ||
               status == kStatusImageAlreadyLoaded;
    }

    LSTATUS PrepareDriverRegEntry(const std::wstring& serviceName, const std::wstring& path)
    {
        std::wstring nativePath = L"\\??\\" + path;
        HKEY servicesKey = nullptr;
        HKEY serviceKey = nullptr;

        LSTATUS status = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services",
            0,
            KEY_CREATE_SUB_KEY | KEY_SET_VALUE,
            &servicesKey);
        if (status != ERROR_SUCCESS)
            return status;

        status = RegCreateKeyExW(
            servicesKey,
            serviceName.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &serviceKey,
            nullptr);
        if (status == ERROR_SUCCESS)
        {
            status = RegSetValueExW(
                serviceKey,
                L"ImagePath",
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(nativePath.c_str()),
                static_cast<DWORD>((nativePath.size() + 1) * sizeof(wchar_t)));
        }

        if (status == ERROR_SUCCESS)
        {
            const DWORD processId = GetCurrentProcessId();
            status = RegSetValueExW(
                serviceKey,
                L"pid",
                0,
                REG_DWORD,
                reinterpret_cast<const BYTE*>(&processId),
                sizeof(processId));
        }

        if (status == ERROR_SUCCESS)
        {
            const DWORD serviceType = 1;
            status = RegSetValueExW(
                serviceKey,
                L"Type",
                0,
                REG_DWORD,
                reinterpret_cast<const BYTE*>(&serviceType),
                sizeof(serviceType));
        }

        if (serviceKey)
            RegCloseKey(serviceKey);
        RegCloseKey(servicesKey);
        return status;
    }

    bool EnableLoadDriverPrivilege()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
        {
            DebugLog("ReadWriteDriver: OpenProcessToken failed (%lu)\n", GetLastError());
            return false;
        }

        LUID luid = {};
        TOKEN_PRIVILEGES privileges = {};
        const bool found = LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &luid) != FALSE;
        if (found)
        {
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Luid = luid;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr))
            {
                const DWORD error = GetLastError();
                CloseHandle(token);
                DebugLog("ReadWriteDriver: AdjustTokenPrivileges failed (%lu)\n", error);
                return false;
            }
        }

        const DWORD error = GetLastError();
        CloseHandle(token);
        if (!found || error == ERROR_NOT_ALL_ASSIGNED)
        {
            DebugLog("ReadWriteDriver: SeLoadDriverPrivilege is unavailable (%lu)\n", error);
            return false;
        }
        return true;
    }

    NTSTATUS LoadDriver(const std::wstring& serviceName, const std::wstring& path)
    {
        if (!path.empty())
        {
            const LSTATUS status = PrepareDriverRegEntry(serviceName, path);
            if (status != ERROR_SUCCESS)
            {
                DebugLog("ReadWriteDriver: registry setup failed (%ld)\n", status);
                return static_cast<NTSTATUS>(status);
            }
        }

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto ntLoadDriver = ntdll
            ? reinterpret_cast<NtLoadDriver_t>(GetProcAddress(ntdll, "NtLoadDriver"))
            : nullptr;
        if (!ntLoadDriver)
        {
            DebugLog("ReadWriteDriver: NtLoadDriver was not found\n");
            return kStatusInvalidCid;
        }

        const std::wstring registryPath =
            L"\\registry\\machine\\SYSTEM\\CurrentControlSet\\Services\\" + serviceName;
        UNICODE_STRING unicodePath = {};
        unicodePath.Buffer = const_cast<PWSTR>(registryPath.c_str());
        unicodePath.Length = static_cast<USHORT>(registryPath.size() * sizeof(wchar_t));
        unicodePath.MaximumLength = unicodePath.Length + sizeof(wchar_t);
        return ntLoadDriver(&unicodePath);
    }

    std::wstring DriverMapperPath()
    {
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        if (length == 0 || length >= ARRAYSIZE(modulePath))
            return {};

        std::wstring path(modulePath, length);
        const std::wstring::size_type separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return L"ReadWriteDriverMapper.sys";
        return path.substr(0, separator + 1) + L"ReadWriteDriverMapper.sys";
    }

    NTSTATUS InvokeCommand(Command& command, uint64_t authToken)
    {
        if (!g_ntUserSetSysColors)
            return kStatusInvalidCid;
        if (!command.user_result)
            return static_cast<NTSTATUS>(0xC000000D);

        command.magic = COMMAND_MAGIC;
        command.version = PROTOCOL_VERSION;
        command.size = static_cast<uint32_t>(sizeof(Command));
        command.auth_token = authToken;
        command.status = kStatusInvalidCid;

        const BOOL result = g_ntUserSetSysColors(
            static_cast<unsigned int>(sizeof(Command) / sizeof(DWORD)),
            reinterpret_cast<char*>(&command),
            reinterpret_cast<char*>(g_newColors),
            0);
        (void)result;
        return static_cast<NTSTATUS>(command.status);
    }

    // The first fields are stable for SystemProcessInformation and are all
    // that is needed to enumerate the process name and PID.
    struct SystemProcessInformationEntry
    {
        ULONG NextEntryOffset;
        ULONG NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG HardFaultCount;
        ULONG NumberOfThreadsHighWatermark;
        ULONGLONG CycleTime;
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UNICODE_STRING ImageName;
        LONG BasePriority;
        HANDLE UniqueProcessId;
    };

    struct RemoteListEntry
    {
        uintptr_t Flink;
        uintptr_t Blink;
    };

    struct RemotePeb
    {
        BYTE flags[4];
        HANDLE Mutant;
        PVOID ImageBaseAddress;
        PVOID Ldr;
    };

    struct RemotePebLdrData
    {
        ULONG Length;
        BOOLEAN Initialized;
        HANDLE SsHandle;
        RemoteListEntry InLoadOrderModuleList;
        RemoteListEntry InMemoryOrderModuleList;
        RemoteListEntry InInitializationOrderModuleList;
    };

    struct RemoteLdrDataTableEntry
    {
        RemoteListEntry InLoadOrderLinks;
        RemoteListEntry InMemoryOrderLinks;
        RemoteListEntry InInitializationOrderLinks;
        PVOID DllBase;
        PVOID EntryPoint;
        ULONG SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    };
}

bool DriverInterfaceV3::Initialize()
{
    if (driverLoaded_ && g_ntUserSetSysColors && sessionToken_ != 0)
        return true;

    driverLoaded_ = false;
    sessionToken_ = 0;

    // Keep the same user32/win32u initialization order as ReadWriteUser.
    LoadLibraryW(L"user32.dll");
    if (!g_win32u)
        g_win32u = LoadLibraryW(L"win32u.dll");
    if (!g_win32u)
    {
        DebugLog("ReadWriteDriver: LoadLibraryW(win32u.dll) failed (%lu)\n", GetLastError());
        return false;
    }

    g_ntUserSetSysColors = reinterpret_cast<NtUserSetSysColors_t>(
        GetProcAddress(g_win32u, "NtUserSetSysColors"));
    if (!g_ntUserSetSysColors)
    {
        DebugLog("ReadWriteDriver: NtUserSetSysColors was not found\n");
        return false;
    }

    if (!EnableLoadDriverPrivilege())
        return false;

    const std::wstring mapperPath = DriverMapperPath();
    const bool mapperExists = !mapperPath.empty() &&
        GetFileAttributesW(mapperPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    const NTSTATUS status = LoadDriver(L"ReadWriteDriver", mapperExists ? mapperPath : L"");
    if (!NtSucceeded(status) && !IsBenignLoadStatus(status))
    {
        DebugLog("ReadWriteDriver: NtLoadDriver failed (0x%08X)\n",
                 static_cast<unsigned int>(status));
        return false;
    }

    Command handshake = {};
    handshake.user_result = reinterpret_cast<uintptr_t>(&handshake);
    handshake.op = COMMAND_ISLOADED;
    const NTSTATUS handshakeStatus = InvokeCommand(handshake, 0);
    if (!NtSucceeded(handshakeStatus) || handshake.auth_token == 0)
    {
        DebugLog("ReadWriteDriver: protocol handshake failed (0x%08X)\n",
                 static_cast<unsigned int>(handshakeStatus));
        return false;
    }

    sessionToken_ = handshake.auth_token;
    driverLoaded_ = true;
    DebugLog("ReadWriteDriver: hook transport initialized\n");
    return true;
}

void DriverInterfaceV3::Cleanup()
{
    currentPid_ = 0;
    baseAddress_ = 0;
    driverLoaded_ = false;
    sessionToken_ = 0;
}

DWORD DriverInterfaceV3::GetProcessId(const wchar_t* processName) const
{
    if (!driverLoaded_ || sessionToken_ == 0 || !processName || !*processName)
        return 0;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto querySystemInformation = ntdll
        ? reinterpret_cast<NtQuerySystemInformation_t>(
            GetProcAddress(ntdll, "NtQuerySystemInformation"))
        : nullptr;
    if (!querySystemInformation)
        return 0;

    std::vector<BYTE> buffer(1024 * 1024);
    ULONG returnLength = 0;
    NTSTATUS status = querySystemInformation(
        5, buffer.data(), static_cast<ULONG>(buffer.size()), &returnLength);
    if (status == kStatusInfoLengthMismatch)
    {
        const size_t newSize = returnLength > buffer.size()
            ? returnLength
            : buffer.size() * 2;
        buffer.resize(newSize);
        status = querySystemInformation(
            5, buffer.data(), static_cast<ULONG>(buffer.size()), &returnLength);
    }
    if (!NtSucceeded(status))
    {
        DebugLog("ReadWriteDriver: NtQuerySystemInformation failed (0x%08X)\n",
                 static_cast<unsigned int>(status));
        return 0;
    }

    for (size_t offset = 0; offset < buffer.size();)
    {
        const auto* entry = reinterpret_cast<const SystemProcessInformationEntry*>(
            buffer.data() + offset);
        if (entry->ImageName.Buffer &&
            entry->ImageName.Length == wcslen(processName) * sizeof(wchar_t) &&
            _wcsnicmp(entry->ImageName.Buffer, processName,
                      entry->ImageName.Length / sizeof(wchar_t)) == 0)
        {
            return static_cast<DWORD>(reinterpret_cast<uintptr_t>(entry->UniqueProcessId));
        }

        if (entry->NextEntryOffset == 0 ||
            entry->NextEntryOffset > buffer.size() - offset)
            break;
        offset += entry->NextEntryOffset;
    }
    return 0;
}

void DriverInterfaceV3::SetCurrentPid(DWORD pid)
{
    currentPid_ = pid;
}

uintptr_t DriverInterfaceV3::GetModuleBase(DWORD pid, const wchar_t* moduleName) const
{
    if (!driverLoaded_ || sessionToken_ == 0 || !pid || !moduleName || !*moduleName)
        return 0;

    Command command = {};
    command.user_result = reinterpret_cast<uintptr_t>(&command);
    command.pid = pid;
    command.op = COMMAND_GETPROCPID;
    const NTSTATUS pebStatus = InvokeCommand(command, sessionToken_);
    if (!NtSucceeded(pebStatus) || !command.result)
        return 0;

    const uintptr_t pebAddress = static_cast<uintptr_t>(command.result);
    RemotePeb peb = {};
    if (!ReadMemory(pid, pebAddress, &peb, sizeof(peb)) || !peb.Ldr)
        return 0;

    RemotePebLdrData ldr = {};
    const uintptr_t ldrAddress = reinterpret_cast<uintptr_t>(peb.Ldr);
    if (!ReadMemory(pid, ldrAddress, &ldr, sizeof(ldr)))
        return 0;

    const uintptr_t listHead = ldrAddress + offsetof(RemotePebLdrData, InMemoryOrderModuleList);
    uintptr_t current = ldr.InMemoryOrderModuleList.Flink;
    for (size_t index = 0; current && current != listHead && index < 1024; ++index)
    {
        const uintptr_t entryAddress =
            current - offsetof(RemoteLdrDataTableEntry, InMemoryOrderLinks);
        RemoteLdrDataTableEntry entry = {};
        if (!ReadMemory(pid, entryAddress, &entry, sizeof(entry)))
            return 0;

        if (entry.BaseDllName.Buffer && entry.BaseDllName.Length > 0 &&
            entry.BaseDllName.Length <= 512)
        {
            const size_t characterCount = entry.BaseDllName.Length / sizeof(wchar_t);
            std::vector<wchar_t> name(characterCount + 1, L'\0');
            if (ReadMemory(pid,
                           reinterpret_cast<uintptr_t>(entry.BaseDllName.Buffer),
                           name.data(), entry.BaseDllName.Length) &&
                _wcsicmp(name.data(), moduleName) == 0)
            {
                return reinterpret_cast<uintptr_t>(entry.DllBase);
            }
        }

        RemoteListEntry links = {};
        if (!ReadMemory(pid, current, &links, sizeof(links)))
            return 0;
        current = links.Flink;
    }
    return 0;
}

void DriverInterfaceV3::SetBaseAddress(uintptr_t baseAddress)
{
    baseAddress_ = baseAddress;
}

uintptr_t DriverInterfaceV3::FindRealCr3(DWORD /*pid*/, uintptr_t /*baseAddress*/, uintptr_t /*dataAnchor*/) const
{
    // TODO: port the physical-memory CR3 search from ReadWriteDriver/physmem.c
    // once that functionality has a user-mode command in the shared protocol.
    return 0;
}

uintptr_t DriverInterfaceV3::GetProcessCr3(DWORD /*pid*/) const
{
    // ReadWriteDriver exposes no CR3 command; keep the existing fallback until
    // the physmem.c implementation is surfaced through Command.
    return 0;
}

bool DriverInterfaceV3::ReadMemory(DWORD pid, uintptr_t address, void* buffer,
                                   size_t size, const char* debugName) const
{
    if (!driverLoaded_ || sessionToken_ == 0 || !pid || !address || !buffer || size == 0)
        return false;

    Command command = {};
    command.user_result = reinterpret_cast<uintptr_t>(&command);
    command.pid = pid;
    command.src = static_cast<uint64_t>(address);
    command.dst = reinterpret_cast<uintptr_t>(buffer);
    command.op = COMMAND_READWRITE;
    command.len = size;

    const NTSTATUS status = InvokeCommand(command, sessionToken_);
    if (!NtSucceeded(status) && debugName)
        DebugLog("ReadWriteDriver: read failed for %s (0x%08X)\n",
                 debugName, static_cast<unsigned int>(status));
    return NtSucceeded(status);
}

bool DriverInterfaceV3::WriteMemory(DWORD pid, uintptr_t address, const void* buffer,
                                    size_t size, const char* debugName) const
{
    if (!driverLoaded_ || sessionToken_ == 0 || !pid || !address || !buffer || size == 0)
        return false;

    Command command = {};
    command.user_result = reinterpret_cast<uintptr_t>(&command);
    command.pid = pid;
    command.src = reinterpret_cast<uintptr_t>(const_cast<void*>(buffer));
    command.dst = static_cast<uint64_t>(address);
    command.op = COMMAND_READWRITE;
    command.flags = COMMAND_FLAG_WRITE;
    command.len = size;

    const NTSTATUS status = InvokeCommand(command, sessionToken_);
    if (!NtSucceeded(status) && debugName)
        DebugLog("ReadWriteDriver: write failed for %s (0x%08X)\n",
                 debugName, static_cast<unsigned int>(status));
    return NtSucceeded(status);
}

bool DriverInterfaceV3::BatchReadMemory(DWORD pid, BatchReadEntry* entries, size_t count,
                                         size_t* successfulCount) const
{
    if (!driverLoaded_ || sessionToken_ == 0 || (!entries && count != 0))
        return false;

    size_t successes = 0;

    for (size_t index = 0; index < count; ++index)
    {
        entries[index].success = ReadMemory(pid, entries[index].address,
                                             entries[index].buffer, entries[index].size);
        if (entries[index].success)
            ++successes;
    }

    if (successfulCount)
        *successfulCount = successes;

    // Preserve the existing bool contract: true means every slot succeeded.
    // Individual failures are recorded above and never abort the remaining
    // reads in the batch.
    return successes == count;
}

void DriverInterfaceV3::InjectMouseMove(int /*moveX*/, int /*moveY*/) const
{
    // The ReadWriteDriver protocol only provides memory operations.
}

void DriverInterfaceV3::TestRandomMouseMoveLoop(std::atomic<bool>& running) const
{
    running.store(false, std::memory_order_relaxed);
}
