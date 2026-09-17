#ifndef PUBGEXT_IOCTL_PROTOCOL_H
#define PUBGEXT_IOCTL_PROTOCOL_H

#include <stdint.h>

#define PUBGEXT_IOCTL_DEVICE_TYPE 0x8337u
#define PUBGEXT_IOCTL_METHOD_BUFFERED 0u
#define PUBGEXT_IOCTL_FILE_ANY_ACCESS 0u
#define PUBGEXT_IOCTL_FILE_READ_ACCESS 0x0001u
#define PUBGEXT_IOCTL_FILE_WRITE_ACCESS 0x0002u

#define PUBGEXT_IOCTL_CTL_CODE(device, function, method, access) \
    (((uint32_t)(device) << 16) | ((uint32_t)(access) << 14) | \
     ((uint32_t)(function) << 2) | (uint32_t)(method))

#define PUBGEXT_IOCTL_AUTH PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x800u, PUBGEXT_IOCTL_METHOD_BUFFERED, (PUBGEXT_IOCTL_FILE_READ_ACCESS | PUBGEXT_IOCTL_FILE_WRITE_ACCESS))
#define PUBGEXT_IOCTL_QUERY_CAPS PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x801u, PUBGEXT_IOCTL_METHOD_BUFFERED, (PUBGEXT_IOCTL_FILE_READ_ACCESS | PUBGEXT_IOCTL_FILE_WRITE_ACCESS))
#define PUBGEXT_IOCTL_READ PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x802u, PUBGEXT_IOCTL_METHOD_BUFFERED, (PUBGEXT_IOCTL_FILE_READ_ACCESS | PUBGEXT_IOCTL_FILE_WRITE_ACCESS))
#define PUBGEXT_IOCTL_WRITE PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x803u, PUBGEXT_IOCTL_METHOD_BUFFERED, (PUBGEXT_IOCTL_FILE_READ_ACCESS | PUBGEXT_IOCTL_FILE_WRITE_ACCESS))

#define PUBGEXT_IOCTL_MAGIC 0x50554247u
#define PUBGEXT_PROTOCOL_MAJOR 2u
#define PUBGEXT_PROTOCOL_MINOR 0u
#define PUBGEXT_MAX_TRANSFER (16u * 1024u * 1024u)
#define PUBGEXT_COPY_CHUNK (64u * 1024u)

#define PUBGEXT_CAP_AUTH       0x00000001u
#define PUBGEXT_CAP_QUERY     0x00000002u
#define PUBGEXT_CAP_READ      0x00000004u
#define PUBGEXT_CAP_WRITE     0x00000008u
#define PUBGEXT_CAP_VIRTUAL   0x00000010u

typedef struct _PUBGEXT_REQUEST_HEADER {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t struct_size;
    uint32_t flags;
    uint64_t request_id;
    uint64_t reserved;
} PUBGEXT_REQUEST_HEADER;

typedef struct _PUBGEXT_RESPONSE_HEADER {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t struct_size;
    uint32_t flags;
    uint64_t request_id;
    int32_t operation_status;
    uint32_t reserved;
    uint64_t transferred;
} PUBGEXT_RESPONSE_HEADER;

typedef struct _PUBGEXT_READ_REQUEST {
    PUBGEXT_REQUEST_HEADER header;
    uint32_t pid;
    uint32_t reserved0;
    uint64_t remote_va;
    uint32_t length;
    uint32_t reserved1;
} PUBGEXT_READ_REQUEST;

typedef struct _PUBGEXT_WRITE_REQUEST {
    PUBGEXT_REQUEST_HEADER header;
    uint32_t pid;
    uint32_t reserved0;
    uint64_t remote_va;
    uint32_t length;
    uint32_t reserved1;
    uint8_t data[1];
} PUBGEXT_WRITE_REQUEST;

