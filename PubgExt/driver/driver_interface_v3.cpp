#include "driver_interface_v3.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <vector>

namespace
{
    constexpr NTSTATUS kStatusPartialCopy = static_cast<NTSTATUS>(0x8000000D);

    void DebugLog(const char* format, ...)
    {
        char message[512] = {};
        va_list args;
        va_start(args, format);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
        va_end(args);
        OutputDebugStringA(message);
    }

    uint64_t NextRequestId(std::atomic<uint64_t>& nextRequestId)
    {
        return nextRequestId.fetch_add(1, std::memory_order_relaxed);
    }

    void InitializeRequestHeader(PUBGEXT_REQUEST_HEADER& header,
                                 uint32_t structSize, uint64_t requestId)
    {
        header = {};
        header.magic = PUBGEXT_IOCTL_MAGIC;
        header.major = PUBGEXT_PROTOCOL_MAJOR;
        header.minor = PUBGEXT_PROTOCOL_MINOR;
        header.struct_size = structSize;
        header.request_id = requestId;
    }

    bool ValidateResponseHeader(const PUBGEXT_RESPONSE_HEADER& header,
                                uint32_t expectedSize, uint64_t requestId,
                                const char* operation)
    {
        if (header.magic != PUBGEXT_IOCTL_MAGIC ||
            header.struct_size != expectedSize ||
            header.request_id != requestId)
        {
            DebugLog("ReadWriteDriver: %s returned an invalid response header\n", operation);
            return false;
        }
        if (header.major != PUBGEXT_PROTOCOL_MAJOR ||
            header.minor != PUBGEXT_PROTOCOL_MINOR)
        {
            DebugLog("ReadWriteDriver: %s returned STATUS_REVISION_MISMATCH\n", operation);
            return false;
        }
        return true;
    }

    size_t TransferChunkSize(uint32_t maxTransfer)
    {
        return static_cast<size_t>(std::min<uint32_t>(
            PUBGEXT_COPY_CHUNK, std::min<uint32_t>(maxTransfer, PUBGEXT_MAX_TRANSFER)));
    }
}

bool DriverInterfaceV3::Initialize()
{
    if (driverLoaded_ && device_ != INVALID_HANDLE_VALUE)
        return true;

    Cleanup();

    // The device ACL grants access to SYSTEM and built-in Administrators.
    // Run the application elevated or CreateFile will fail with access denied.
    device_ = CreateFileW(L"\\\\.\\PubgExtRw",
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    if (device_ == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        DebugLog("ReadWriteDriver: CreateFile(\\\\.\\PubgExtRw) failed (%lu); "
                 "driver is not loaded or the app is not elevated\n", error);
        return false;
    }

    PUBGEXT_REQUEST_HEADER queryRequest = {};
    InitializeRequestHeader(queryRequest, sizeof(queryRequest),
                            NextRequestId(nextRequestId_));
    PUBGEXT_QUERY_CAPS_RESPONSE queryResponse = {};
    DWORD returned = 0;
    if (!DeviceIoControl(device_, PUBGEXT_IOCTL_QUERY_CAPS,
                         &queryRequest, sizeof(queryRequest),
                         &queryResponse, sizeof(queryResponse),
                         &returned, nullptr) ||
        returned < sizeof(queryResponse) ||
        !ValidateResponseHeader(queryResponse.header, sizeof(queryResponse),
                                queryRequest.request_id, "QUERY_CAPS") ||
        queryResponse.header.operation_status != static_cast<NTSTATUS>(0))
    {
        DebugLog("ReadWriteDriver: QUERY_CAPS failed (Win32 error %lu)\n",
                 GetLastError());
        Cleanup();
        return false;
    }

    constexpr uint32_t requiredCaps = PUBGEXT_CAP_AUTH | PUBGEXT_CAP_QUERY |
                                       PUBGEXT_CAP_READ | PUBGEXT_CAP_WRITE |
                                       PUBGEXT_CAP_VIRTUAL;
    if ((queryResponse.caps & requiredCaps) != requiredCaps ||
        queryResponse.max_transfer == 0 || TransferChunkSize(queryResponse.max_transfer) == 0)
    {
        DebugLog("ReadWriteDriver: QUERY_CAPS reported unsupported capabilities\n");
        Cleanup();
        return false;
    }
    maxTransfer_ = std::min<uint32_t>(queryResponse.max_transfer, PUBGEXT_MAX_TRANSFER);

    // AUTH is a header-only request. The driver validates the exact 32-byte
    // request and returns its 40-byte response in the output buffer.
    PUBGEXT_REQUEST_HEADER authRequest = {};
    InitializeRequestHeader(authRequest, sizeof(authRequest),
                            NextRequestId(nextRequestId_));
    PUBGEXT_RESPONSE_HEADER authResponse = {};
    returned = 0;
    if (!DeviceIoControl(device_, PUBGEXT_IOCTL_AUTH,
                         &authRequest, sizeof(authRequest),
                         &authResponse, sizeof(authResponse),
                         &returned, nullptr) ||
        returned < sizeof(authResponse) ||
        !ValidateResponseHeader(authResponse, sizeof(authResponse),
                                authRequest.request_id, "AUTH") ||
        authResponse.operation_status != static_cast<NTSTATUS>(0))
    {
        DebugLog("ReadWriteDriver: AUTH failed (Win32 error %lu)\n", GetLastError());
        Cleanup();
        return false;
    }

    driverLoaded_ = true;
    DebugLog("ReadWriteDriver: device/IOCTL transport initialized\n");
    return true;
}

void DriverInterfaceV3::Cleanup()
{
    if (device_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
    currentPid_ = 0;
    baseAddress_ = 0;
    maxTransfer_ = 0;
    driverLoaded_ = false;
}

DWORD DriverInterfaceV3::GetProcessId(const wchar_t* processName) const
{
    if (!processName || !*processName)
        return 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        DebugLog("ReadWriteDriver: CreateToolhelp32Snapshot(processes) failed (%lu)\n",
                 GetLastError());
        return 0;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, processName) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

void DriverInterfaceV3::SetCurrentPid(DWORD pid)
{
    currentPid_ = pid;
}

uintptr_t DriverInterfaceV3::GetModuleBase(DWORD pid, const wchar_t* moduleName) const
{
    if (!pid || !moduleName || !*moduleName)
        return 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        DebugLog("ReadWriteDriver: CreateToolhelp32Snapshot(modules) failed (%lu)\n",
                 GetLastError());
        return 0;
    }

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    uintptr_t moduleBase = 0;
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, moduleName) == 0)
            {
                moduleBase = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return moduleBase;
}

