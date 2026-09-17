#include "main.h"
#include "Halo.h"
#include "Apex.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr NTSTATUS kStatusUnsuccessful = static_cast<NTSTATUS>(0xC0000001);
    constexpr NTSTATUS kStatusPartialCopy = static_cast<NTSTATUS>(0x8000000D);

    HANDLE g_device = INVALID_HANDLE_VALUE;
    uint32_t g_max_transfer = 0;
    uint64_t g_next_request_id = 1;

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
        if (!required)
            return {};

        std::string result(static_cast<size_t>(required), '\0');
        if (!WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                 static_cast<int>(value.size()),
                                 &result[0], required, nullptr, nullptr))
            return {};
        return result;
    }

    bool GetExecutablePath(std::wstring& path)
    {
        std::vector<wchar_t> buffer(260);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!length)
                return false;

            if (length < buffer.size() - 1 || buffer.size() >= 32768)
            {
                path.assign(buffer.data(), length);
                return true;
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::wstring CanonicalExecutablePath(const std::wstring& rawPath)
    {
        std::wstring path = rawPath;
        if (path.rfind(L"\\\\?\\", 0) == 0 ||
            path.rfind(L"\\??\\", 0) == 0)
        {
            path.erase(0, 4);
        }

        CharUpperBuffW(&path[0], static_cast<DWORD>(path.size()));
        return L"\\??\\" + path;
    }

    void PrintExecutablePathDiagnostics()
    {
        std::wstring rawPath;
        if (!GetExecutablePath(rawPath))
        {
            std::printf("Could not determine executable image path for driver "
                        "allowlist comparison (Win32 error %lu)\n",
                        GetLastError());
            return;
        }

        const std::string rawUtf8 = WideToUtf8(rawPath);
        const std::string canonicalUtf8 =
            WideToUtf8(CanonicalExecutablePath(rawPath));
        std::printf("Executable image path for driver allowlist comparison "
                    "(RAW): %s\n",
                    rawUtf8.c_str());
        std::printf("Executable image path for driver allowlist comparison "
                    "(CANONICAL): %s\n",
                    canonicalUtf8.c_str());
    }

    void DebugLog(const char* message)
    {
        OutputDebugStringA(message);
    }

    bool NtSucceeded(NTSTATUS status)
    {
        return status >= 0;
    }

    uint64_t NextRequestId()
    {
        return g_next_request_id++;
    }

    void InitializeRequestHeader(PUBGEXT_REQUEST_HEADER& header,
                                 uint32_t structSize)
    {
        header = {};
        header.magic = PUBGEXT_IOCTL_MAGIC;
        header.major = PUBGEXT_PROTOCOL_MAJOR;
        header.minor = PUBGEXT_PROTOCOL_MINOR;
        header.struct_size = structSize;
        header.request_id = NextRequestId();
    }

    bool ValidateResponse(const PUBGEXT_RESPONSE_HEADER& header,
                          uint32_t expectedSize, uint64_t requestId,
                          const char* operation)
    {
        if (header.magic != PUBGEXT_IOCTL_MAGIC ||
            header.struct_size != expectedSize ||
            header.flags != 0 || header.reserved != 0 ||
            header.request_id != requestId)
        {
            std::printf("%s returned an invalid response header\n", operation);
            return false;
        }
        if (header.major != PUBGEXT_PROTOCOL_MAJOR ||
            header.minor != PUBGEXT_PROTOCOL_MINOR)
        {
            std::printf("%s returned STATUS_REVISION_MISMATCH\n", operation);
            return false;
        }
        return true;
    }

    size_t TransferChunkSize()
    {
        return static_cast<size_t>(std::min<uint32_t>(
            PUBGEXT_COPY_CHUNK,
            std::min<uint32_t>(g_max_transfer, PUBGEXT_MAX_TRANSFER)));
    }

    NTSTATUS IoctlFailure(const char* operation)
    {
        const DWORD error = GetLastError();
        char message[256] = {};
        sprintf_s(message, "%s DeviceIoControl failed (Win32 error %lu)\n",
                  operation, error);
        DebugLog(message);
        return kStatusUnsuccessful;
    }
}

