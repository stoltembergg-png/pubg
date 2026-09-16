#ifndef PUBGEXT_DRIVER_COMMON_H
#define PUBGEXT_DRIVER_COMMON_H

#include <windows.h>
#include <cstddef>
#include <cstdint>

// TODO: consolidate this definition into one header shared by user mode and
// the kernel.  Until then, ReadWriteDriver must mirror this SAME header
// verbatim; the driver receives Command through NtUserSetSysColors.
#define PROTOCOL_VERSION 1u

typedef struct _PUBGEXT_DRIVER_COMMAND
{
    uintptr_t      selfref;
    uintptr_t      pid;
    uintptr_t      destination;
    int            cmdId;
    char           rw;
    unsigned char* pSource;
    SIZE_T         size;
} Command;

// Keep these checks in sync with the kernel's copy before changing Command.
// The conditional values support both the x86 and x64 layout rules while the
// RuntimeBroker target is normally built for x64.
static_assert(offsetof(Command, selfref) == 0, "Command.selfref layout changed");
static_assert(offsetof(Command, pid) == sizeof(uintptr_t), "Command.pid layout changed");
static_assert(offsetof(Command, destination) == 2 * sizeof(uintptr_t),
              "Command.destination layout changed");
static_assert(offsetof(Command, cmdId) == 3 * sizeof(uintptr_t),
              "Command.cmdId layout changed");
static_assert(offsetof(Command, rw) == 3 * sizeof(uintptr_t) + sizeof(int),
              "Command.rw layout changed");
static_assert(offsetof(Command, pSource) == (sizeof(uintptr_t) == 8 ? 32u : 20u),
              "Command.pSource layout changed");
static_assert(offsetof(Command, size) == (sizeof(uintptr_t) == 8 ? 40u : 24u),
              "Command.size layout changed");
static_assert(sizeof(Command) == (sizeof(uintptr_t) == 8 ? 48u : 28u),
              "Command size changed; update the kernel mirror first");

#define COMMAND_READWRITE  0xB16B00B5
#define COMMAND_GETPROCPID 0xBADA55
#define COMMAND_ISLOADED   0x69420

#endif // PUBGEXT_DRIVER_COMMON_H