void DriverInterfaceV3::SetBaseAddress(uintptr_t baseAddress)
{
    baseAddress_ = baseAddress;
}

bool DriverInterfaceV3::ReadMemory(DWORD pid, uintptr_t address, void* buffer,
                                   size_t size, const char* debugName,
                                   size_t* bytesTransferred) const
{
    if (bytesTransferred)
        *bytesTransferred = 0;
    if (!driverLoaded_ || device_ == INVALID_HANDLE_VALUE || !pid || !address ||
        !buffer || size == 0 || maxTransfer_ == 0)
        return false;

    const size_t chunkLimit = TransferChunkSize(maxTransfer_);
    if (chunkLimit == 0)
        return false;

    size_t totalTransferred = 0;
    while (totalTransferred < size)
    {
        const size_t chunk = std::min(chunkLimit, size - totalTransferred);
        if (totalTransferred > std::numeric_limits<uintptr_t>::max() - address)
            break;

        PUBGEXT_READ_REQUEST request = {};
        InitializeRequestHeader(request.header, sizeof(request),
                                NextRequestId(nextRequestId_));
        request.pid = pid;
        request.remote_va = static_cast<uint64_t>(address + totalTransferred);
        request.length = static_cast<uint32_t>(chunk);

        std::vector<BYTE> output(sizeof(PUBGEXT_RESPONSE_HEADER) + chunk);
        DWORD returned = 0;
        if (!DeviceIoControl(device_, PUBGEXT_IOCTL_READ,
                             &request, sizeof(request),
                             output.data(), static_cast<DWORD>(output.size()),
                             &returned, nullptr))
        {
            if (debugName)
                DebugLog("ReadWriteDriver: read failed for %s (Win32 error %lu)\n",
                         debugName, GetLastError());
            break;
        }
        if (returned < sizeof(PUBGEXT_RESPONSE_HEADER))
        {
            DebugLog("ReadWriteDriver: READ returned a truncated response\n");
            break;
        }

        const auto* response = reinterpret_cast<const PUBGEXT_RESPONSE_HEADER*>(output.data());
        if (!ValidateResponseHeader(*response, sizeof(PUBGEXT_RESPONSE_HEADER),
                                    request.header.request_id, "READ"))
            break;
        if (response->transferred > chunk ||
            response->transferred > returned - sizeof(PUBGEXT_RESPONSE_HEADER))
        {
            DebugLog("ReadWriteDriver: READ returned an invalid transfer count\n");
            break;
        }

        const size_t transferred = static_cast<size_t>(response->transferred);
        if (transferred != 0)
        {
            std::memcpy(static_cast<BYTE*>(buffer) + totalTransferred,
                        output.data() + sizeof(PUBGEXT_RESPONSE_HEADER), transferred);
            totalTransferred += transferred;
        }

        const NTSTATUS operationStatus = static_cast<NTSTATUS>(response->operation_status);
        if (operationStatus != static_cast<NTSTATUS>(0))
        {
            if (operationStatus == kStatusPartialCopy)
                DebugLog("ReadWriteDriver: READ returned STATUS_PARTIAL_COPY (%zu/%zu bytes)\n",
                         transferred, chunk);
            else
                DebugLog("ReadWriteDriver: READ operation failed (0x%08X)\n",
                         static_cast<unsigned int>(operationStatus));
            break;
        }
        if (transferred != chunk)
        {
            DebugLog("ReadWriteDriver: READ returned fewer bytes with STATUS_SUCCESS\n");
            break;
        }
    }

    if (bytesTransferred)
        *bytesTransferred = totalTransferred;
    return totalTransferred == size;
}