bool InitializeDevice()
{
    if (g_device != INVALID_HANDLE_VALUE)
        return true;

    g_device = CreateFileW(L"\\\\.\\PubgExtRw",
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (g_device == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            std::printf("Could not open \\\\.\\PubgExtRw: Win32 error %lu - "
                        "device does not exist (driver not loaded).\n",
                        error);
        }
        else if (error == ERROR_ACCESS_DENIED)
        {
            std::printf("Could not open \\\\.\\PubgExtRw: Win32 error %lu - "
                        "device exists, access denied (driver allowlist "
                        "mismatch).\n",
                        error);
        }
        else
        {
            std::printf("Could not open \\\\.\\PubgExtRw: Win32 error %lu.\n",
                        error);
        }
        return false;
    }

    PUBGEXT_REQUEST_HEADER queryRequest = {};
    InitializeRequestHeader(queryRequest, sizeof(queryRequest));
    PUBGEXT_QUERY_CAPS_RESPONSE queryResponse = {};
    DWORD returned = 0;
    if (!DeviceIoControl(g_device, PUBGEXT_IOCTL_QUERY_CAPS,
                         &queryRequest, sizeof(queryRequest),
                         &queryResponse, sizeof(queryResponse),
                         &returned, nullptr) ||
        returned < sizeof(queryResponse) ||
        !ValidateResponse(queryResponse.header, sizeof(queryResponse),
                          queryRequest.request_id, "QUERY_CAPS") ||
        queryResponse.header.operation_status != static_cast<NTSTATUS>(0))
    {
        std::printf("QUERY_CAPS failed (Win32 error %lu)\n", GetLastError());
        CleanupDevice();
        return false;
    }

    constexpr uint32_t requiredCaps = PUBGEXT_CAP_AUTH | PUBGEXT_CAP_QUERY |
                                       PUBGEXT_CAP_READ | PUBGEXT_CAP_WRITE |
                                       PUBGEXT_CAP_VIRTUAL;
    if ((queryResponse.caps & requiredCaps) != requiredCaps ||
        queryResponse.max_transfer == 0)
    {
        std::printf("QUERY_CAPS reported unsupported capabilities\n");
        CleanupDevice();
        return false;
    }
    g_max_transfer = std::min<uint32_t>(queryResponse.max_transfer,
                                        PUBGEXT_MAX_TRANSFER);

    // AUTH is exactly the driver's 32-byte header request and 40-byte response.
    PUBGEXT_REQUEST_HEADER authRequest = {};
    InitializeRequestHeader(authRequest, sizeof(authRequest));
    PUBGEXT_RESPONSE_HEADER authResponse = {};
    returned = 0;
    if (!DeviceIoControl(g_device, PUBGEXT_IOCTL_AUTH,
                         &authRequest, sizeof(authRequest),
                         &authResponse, sizeof(authResponse),
                         &returned, nullptr) ||
        returned < sizeof(authResponse) ||
        !ValidateResponse(authResponse, sizeof(authResponse),
                          authRequest.request_id, "AUTH") ||
        authResponse.operation_status != static_cast<NTSTATUS>(0))
    {
        std::printf("AUTH failed (Win32 error %lu)\n", GetLastError());
        CleanupDevice();
        return false;
    }

    std::printf("Connected: protocol %u.%u, max transfer %u bytes\n",
                PUBGEXT_PROTOCOL_MAJOR, PUBGEXT_PROTOCOL_MINOR, g_max_transfer);
    return true;
}

void CleanupDevice()
{
    if (g_device != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
    }
    g_max_transfer = 0;
}