typedef struct _PUBGEXT_QUERY_CAPS_RESPONSE {
    PUBGEXT_RESPONSE_HEADER header;
    uint32_t caps;
    uint32_t max_transfer;
} PUBGEXT_QUERY_CAPS_RESPONSE;

#define PUBGEXT_REQUEST_FIXED_SIZE ((uint32_t)sizeof(PUBGEXT_REQUEST_HEADER))
#define PUBGEXT_RESPONSE_FIXED_SIZE ((uint32_t)sizeof(PUBGEXT_RESPONSE_HEADER))
#define PUBGEXT_READ_FIXED_SIZE ((uint32_t)sizeof(PUBGEXT_READ_REQUEST))
#define PUBGEXT_OFFSET_OF(type, member) ((uint32_t)(uintptr_t)&(((type*)0)->member))
#define PUBGEXT_WRITE_FIXED_SIZE PUBGEXT_OFFSET_OF(PUBGEXT_WRITE_REQUEST, data)
#define __builtin_offsetof(type, member) PUBGEXT_OFFSET_OF(type, member)

#if defined(__cplusplus)
#define PUBGEXT_STATIC_ASSERT(e, m) static_assert((e), m)
#else
#define PUBGEXT_ASSERT_JOIN_(a, b) a##b
#define PUBGEXT_ASSERT_JOIN(a, b) PUBGEXT_ASSERT_JOIN_(a, b)
#define PUBGEXT_STATIC_ASSERT(e, m) typedef char PUBGEXT_ASSERT_JOIN(pubgext_assert_, __LINE__)[(e) ? 1 : -1]
#endif

PUBGEXT_STATIC_ASSERT(sizeof(PUBGEXT_REQUEST_HEADER) == 32, "request header size");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, magic) == 0, "request magic offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, major) == 4, "request major offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, minor) == 6, "request minor offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, struct_size) == 8, "request size offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, flags) == 12, "request flags offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, request_id) == 16, "request id offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_REQUEST_HEADER, reserved) == 24, "request reserved offset");
PUBGEXT_STATIC_ASSERT(sizeof(PUBGEXT_RESPONSE_HEADER) == 40, "response header size");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, magic) == 0, "response magic offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, major) == 4, "response major offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, minor) == 6, "response minor offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, struct_size) == 8, "response size offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, flags) == 12, "response flags offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, request_id) == 16, "response id offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, operation_status) == 24, "response status offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, reserved) == 28, "response reserved offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_RESPONSE_HEADER, transferred) == 32, "response transferred offset");
PUBGEXT_STATIC_ASSERT(sizeof(PUBGEXT_READ_REQUEST) == 56, "read request size");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_READ_REQUEST, header) == 0, "read header offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_READ_REQUEST, pid) == 32, "read pid offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_READ_REQUEST, remote_va) == 40, "read va offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_READ_REQUEST, length) == 48, "read length offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_READ_REQUEST, reserved1) == 52, "read reserved offset");
PUBGEXT_STATIC_ASSERT(sizeof(PUBGEXT_WRITE_REQUEST) == 64, "write request minimum storage size");
PUBGEXT_STATIC_ASSERT(PUBGEXT_WRITE_FIXED_SIZE == 56, "write fixed size");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_WRITE_REQUEST, data) == 56, "write data offset");
PUBGEXT_STATIC_ASSERT(sizeof(PUBGEXT_QUERY_CAPS_RESPONSE) == 48, "caps response size");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_QUERY_CAPS_RESPONSE, caps) == 40, "caps offset");
PUBGEXT_STATIC_ASSERT(__builtin_offsetof(PUBGEXT_QUERY_CAPS_RESPONSE, max_transfer) == 44, "max transfer offset");

#undef PUBGEXT_STATIC_ASSERT
#undef __builtin_offsetof
#if !defined(__cplusplus)
#undef PUBGEXT_ASSERT_JOIN
#undef PUBGEXT_ASSERT_JOIN_
#endif

#endif