bool DriverInterfaceV3::WriteMemory(DWORD pid, uintptr_t address, const void* buffer,
                                    size_t size, const char* debugName,
                                    size_t* bytesTransferred) const
{
    if (bytesTransferred)
        *bytesTransferred = 0;
    if (!driverLoaded_ || device_ == INVALID_HANDLE_VALUE || !pid || !address ||
        !buffer || size == 0 || maxTransfer_ == 0)
        return false;

    const size_t chunkLimit = TransferChunkSize(maxTransfer_);
    if (chunkLimit == 0)
        return false;

    size_t totalTransferred = 0;
    while (totalTransferred < size)
    {
        const size_t chunk = std::min(chunkLimit, size - totalTransferred);
        if (totalTransferred > std::numeric_limits<uintptr_t>::max() - address)
            break;

        std::vector<BYTE> input(PUBGEXT_WRITE_FIXED_SIZE + chunk);
        auto* request = reinterpret_cast<PUBGEXT_WRITE_REQUEST*>(input.data());
        InitializeRequestHeader(request->header, PUBGEXT_WRITE_FIXED_SIZE,
                                NextRequestId(nextRequestId_));
        request->pid = pid;
        request->remote_va = static_cast<uint64_t>(address + totalTransferred);
        request->length = static_cast<uint32_t>(chunk);
        std::memcpy(request->data,
                    static_cast<const BYTE*>(buffer) + totalTransferred, chunk);

        PUBGEXT_RESPONSE_HEADER response = {};
        DWORD returned = 0;
        if (!DeviceIoControl(device_, PUBGEXT_IOCTL_WRITE,
                             input.data(), static_cast<DWORD>(input.size()),
                             &response, sizeof(response),
                             &returned, nullptr))
        {
            if (debugName)
                DebugLog("ReadWriteDriver: write failed for %s (Win32 error %lu)\n",
                         debugName, GetLastError());
            break;
        }
        if (returned < sizeof(response) ||
            !ValidateResponseHeader(response, sizeof(response),
                                    request->header.request_id, "WRITE"))
            break;
        if (response.transferred > chunk)
        {
            DebugLog("ReadWriteDriver: WRITE returned an invalid transfer count\n");
            break;
        }

        totalTransferred += static_cast<size_t>(response.transferred);
        const NTSTATUS operationStatus = static_cast<NTSTATUS>(response.operation_status);
        if (operationStatus != static_cast<NTSTATUS>(0))
        {
            if (operationStatus == kStatusPartialCopy)
                DebugLog("ReadWriteDriver: WRITE returned STATUS_PARTIAL_COPY (%llu/%zu bytes)\n",
                         static_cast<unsigned long long>(response.transferred), chunk);
            else
                DebugLog("ReadWriteDriver: WRITE operation failed (0x%08X)\n",
                         static_cast<unsigned int>(operationStatus));
            break;
        }
        if (response.transferred != chunk)
        {
            DebugLog("ReadWriteDriver: WRITE returned fewer bytes with STATUS_SUCCESS\n");
            break;
        }
    }

    if (bytesTransferred)
        *bytesTransferred = totalTransferred;
    return totalTransferred == size;
}

bool DriverInterfaceV3::BatchReadMemory(DWORD pid, BatchReadEntry* entries, size_t count,
                                         size_t* successfulCount) const
{
    if (successfulCount)
        *successfulCount = 0;
    if (!driverLoaded_ || device_ == INVALID_HANDLE_VALUE || (!entries && count != 0))
        return false;

    size_t successes = 0;
    for (size_t index = 0; index < count; ++index)
    {
        entries[index].success = false;
        entries[index].transferred = 0;
        entries[index].success = ReadMemory(pid, entries[index].address,
                                             entries[index].buffer, entries[index].size,
                                             nullptr, &entries[index].transferred);
        if (entries[index].success)
            ++successes;
    }

    if (successfulCount)
        *successfulCount = successes;
    return successes == count;
}

void DriverInterfaceV3::InjectMouseMove(int /*moveX*/, int /*moveY*/) const
{
    // The device protocol only provides memory operations.
}

void DriverInterfaceV3::TestRandomMouseMoveLoop(std::atomic<bool>& running) const
{
    running.store(false, std::memory_order_relaxed);
}
