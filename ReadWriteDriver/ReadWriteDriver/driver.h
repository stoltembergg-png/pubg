#pragma once

#include <ntifs.h>
#include <wdmsec.h>
#include <stdint.h>

/* The payload is initialized by the mapper and owns the device dispatch. */
static PVOID g_session_object;
