# kdmapper vendoring record

This directory is a clearly separated vendor snapshot of the upstream project.
The targeted compatibility changes and offline coverage record below are kept
inside this directory; no generated legacy binary outside this directory was
modified.

## Provenance

- Upstream: <https://github.com/TheCruZ/kdmapper>
- Snapshot method: shallow clone of the upstream `master` branch
- Upstream commit: `48ac931d87372702a23c6f34ee7b8440787d9fc7`
- Commit timestamp (exact): `2026-08-23T16:14:22+02:00`
- Vendored on: `2026-09-16`

## License

Upstream contains `LICENSE`, which declares the MIT License and copyright
`(c) 2021 TheCruZ`. The original `LICENSE` file is retained unchanged. The
MIT attribution and permission notice must remain in copies or substantial
portions of this code.

The source also credits the original creator and contributors in
`README.MD:80-88`; those notices were retained.

## Exclusions from the vendor snapshot

The following upstream checkout artifacts were intentionally removed because
this repository vendors the auditable source rather than prebuilt payloads:

- `.git/` (the shallow clone metadata; the commit is recorded above);
- `HelloWorld.sys` (standalone sample driver binary);
- `DbgHelp/dbghelp.dll` (upstream binary, 2,254,392 bytes);
- `DbgHelp/symsrv.dll` (upstream binary, 423,480 bytes).

The source file `kdmapper/include/intel_driver_resource.hpp` is retained as
upstream source even though it contains an embedded driver byte array. Build
products under `Intermediate/` and `x64/` are local outputs and are ignored;
they are not part of the vendored source. The repository's existing
`tools/kdmapper.exe` was not deleted, changed, or added to version control.

## Offline coverage audit: local 26100.9457 kernel

The local files were inspected read-only. No kdmapper invocation, vulnerable
driver load, service start, kernel read/write, or mapped-driver execution was
performed. The target `ntoskrnl.exe` is file version `10.0.26100.9457`, TDS
`0xFDA9ED74`, image size `0x1450000`, and checksum `0xC817D5`. The local
`ci.dll` is `10.0.26100.9444`; `WdFilter.sys` is `4.18.25080.5` and is present
at `C:\Windows\System32\drivers\WdFilter.sys`.

### Pattern results

RVA values below are file RVAs. “Unique” means exactly one match in the named
module section; evidence is the beginning of the matched byte sequence.

| Item | Source | Target | Local result | Correction / confidence |
|---|---|---|---|---|
| `MmAllocateIndependentPagesEx` signature | `kdmapper/intel_driver.cpp:548-550` | `ntoskrnl:.text` | Unique at `0x495412`; call resolves to `0xAA42A0`; `41 8B D6 B9 00 10 00 00 E8 81 EE 60 00 48 8B D8` | No change. PDB value `0xAA42A0` agrees; high |
| PDB name for `MmAllocateIndependentPagesEx` | `SymbolsFromPDB/main.cpp:29`, `intel_driver.cpp:536-541` | `ntoskrnl` PDB | Generated profile exposes exact unique symbol `MmAllocateIndependentPages` at `0xAA42A0`; `...Ex` is not the local PDB spelling | PDB updater now requests the local spelling; consumer keeps `...Ex` fallback for older PDBs; high |
| `MmFreeIndependentPages` primary | `intel_driver.cpp:586-588` | `ntoskrnl:PAGE` | No match | Fallback is selected; high for local fallback |
| `MmFreeIndependentPages` Win11 fallback | `intel_driver.cpp:594-596` | `ntoskrnl:PAGE` | Unique at `0x805D6A`; call resolves to `0x2065C0`; `8B 15 EC F2 7B 00 48 8B CB E8 48 08 A0 FF 48 8D 8B` | No change. PDB value `0x2065C0` agrees; high |
| `MmSetPageProtection` primary | `intel_driver.cpp:644-646` | `ntoskrnl:PAGELK` | Unique at `0xB64C11`; call resolves to `0x4E5EE0`; `0F 45 D8 48 8D AE 50 D0 FF FF E8 C0 12 98 FF` | No change. PDB value `0x4E5EE0` agrees; high |
| `MmSetPageProtection` fallback | `intel_driver.cpp:649-651` | `ntoskrnl:PAGELK` | No match after evaluating the corrected 20-byte mask | Upstream mask was 17 characters for a 20-byte pattern; mask corrected; high |
| PiDDB lock, 22000 pattern | `intel_driver.cpp:964` | `ntoskrnl:PAGE` | No match | 26100 pattern selected; no replacement bytes invented |
| PiDDB lock, 22449+ pattern | `intel_driver.cpp:968` | `ntoskrnl:PAGE` | No match | 26100 pattern selected; no replacement bytes invented |
| PiDDB lock, 26100 pattern | `intel_driver.cpp:970` | `ntoskrnl:PAGE` | Unique at `0x94F284`; LEA resolves to `0xF8B8E0` in writable `.data`; `8B D8 85 C0 0F 88 CC 00 00 00 65 48 8B 04 25 88 01 00 00 48 8D 0D 42 C6 63 00` | No change; high for exact local pattern/target |
| PiDDB cache table primary | `intel_driver.cpp:965` | `ntoskrnl:PAGE` | No match | Fallback selected; no replacement bytes invented |
| PiDDB cache table fallback | `intel_driver.cpp:990` | `ntoskrnl:PAGE` | Unique at `0x94F3F5`; LEA resolves to `0xFDA480` in writable `PAGEDATA`; `48 8B F9 33 C0 48 8D 0D 7F B0 68 00 45 33 F6` | No change; high for exact local pattern/target |
| WdFilter runtime list | `intel_driver.cpp:246` | `WdFilter.sys:PAGE` | Unique at `0x65D6F`; RIP target `0x25AF0` in writable `.data`; `48 8B 0D 7A FD FB FF FF 05 5C FD FB FF` | No change; high for local module |
| WdFilter runtime count | `intel_driver.cpp:252` | `WdFilter.sys:PAGE` | Unique at `0x65D76`; RIP target `0x25AD8` in writable `.data`; `FF 05 5C FD FB FF 48 39 11` | No change; high for local module |
| WdFilter `MpFreeDriverInfoEx` primary | `intel_driver.cpp:267` | `WdFilter.sys:PAGE` | Unique at `0x80309`; call resolves to `0x6D6A4` in executable `PAGE`; `89 42 08 E8 93 D3 FE FF 48 8B 0D C8 57 FA FF E9` | No change; high for local module |
| WdFilter `MpFreeDriverInfoEx` fallback | `intel_driver.cpp:276` | `WdFilter.sys:PAGE` | No match | Primary selected; no replacement bytes invented |
| CI hash-list signature, before | `intel_driver.cpp:1154` | `ci.dll:PAGE` | Two matches: `0x7C87F` and `0x7C8D9`; targets `0xEB090` and `0xEB098`, both writable `PAGEDATA` | Ambiguous on local build; narrowed with `75 40` context |
| CI hash-list signature, after | `intel_driver.cpp:1154` | `ci.dll:PAGE` | Unique at `0x7C87F`; target `0xEB090` in writable `PAGEDATA`; `48 8B 1D 0A E8 06 00 EB 4C F7 43 40 00 20 00 00 75 40` | Added local distinguishing branch bytes; high for local static scan |
| CI hash-cache lock search, before | `intel_driver.cpp:1159` | `ci.dll:PAGE` window | Two `48 8D 0D` matches in the first candidate window | Ambiguous short search |
| CI hash-cache lock search, after | `intel_driver.cpp:1162-1169` | `ci.dll:PAGE` window | Unique contextual match at `0x7C84F` (`44 8D 7D 01 41 8A D7 48 8D 0D`), then `+7` to the LEA; resolves to `0x45FC0` in writable `.data` | Context narrowed without hard-coding displacement; high for local static scan |

