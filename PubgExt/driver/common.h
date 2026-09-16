#ifndef PUBGEXT_DRIVER_COMMON_H
#define PUBGEXT_DRIVER_COMMON_H

#include <windows.h>
#include <cstddef>
#include <cstdint>

// This structure is shared verbatim with ReadWriteDriver/driver.h.  The
// driver receives it through the NtUserSetSysColors hook, so its field order
// and pointer-sized fields must not be changed without changing the driver.
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

#define COMMAND_READWRITE  0xB16B00B5
#define COMMAND_GETPROCPID 0xBADA55
#define COMMAND_ISLOADED   0x69420

#endif // PUBGEXT_DRIVER_COMMON_H