NTSTATUS KeReadVirtualMemory(uintptr_t pid, unsigned char* source,
                             uintptr_t destination, SIZE_T size,
                             SIZE_T* transferredOut)
{
    if (transferredOut)
        *transferredOut = 0;
    if (g_device == INVALID_HANDLE_VALUE || !pid || !source || !destination || !size)
        return kStatusUnsuccessful;

    const size_t chunkLimit = TransferChunkSize();
    if (!chunkLimit)
        return kStatusUnsuccessful;

    size_t total = 0;
    while (total < size)
    {
        const size_t chunk = std::min(chunkLimit, size - total);
        const uintptr_t remote = reinterpret_cast<uintptr_t>(source);
        if (total > std::numeric_limits<uintptr_t>::max() - remote)
            break;

        PUBGEXT_READ_REQUEST request = {};
        InitializeRequestHeader(request.header, sizeof(request));
        request.pid = static_cast<uint32_t>(pid);
        request.remote_va = static_cast<uint64_t>(remote + total);
        request.length = static_cast<uint32_t>(chunk);

        std::vector<unsigned char> output(sizeof(PUBGEXT_RESPONSE_HEADER) + chunk);
        DWORD returned = 0;
        if (!DeviceIoControl(g_device, PUBGEXT_IOCTL_READ,
                             &request, sizeof(request),
                             output.data(), static_cast<DWORD>(output.size()),
                             &returned, nullptr))
            return IoctlFailure("READ");
        if (returned < sizeof(PUBGEXT_RESPONSE_HEADER))
            return kStatusUnsuccessful;

        const auto* response = reinterpret_cast<const PUBGEXT_RESPONSE_HEADER*>(output.data());
        if (!ValidateResponse(*response, sizeof(PUBGEXT_RESPONSE_HEADER),
                              request.header.request_id, "READ") ||
            response->transferred > chunk ||
            response->transferred > returned - sizeof(PUBGEXT_RESPONSE_HEADER))
            return kStatusUnsuccessful;

        const size_t done = static_cast<size_t>(response->transferred);
        if (done)
        {
            std::memcpy(reinterpret_cast<unsigned char*>(destination) + total,
                        output.data() + sizeof(PUBGEXT_RESPONSE_HEADER), done);
            total += done;
        }

        const NTSTATUS operationStatus = static_cast<NTSTATUS>(response->operation_status);
        if (!NtSucceeded(operationStatus))
        {
            if (operationStatus == kStatusPartialCopy)
                std::printf("READ returned STATUS_PARTIAL_COPY (%zu/%zu bytes)\n", done, chunk);
            break;
        }
        if (done != chunk)
            break;
    }

    if (transferredOut)
        *transferredOut = total;
    return total == size ? static_cast<NTSTATUS>(0) : kStatusPartialCopy;
}

NTSTATUS KeWriteVirtualMemory(uintptr_t pid, unsigned char* source,
                              uintptr_t destination, SIZE_T size,
                              SIZE_T* transferredOut)
{
    if (transferredOut)
        *transferredOut = 0;
    if (g_device == INVALID_HANDLE_VALUE || !pid || !source || !destination || !size)
        return kStatusUnsuccessful;

    const size_t chunkLimit = TransferChunkSize();
    if (!chunkLimit)
        return kStatusUnsuccessful;

    size_t total = 0;
    while (total < size)
    {
        const size_t chunk = std::min(chunkLimit, size - total);
        const uintptr_t remote = destination;
        if (total > std::numeric_limits<uintptr_t>::max() - remote)
            break;

        std::vector<unsigned char> input(PUBGEXT_WRITE_FIXED_SIZE + chunk);
        auto* request = reinterpret_cast<PUBGEXT_WRITE_REQUEST*>(input.data());
        InitializeRequestHeader(request->header, PUBGEXT_WRITE_FIXED_SIZE);
        request->pid = static_cast<uint32_t>(pid);
        request->remote_va = static_cast<uint64_t>(remote + total);
        request->length = static_cast<uint32_t>(chunk);
        std::memcpy(request->data, source + total, chunk);

        PUBGEXT_RESPONSE_HEADER response = {};
        DWORD returned = 0;
        if (!DeviceIoControl(g_device, PUBGEXT_IOCTL_WRITE,
                             input.data(), static_cast<DWORD>(input.size()),
                             &response, sizeof(response),
                             &returned, nullptr))
            return IoctlFailure("WRITE");
        if (returned < sizeof(response) ||
            !ValidateResponse(response, sizeof(response),
                              request->header.request_id, "WRITE") ||
            response.transferred > chunk)
            return kStatusUnsuccessful;

        total += static_cast<size_t>(response.transferred);
        const NTSTATUS operationStatus = static_cast<NTSTATUS>(response.operation_status);
        if (!NtSucceeded(operationStatus) || response.transferred != chunk)
        {
            if (operationStatus == kStatusPartialCopy)
                std::printf("WRITE returned STATUS_PARTIAL_COPY (%llu/%zu bytes)\n",
                            static_cast<unsigned long long>(response.transferred), chunk);
            break;
        }
    }

    if (transferredOut)
        *transferredOut = total;
    return total == size ? static_cast<NTSTATUS>(0) : kStatusPartialCopy;
}