### Non-pattern build/ABI assumptions

| Assumption | Source | Offline status |
|---|---|---|
| WdFilter `MpBmDocOpenRules` fields `+0x60` (count), `+0x68` (array), `+0x70` (list) | `intel_driver.cpp:233-235` | The three surrounding local references resolve to writable WdFilter data, but private symbol/type metadata was unavailable; field layout remains unverified |
| WdFilter entry `UNICODE_STRING` at `Entry+0x10`, array back-reference at `Entry-0x10`, driver-info base at `Entry-0x20`, magic `0xDA18` | `intel_driver.cpp:308`, `315`, `345`, `350` | Requires live WdFilter objects; not verifiable from the file-only scan |
| `MmUnloadedDrivers` traversal `object+0x8`, `device+0x8`, `driver+0x28`, `section+0x58` | `intel_driver.cpp:815-849` | Requires live kernel object graph; not verified and not changed |
| PiDDB/AVL/list layouts | `kdmapper/include/nt.hpp:87-126`, `intel_driver.cpp:1029-1066` | Standard structure declarations are compiled unchanged; private kernel type layout was not exposed by the generated profile, so runtime object compatibility is unverified |
| Intel vulnerable-driver IOCTL cases `0x33`, `0x30`, `0x25`, `0x19`, `0x1A` | `intel_driver.cpp:413-479` | Vulnerable-driver ABI dependency, not a Windows build offset; no device interaction was performed |

The three kernel routine targets also match the generated PDB profile:
`MmAllocateIndependentPages=0xAA42A0`, `MmFreeIndependentPages=0x2065C0`,
and `MmSetPageProtection=0x4E5EE0`, each in an executable section. The PDB
profile did not expose `PiDDBLock`, `PiDDBCacheTable`, CI globals, or the
private WdFilter symbols, so those values are disassembly/RIP-resolution
evidence only; no private symbol value is claimed.

### Build gate and remaining gaps

There is no runtime build-number gate in this snapshot. `README.MD:5` is a
documentation claim, not a rejection/acceptance check, so neither
`26200.9457` nor `26100.9457` is rejected by a version gate. The static
patterns listed above are validated for the local files; this does not turn
the upstream `26200.8875` claim into general 26200 coverage. The PDB path now
also checks the local direct `drivers\\WdFilter.sys` layout, preventing that
module from being silently skipped during a future offsets refresh.

Still not verified: execution of the PDB updater/symbol download; behavior of
the `iqvw64e.sys` exploit under vulnerable-driver blocklist, HVCI, antivirus,
or anti-cheat controls; actual driver loading, mapping, cleanup, PatchGuard,
or any kernel behavior requiring execution. `win32kbase.sys` was identified
locally but is not a kdmapper target in this source and no kdmapper offset was
derived for it.

### Rebuild after coverage patch

Command: `MSBuild tools\\kdmapper-src\\kdmapper.sln /m /t:Build /p:Configuration=Release /p:Platform=x64 /nologo`

Result: exit code `0`, 0 warnings, 0 errors. The rebuilt source artifact is
`tools/kdmapper-src/x64/Release/kdmapper_Release.exe`, 154,112 bytes. The
legacy `tools/kdmapper.exe` was not touched; its SHA-256 remains
`BEA7B418C7D53D59F0C6B8A02A335EF2C9EFAA015F1E6F6CC46D9C68476CA5E4`.
