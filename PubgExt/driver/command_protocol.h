#ifndef PUBGEXT_DRIVER_COMMAND_PROTOCOL_H
#define PUBGEXT_DRIVER_COMMAND_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_VERSION 1u
#define COMMAND_MAGIC    0x50554247u

#define COMMAND_READWRITE  0xB16B00B5u
#define COMMAND_GETPROCPID 0x00BADA55u
#define COMMAND_ISLOADED   0x00069420u

#define COMMAND_FLAG_WRITE 0x00000001u

typedef struct _PUBGEXT_DRIVER_COMMAND
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t reserved;
    uint64_t auth_token;
    uint64_t user_result;
    uint32_t op;
    uint32_t flags;
    uint64_t pid;
    uint64_t addr;
    uint64_t src;
    uint64_t dst;
    uint64_t len;
    uint64_t result;
    int32_t status;
    uint32_t reserved_result;
} Command;

#if defined(__cplusplus)
#define PUBGEXT_PROTOCOL_ASSERT(expression, message) static_assert((expression), message)
#else
#define PUBGEXT_PROTOCOL_ASSERT_NAME_JOIN(left, right) left##right
#define PUBGEXT_PROTOCOL_ASSERT_NAME(left, right) PUBGEXT_PROTOCOL_ASSERT_NAME_JOIN(left, right)
#define PUBGEXT_PROTOCOL_ASSERT(expression, message) \
    typedef char PUBGEXT_PROTOCOL_ASSERT_NAME(pubgext_protocol_assertion_, __LINE__)[(expression) ? 1 : -1]
#endif

PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, magic) == 0, "Command.magic layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, version) == 4, "Command.version layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, size) == 8, "Command.size layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, auth_token) == 16, "Command.auth_token layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, user_result) == 24, "Command.user_result layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, op) == 32, "Command.op layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, pid) == 40, "Command.pid layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, addr) == 48, "Command.addr layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, src) == 56, "Command.src layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, dst) == 64, "Command.dst layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, len) == 72, "Command.len layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, result) == 80, "Command.result layout changed");
PUBGEXT_PROTOCOL_ASSERT(offsetof(Command, status) == 88, "Command.status layout changed");
PUBGEXT_PROTOCOL_ASSERT(sizeof(Command) == 96, "Command size changed");

#undef PUBGEXT_PROTOCOL_ASSERT
#if !defined(__cplusplus)
#undef PUBGEXT_PROTOCOL_ASSERT_NAME
#undef PUBGEXT_PROTOCOL_ASSERT_NAME_JOIN
#endif

#endif