uintptr_t GetPIDByName(const wchar_t* name)
{
    if (!name || !*name)
        return 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    uintptr_t pid = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, name) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

uintptr_t GetModuleBase(uintptr_t input_pid, const wchar_t* name)
{
    if (!input_pid || !name || !*name)
        return 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, static_cast<DWORD>(input_pid));
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, name) == 0)
            {
                base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return base;
}

bool RunDriverSmokeTest()
{
    if (!InitializeDevice())
        return false;

    uint64_t value = 0x1122334455667788ull;
    uint64_t observed = 0;
    const uintptr_t pid = GetCurrentProcessId();
    SIZE_T transferred = 0;
    const NTSTATUS readStatus = KeReadVirtualMemory(
        pid, reinterpret_cast<unsigned char*>(&value),
        reinterpret_cast<uintptr_t>(&observed), sizeof(observed), &transferred);
    if (readStatus != static_cast<NTSTATUS>(0) || observed != value)
    {
        std::printf("READ smoke test failed: status 0x%08X, bytes: %zu\n",
                    static_cast<unsigned int>(readStatus), transferred);
        return false;
    }

    const uint64_t replacement = 0x8877665544332211ull;
    const NTSTATUS writeStatus = KeWriteVirtualMemory(
        pid, reinterpret_cast<unsigned char*>(const_cast<uint64_t*>(&replacement)),
        reinterpret_cast<uintptr_t>(&value), sizeof(replacement), &transferred);
    if (writeStatus != static_cast<NTSTATUS>(0) || value != replacement)
    {
        std::printf("WRITE smoke test failed: status 0x%08X, bytes: %zu\n",
                    static_cast<unsigned int>(writeStatus), transferred);
        return false;
    }

    std::printf("QUERY_CAPS -> AUTH -> READ -> WRITE smoke test passed\n");
    return true;
}

int main()
{
    char input = 0;
    PrintExecutablePathDiagnostics();
    std::printf("1) Connect and run IOCTL smoke test\n");
    std::printf("2) Find notepad.exe by name\n");
    std::printf("3) Halo MCC\n");
    std::printf("4) Apex Legends\n");

    while (true)
    {
        if (scanf_s(" %c", &input, 1) != 1)
            break;

        if (input == '1')
        {
            RunDriverSmokeTest();
        }
        else if (input == '2')
        {
            const uintptr_t pid = GetPIDByName(L"notepad.exe");
            std::printf("notepad.exe PID: %llu\n",
                        static_cast<unsigned long long>(pid));
        }
        else if (input == '3')
        {
            if (InitializeDevice())
                Halo();
        }
        else if (input == '4')
        {
            if (InitializeDevice())
                Apex();
        }
    }

    CleanupDevice();
    return 0;
}
