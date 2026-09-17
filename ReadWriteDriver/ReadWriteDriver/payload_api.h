#ifndef READWRITEDRIVER_PAYLOAD_API_H
#define READWRITEDRIVER_PAYLOAD_API_H

#include <stdint.h>

#define PUBGEXT_PAYLOAD_ABI_MAJOR 1u
#define PUBGEXT_PAYLOAD_ABI_MINOR 0u

typedef struct _PUBGEXT_PAYLOAD_INIT {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint64_t driver_object;
} PUBGEXT_PAYLOAD_INIT;

typedef struct _PUBGEXT_PAYLOAD_INIT_RESULT {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    int32_t status;
    uint32_t device_created;
} PUBGEXT_PAYLOAD_INIT_RESULT;

typedef int32_t (*PUBGEXT_PAYLOAD_ENTRY)(const PUBGEXT_PAYLOAD_INIT* init,
    PUBGEXT_PAYLOAD_INIT_RESULT* result);

#if defined(__cplusplus)
#define PUBGEXT_PAYLOAD_ASSERT(e, m) static_assert((e), m)
#else
#define PUBGEXT_PAYLOAD_ASSERT_JOIN_(a, b) a##b
#define PUBGEXT_PAYLOAD_ASSERT_JOIN(a, b) PUBGEXT_PAYLOAD_ASSERT_JOIN_(a, b)
#define PUBGEXT_PAYLOAD_ASSERT(e, m) typedef char PUBGEXT_PAYLOAD_ASSERT_JOIN(payload_assert_, __LINE__)[(e) ? 1 : -1]
#endif
#define PUBGEXT_PAYLOAD_OFFSET_OF(type, member) ((uint32_t)(uintptr_t)&(((type*)0)->member))
#define __builtin_offsetof(type, member) PUBGEXT_PAYLOAD_OFFSET_OF(type, member)
PUBGEXT_PAYLOAD_ASSERT(sizeof(PUBGEXT_PAYLOAD_INIT) == 24, "payload init size");
PUBGEXT_PAYLOAD_ASSERT(sizeof(PUBGEXT_PAYLOAD_INIT_RESULT) == 16, "payload result size");
PUBGEXT_PAYLOAD_ASSERT(__builtin_offsetof(PUBGEXT_PAYLOAD_INIT, abi_major) == 4, "payload init version offset");
PUBGEXT_PAYLOAD_ASSERT(__builtin_offsetof(PUBGEXT_PAYLOAD_INIT, driver_object) == 16, "payload driver object offset");
PUBGEXT_PAYLOAD_ASSERT(__builtin_offsetof(PUBGEXT_PAYLOAD_INIT_RESULT, status) == 8, "payload result status offset");
PUBGEXT_PAYLOAD_ASSERT(__builtin_offsetof(PUBGEXT_PAYLOAD_INIT_RESULT, device_created) == 12, "payload result device offset");
#undef PUBGEXT_PAYLOAD_ASSERT
#undef __builtin_offsetof
#if !defined(__cplusplus)
#undef PUBGEXT_PAYLOAD_ASSERT_JOIN
#undef PUBGEXT_PAYLOAD_ASSERT_JOIN_
#endif

#endif
