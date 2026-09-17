# KDMapper

KDMapper is a simple tool that exploits iqvw64e.sys Intel driver to manually map non-signed drivers in memory

Tested from **Windows 10 1607** to **Windows 11 25H2 26200.8875** :heavy_check_mark:

**Note:** Add definition **DISABLE_OUTPUT** to remove all console outputs

**Note:** Is recommended set a custom entry point like in the HelloWorld example


## Parameters:

	--copy-header to enable the copy driver header option by commandline
	--free to automatically unmap the allocated memory
	--indPages to map in allocated independent pages
	--PassAllocationPtr to pass allocation ptr as first param
	[PDB offset based build only]:
	--offsetsPath "FilePath" to include your own offsets file path (by default .\offsets.ini)(if FilePath contains spaces, it must be enclosed in quotation marks)
	--dontUpdateOffsets to execute without updating the offsets file (warning: you have to be sure that the offsets are not outdated to your current windows build, or you risk a potential BSOD)
	

## Features:
	
	Works with /GS- compiled drivers
	Hooks NtAddAtom which exists everywhere and is rarely called
	Clears MmUnloadedDrivers
	Clears PiDDBCacheTable
	Clears g_KernelHashBucketList
	Clears Wdfilter RuntimeDriverList RuntimeDriverCount and RuntimeDriverArry
	Use NtLoadDriver and NtUnloadDriver for less traces
	Prevent load if \Device\Nal exists (Prevents BSOD)
	Header section skipped while copying driver to kernel
	Added the possibility to modify params before call driver entry
	Added PDB_OFFSETS macro that will allow the use of Offset PDB features (choose the target build)
	Introduced new project "SymbolsFromPDB" that will help KDMapper to adapt quickly to any windows updates by parsing the target .PDB files (PDB_OFFSETS macro must be defined to use this feature)

## Building:

- Install Visual Studio: https://visualstudio.microsoft.com/downloads/
- Install Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/
- Install Windows WDK: https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk
- Open the solution
- Choose target build eg. Release
- Build

## Requirements:

- Return from driver entry fastest as you can to prevent unexpected calls or patch guard, don't ever create a infinite while loop in the driver entry, create a thread or any other procedure to keep code running (if you can't close kdmapper you are doing it wrong)
- Disable vulnerable driver list if enabled https://support.microsoft.com/en-au/topic/kb5020779-the-vulnerable-driver-blocklist-after-the-october-2022-preview-release-3fcbe13a-6013-4118-b584-fcfbc6a09936

## Common issues

### BSOD with a simple driver
In DriverEntry, **DriverObject and RegistryPath** are **NULL** unless you specify anything! this is a manual mapped driver and **not a normal loading procedure**

### Error \\Device\\Nal is already in use.

This means that there is a intel driver already loaded or another instance of kdmapper is running or kdmapper crashed and didn't unload the previous driver.

If you are sure that there is no other instance of kdmapper running, you can try to restart your computer to fix this issue.

If the problem persists, you can try to unload the intel driver manually (If the driver was loaded with kdmapper will have a random name and will be located in %temp%), if not, the driver name is iqvw64e.sys and is installed with your network drivers.

### Errors 0xC0000022 and 0xC000009A:

Both are usually caused by FACEIT AC or other Antivirus/AntiCheat

**Note:** Some AntiCheats will keep running even if the game is not open

**Note:** Some Antivirus will keep blocking this even if you disabled them

### Error 0xC0000603:

