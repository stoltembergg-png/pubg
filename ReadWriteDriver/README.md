# ReadWriteDriver

This directory contains the conservative device + IOCTL payload and its mapper.
The current transport uses no legacy interception: the payload creates
`\\Device\\PubgExtRw` and the `\\DosDevices\\PubgExtRw` link; user mode opens
it as `\\.\PubgExtRw` and uses `DeviceIoControl` for AUTH, QUERY_CAPS, READ and
WRITE.

## Lifetime and authorization policy

The mapper performs fail-closed profile and integrity preflight, maps the
payload, and calls its versioned initialization ABI. The payload owns the
device and its dispatch routines. `DriverUnload` is explicitly `NULL` in both
components: hot-unload is not supported by design, and the mapped image is
never released until reboot. This is intentional protection against device,
dispatch, and image lifetime races.

The device uses `IoCreateDeviceSecure` with
`D:P(A;;GA;;;SY)(A;;GA;;;BA)` and `FILE_DEVICE_SECURE_OPEN`. A session is
bound to the `FILE_OBJECT`; its opener must be the exact normalized full image
path allowlisted in `ReadWriteDriver/driver.c`. Duplicated handles from a
different requestor process are rejected. No PID, token, or user pointer is an
authorization primitive.

The payload initialization ABI is defined in
`ReadWriteDriver/ReadWriteDriver/payload_api.h`
(currently major 1, minor 0). The public IOCTL ABI is `METHOD_BUFFERED`, device
type `0x8337`, and is defined only in `../PubgExt/driver/ioctl_protocol.h`.
Virtual copies run only at `PASSIVE_LEVEL`, attach to the target process, probe
the remote range page by page within 64 KiB chunks, and consume the process
reference in `__finally`.

## Rebuilding the embedded payload

Build `ReadWriteDriver.sys` before regenerating the mapper array. The canonical
input path is `ReadWriteDriver/x64/Release/ReadWriteDriver.sys`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ReadWriteDriver/tools/embed_payload.ps1
```

The script derives the declared array length from that path and writes
`ReadWriteDriverMapper/driver.h`. It emits the bytes without transformation,
so the generated array size and SHA-256 are expected to match the input
exactly. **Regenerate the embedded payload after every driver recompilation**;
otherwise the mapper can load stale bytes and an incompatible payload ABI.

`DriverUnload` is explicitly `NULL`: hot-unload is not supported. Close the
client and reboot or restore the test VM instead of attempting to unload the
mapped image.