The certificate has been blocked as vulnerable and the mapper will return a status of STATUS_IMAGE_CERT_REVOKED. More info at [Microsoft](https://support.microsoft.com/en-au/topic/kb5020779-the-vulnerable-driver-blocklist-after-the-october-2022-preview-release-3fcbe13a-6013-4118-b584-fcfbc6a09936)

If you want to disable your vulnerable driver list you have to open regedit.exe, go to HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\CI\Config and set 'VulnerableDriverBlocklistEnable' as dword with value 0 and restart to apply the changes
Alternativelty check this [Microsoft](https://support.microsoft.com/en-us/topic/kb5020779-the-vulnerable-driver-blocklist-after-the-october-2022-preview-release-3fcbe13a-6013-4118-b584-fcfbc6a09936)

## Creators and contributors

Original creator https://github.com/z175

Updated and improved by https://github.com/TheCruZ

Initial PDB offsets parser written by https://github.com/Mohi-eddine

Some code cleaning https://github.com/tihomirocrew

Have Fun!!


---

# Repository audit note

This file is the upstream README retained in the vendored source directory.
The snapshot provenance, license record, coverage analysis, and build result
are in `VENDORING.md` and the sections below. On Windows, this `README.MD`
path is the same case-insensitive path requested as `README.md`.

## Legacy binary relationship

`tools/kdmapper.exe` is a legacy, opaque, non-audited binary. It remains in
place and is not vendored or replaced by this change. The source in this
directory is the auditable reference for future builds and fixes; a matching
size does not establish binary identity or behavioral equivalence.

No mapper invocation, driver load, service start, kernel access, or driver
execution was performed during this audit.

## Upstream build coverage

The upstream README claims testing from Windows 10 1607 through Windows 11
25H2 build 26200.8875 (`README.MD:5`). The source does not contain a complete
build-number compatibility table; the following is the evidence-based map of
the claims and version-sensitive code found in the snapshot.

| Windows build/revision | Status in this snapshot | Evidence |
|---|---|---|
| 1607 | Upstream-tested range claim; not independently verified | `README.MD:5`; older PiDDB pattern fallback is described at `intel_driver.cpp:967` |
| 1803 through 24H2 | Pattern comments claim tested coverage for selected kernel routines | `intel_driver.cpp:546`, `intel_driver.cpp:641` |
| 22000.132 | Explicit PiDDB lock pattern update | `intel_driver.cpp:964` |
| 22449 and newer | Explicit alternate PiDDB lock pattern | `intel_driver.cpp:968` |
| 26100.1000 | Explicit third PiDDB lock pattern update | `intel_driver.cpp:970` |
| 26200.8875 | Upstream README claim only; no build-specific 26200 pattern/offset is present | `README.MD:5`; latest explicit pattern comment is `intel_driver.cpp:970` |
| 26200.9457 (target host) | Static signatures were checked against the local `ntoskrnl.exe`/`ci.dll`/`WdFilter.sys` files; runtime behavior remains unverified | detailed evidence and remaining gaps are in `VENDORING.md` |
| 26100.9457 | Static kernel signatures and PDB-agreeing routine RVAs were checked locally; runtime behavior remains unverified | detailed evidence and remaining gaps are in `VENDORING.md` |

The `PDB_OFFSETS` build can obtain offsets dynamically from the target system's
PDBs, which is a mitigation for new builds, not proof that this snapshot works
on them. The symbols and updater path are defined at
`SymbolsFromPDB/main.cpp:23-44` and loaded by `KDSymbolsHandler.cpp:11-66`.
The PDB updater itself was not executed here; it requires target symbols and
the upstream DbgHelp runtime, whose DLLs were excluded as recorded in
`VENDORING.md`.

## Version-sensitive implementation map

- Hard-coded kernel-structure offsets include WdFilter fields at
  `intel_driver.cpp:233-235`, list/object traversal offsets at
  `intel_driver.cpp:308`, `315`, `345`, and kernel object traversal offsets at
  `intel_driver.cpp:815-849`.
- Signature/pattern scanning is concentrated in
  `intel_driver.cpp:246-294`, `546-664`, `964-998`, and `1122-1125`;
  `utils.cpp:93` implements the byte-mask scanner.
- The PDB symbol set covers `MmAllocateIndependentPages` (the mapper consumer
  also accepts the older `MmAllocateIndependentPagesEx` spelling),
  `MmFreeIndependentPages`, `PiDDBLock`, `PiDDBCacheTable`,
  `MmSetPageProtection`, `g_KernelHashBucketList`, `g_HashCacheLock`, and
  WdFilter symbols at `SymbolsFromPDB/main.cpp:28-44`.
- The Intel driver's IOCTL and command case numbers are hard-coded at
  `kdmapper/include/intel_driver.hpp:12` and
  `kdmapper/intel_driver.cpp:413-479`.

## Vulnerable driver and host controls

- Driver name: `iqvw64e.sys`, exposed through `\\.\Nal`; evidence is
  `README.MD:3`, `README.MD:63`, and `intel_driver.cpp:98-169`.
- Driver version: the embedded PE version resource identifies `iQVW64.SYS`
  as `1.03.0.7 built by: WinDDK`, with product version `1.03.0.7` and company
  `Intel Corporation`. This metadata is in the byte array retained at
  `kdmapper/include/intel_driver_resource.hpp:1-873` and was inspected from
  the non-executed Release PE; it is not a guarantee that a separately
  installed `iqvw64e.sys` has the same version.
- Windows vulnerable-driver blocklist: a dependency/operational constraint.
  The upstream README instructs that the blocklist may need to be disabled at
  `README.MD:47-50` and `65-78`; the service code handles
  `STATUS_IMAGE_CERT_REVOKED` at `service.cpp:65-69`.
- Antivirus/anti-cheat: also an operational constraint, with explicit error
  handling at `README.MD:65-71` and `service.cpp:70-72`.

## Build check

On 2026-09-16, Visual Studio 2022 Build Tools (MSVC 14.44.35207, v143) built
the upstream solution with `Release|x64`:

```text
MSBuild tools\kdmapper-src\kdmapper.sln /m /t:Build /p:Configuration=Release /p:Platform=x64 /nologo
exit code: 0
0 warnings, 0 errors
```

The generated `tools/kdmapper-src/x64/Release/kdmapper_Release.exe` is
154,112 bytes, exactly the same size as the legacy `tools/kdmapper.exe`.
SHA-256 differs, however:

- legacy: `BEA7B418C7D53D59F0C6B8A02A335EF2C9EFAA015F1E6F6CC46D9C68476CA5E4`
- source build after the local coverage patch: `EDF0626377C4642C3B23A54CB94113DE068818576D0D374CC6B00A7E74515D06`

Both inspected PE files are x64 CUI images with the same six-section summary
and the same imports. Neither contains file/product version metadata. No
runtime behavior was compared because even the normal usage path calls
`intel_driver::Load()` (`main.cpp:183`), which would load the vulnerable
driver; no `--help`-only mode exists in this source.

## If a new Windows build breaks the mapper

1. Prefer the `PDB_OFFSETS` configuration and regenerate offsets with
   `SymbolsFromPDB`; verify every required symbol before use.
2. If static scanning remains required, update the signatures and relative
   address adjustments in `kdmapper/intel_driver.cpp` near the affected
   routine (`Mm*`, PiDDB, WdFilter, or CI).
3. Re-check hard-coded structure offsets in `intel_driver.cpp`, especially the
   WdFilter and driver-object traversal paths.
4. Build and inspect the new Release x64 artifact, then validate only in an
   isolated, authorized test environment; do not treat the legacy binary as a
   compatibility oracle.

The exact 26200.9457 and 26100.9457 behavior, separately installed driver
identity, kernel compatibility, and PDB updater execution were not verified in
this non-loading audit.

