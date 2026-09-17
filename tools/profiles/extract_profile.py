#!/usr/bin/env python3
"""Create a machine-local compatibility profile from Windows PE files.

This program is deliberately read-only with respect to the input binaries. It
does not load a driver. dbghelp is used first because it is already shipped by
Windows; the symbol cache and downloaded PDBs live outside the Windows tree.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wintypes
import json
import os
import struct
import sys
import tempfile
import uuid
from pathlib import Path
from typing import Any

try:
    import pefile
except ImportError as exc:
    raise SystemExit("pefile is required: python -m pip install pefile") from exc

try:
    import requests
except ImportError as exc:
    raise SystemExit("requests is required: python -m pip install requests") from exc

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM, CS_OP_REG
    from capstone.x86_const import X86_REG_RIP
except ImportError:
    Cs = None


SYMBOL_PATH = "https://msdl.microsoft.com/download/symbols"
SYMOPT_UNDNAME = 0x00000002
SYMOPT_DEFERRED_LOADS = 0x00000004
SYMOPT_LOAD_LINES = 0x00000010
SYMOPT_FAIL_CRITICAL_ERRORS = 0x00000200
TI_GET_SYMNAME = 1
TI_FINDCHILDREN = 7
TI_GET_OFFSET = 14


def hex_value(value: int | None) -> str | None:
    return None if value is None else f"0x{value:X}"


def version_string(pe: pefile.PE) -> str | None:
    if not hasattr(pe, "VS_FIXEDFILEINFO") or not pe.VS_FIXEDFILEINFO:
        return None
    info = pe.VS_FIXEDFILEINFO[0]
    return ".".join(
        str(x)
        for x in (
            info.FileVersionMS >> 16,
            info.FileVersionMS & 0xFFFF,
            info.FileVersionLS >> 16,
            info.FileVersionLS & 0xFFFF,
        )
    )


def read_codeview(pe: pefile.PE) -> dict[str, Any]:
    result: dict[str, Any] = {"pdb": None, "guid": None, "age": None, "guid_age": None}
    try:
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"]]
        )
        for entry in pe.DIRECTORY_ENTRY_DEBUG:
            if entry.struct.Type != 2:
                continue
            data = pe.get_data(entry.struct.AddressOfRawData, entry.struct.SizeOfData)
            if data[:4] == b"RSDS" and len(data) >= 24:
                guid = str(uuid.UUID(bytes_le=data[4:20])).upper()
                age = struct.unpack_from("<I", data, 20)[0]
                pdb_name = data[24:].split(b"\0", 1)[0].decode("utf-8", "replace")
                result.update(
                    pdb=Path(pdb_name).name,
                    guid=guid,
                    age=age,
                    # Symbol-store keys use the 32 hexadecimal GUID digits,
                    # followed immediately by the age (no hyphens).
                    guid_age=f"{guid.replace('-', '')}{age:X}",
                )
                break
            if data[:4] == b"NB10" and len(data) >= 16:
                age = struct.unpack_from("<I", data, 12)[0]
                pdb_name = data[16:].split(b"\0", 1)[0].decode("utf-8", "replace")
                result.update(pdb=Path(pdb_name).name, age=age, format="NB10")
                break
    except (AttributeError, pefile.PEFormatError, IndexError, struct.error):
        pass
    return result


def section_for_rva(pe: pefile.PE, rva: int | None) -> dict[str, Any] | None:
    if rva is None:
        return None
    for section in pe.sections:
        start = section.VirtualAddress
        end = start + max(section.Misc_VirtualSize, section.SizeOfRawData)
        if start <= rva < end:
            chars = int(section.Characteristics)
            executable = bool(chars & 0x20000000)
            writable = bool(chars & 0x80000000)
            characteristics_flags = []
            for bit, label in (
                (0x00000020, "IMAGE_SCN_CNT_CODE"),
                (0x00000040, "IMAGE_SCN_CNT_INITIALIZED_DATA"),
                (0x00000080, "IMAGE_SCN_CNT_UNINITIALIZED_DATA"),
                (0x02000000, "IMAGE_SCN_MEM_DISCARDABLE"),
                (0x04000000, "IMAGE_SCN_MEM_NOT_CACHED"),
                (0x08000000, "IMAGE_SCN_MEM_NOT_PAGED"),
                (0x10000000, "IMAGE_SCN_MEM_SHARED"),
                (0x20000000, "IMAGE_SCN_MEM_EXECUTE"),
                (0x40000000, "IMAGE_SCN_MEM_READ"),
                (0x80000000, "IMAGE_SCN_MEM_WRITE"),
            ):
                if chars & bit:
                    characteristics_flags.append(label)
            return {
                "name": section.Name.rstrip(b"\0").decode("ascii", "replace"),
                "rva_start": hex_value(start),
                "rva_end": hex_value(end),
                "executable": executable,
                "writable": writable,
                "kind": "code" if executable else "data",
                "characteristics": hex_value(chars),
                "characteristics_flags": characteristics_flags,
            }
    return None


def module_identity(path: Path, pe: pefile.PE) -> dict[str, Any]:
    cv = read_codeview(pe)
    return {
        "name": path.name,
        "path": str(path),
        "file_version": version_string(pe),
        "file_size": path.stat().st_size,
        "time_date_stamp": int(pe.FILE_HEADER.TimeDateStamp),
        "time_date_stamp_hex": hex_value(int(pe.FILE_HEADER.TimeDateStamp)),
        "size_of_image": int(pe.OPTIONAL_HEADER.SizeOfImage),
        "size_of_image_hex": hex_value(int(pe.OPTIONAL_HEADER.SizeOfImage)),
        "check_sum": int(pe.OPTIONAL_HEADER.CheckSum),
        "check_sum_hex": hex_value(int(pe.OPTIONAL_HEADER.CheckSum)),
        "codeview": cv,
        "sections": [
            {
                "name": s.Name.rstrip(b"\0").decode("ascii", "replace"),
                "rva": hex_value(int(s.VirtualAddress)),
                "virtual_size": int(s.Misc_VirtualSize),
                "raw_size": int(s.SizeOfRawData),
                "characteristics": hex_value(int(s.Characteristics)),
            }
            for s in pe.sections
        ],
    }


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.ULONG),
        ("TypeIndex", wintypes.ULONG),
        ("Reserved", ctypes.c_ulonglong * 2),
        ("Index", wintypes.ULONG),
        ("Size", wintypes.ULONG),
        ("ModBase", ctypes.c_ulonglong),
        ("Flags", wintypes.ULONG),
        ("Value", ctypes.c_ulonglong),
        ("Address", ctypes.c_ulonglong),
        ("Register", wintypes.ULONG),
        ("Scope", wintypes.ULONG),
        ("Tag", wintypes.ULONG),
        ("NameLen", wintypes.ULONG),
        ("MaxNameLen", wintypes.ULONG),
        ("Name", ctypes.c_char * 1),
    ]


class TI_FINDCHILDREN_PARAMS(ctypes.Structure):
    _fields_ = [("Count", wintypes.ULONG), ("Start", wintypes.ULONG), ("ChildId", wintypes.ULONG * 1)]


class DbgHelp:
    """Small, isolated ctypes wrapper. Failure is retained as evidence."""

    def __init__(self, symbol_cache: Path):
        self.errors: list[str] = []
        self.handle = None
        self.modules: dict[str, tuple[int, pefile.PE]] = {}
        if os.name != "nt":
            self.errors.append("dbghelp is only available on Windows")
            return
        try:
            self.dll = ctypes.WinDLL("dbghelp.dll")
            self.kernel = ctypes.WinDLL("kernel32.dll")
            self.process = self.kernel.GetCurrentProcess()
            self.dll.SymInitializeW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, wintypes.BOOL]
            self.dll.SymInitializeW.restype = wintypes.BOOL
            self.dll.SymLoadModuleExW.argtypes = [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_wchar_p,
                ctypes.c_wchar_p,
                ctypes.c_ulonglong,
                wintypes.DWORD,
                ctypes.c_void_p,
                wintypes.DWORD,
            ]
            self.dll.SymLoadModuleExW.restype = ctypes.c_ulonglong
            self.dll.SymFromName.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
            self.dll.SymFromName.restype = wintypes.BOOL
            self.dll.SymGetTypeFromName.argtypes = [
                ctypes.c_void_p, ctypes.c_ulonglong, ctypes.c_char_p, ctypes.POINTER(wintypes.ULONG)
            ]
            self.dll.SymGetTypeFromName.restype = wintypes.BOOL
            self.dll.SymGetTypeInfo.argtypes = [
                ctypes.c_void_p, ctypes.c_ulonglong, wintypes.ULONG, wintypes.ULONG, ctypes.c_void_p
            ]
            self.dll.SymGetTypeInfo.restype = wintypes.BOOL
            if hasattr(self.dll, "SymUnloadModule64"):
                self.dll.SymUnloadModule64.argtypes = [ctypes.c_void_p, ctypes.c_ulonglong]
                self.dll.SymUnloadModule64.restype = wintypes.BOOL
            self.dll.SymCleanup.argtypes = [ctypes.c_void_p]
            self.dll.SymCleanup.restype = wintypes.BOOL
            self.dll.SymSetOptions(
                SYMOPT_UNDNAME
                | SYMOPT_DEFERRED_LOADS
                | SYMOPT_LOAD_LINES
                | SYMOPT_FAIL_CRITICAL_ERRORS
            )
            symbol_path = f"srv*{symbol_cache}*{SYMBOL_PATH}"
            if not self.dll.SymInitializeW(self.process, symbol_path, False):
                self.errors.append(f"SymInitializeW failed: {ctypes.get_last_error()}")
                return
            self.handle = self.process
        except (AttributeError, OSError) as exc:
            self.errors.append(f"dbghelp unavailable: {exc}")

    def load(self, path: Path, pe: pefile.PE) -> int | None:
        if not self.handle:
            return None
        try:
            base = self.dll.SymLoadModuleExW(
                self.process,
                None,
                str(path),
                None,
                int(pe.OPTIONAL_HEADER.ImageBase),
                int(pe.OPTIONAL_HEADER.SizeOfImage),
                None,
                0,
            )
            if not base:
                self.errors.append(
                    f"SymLoadModuleExW({path.name}) failed: {ctypes.get_last_error()}"
                )
                return None
            self.modules[path.name.lower()] = (int(base), pe)
            return int(base)
        except (AttributeError, OSError) as exc:
            self.errors.append(f"symbol load {path.name} failed: {exc}")
            return None

    def symbol(self, module: str, name: str) -> dict[str, Any] | None:
        if not self.handle:
            return None
        raw = ctypes.create_string_buffer(ctypes.sizeof(SYMBOL_INFO) + 1024)
        info = ctypes.cast(raw, ctypes.POINTER(SYMBOL_INFO)).contents
        # DbgHelp expects the offset of Name, not sizeof(SYMBOL_INFO).
        info.SizeOfStruct = SYMBOL_INFO.Name.offset
        info.MaxNameLen = 1024
        queries = [f"{module}!{name}", f"nt!{name}", f"ntkrnlmp!{name}", name]
        ok = False
        query_used = queries[0]
        for query in queries:
            try:
                ok = bool(self.dll.SymFromName(self.process, query.encode("ascii"), ctypes.byref(info)))
            except (AttributeError, OSError):
                ok = False
            if ok:
                query_used = query
                break
        if not ok:
            return None
        base, pe = self.modules.get(module.lower(), (None, None))
        if base is None:
            return None
        address = int(info.Address)
        rva = address - base
        section = section_for_rva(pe, rva)
        return {
            "name": name,
            "address": hex_value(address),
            "rva": rva,
            "rva_hex": hex_value(rva),
            "section": section,
            "method": "dbghelp",
            "evidence": f"SymFromName({query_used})",
            "confidence": "high" if section and section["executable"] else "UNKNOWN",
        }

    def type_field_offset(self, module: str, type_name: str, field_names: set[str]) -> dict[str, int]:
        if not self.handle or not hasattr(self.dll, "SymGetTypeFromName"):
            return {}
        base, _ = self.modules.get(module.lower(), (None, None))
        if base is None:
            return {}
        type_id = wintypes.ULONG()
        if not self.dll.SymGetTypeFromName(
            self.process, base, type_name.encode("ascii"), ctypes.byref(type_id)
        ):
            return {}
        count = wintypes.ULONG()
        if not self.dll.SymGetTypeInfo(
            self.process, base, type_id.value, TI_FINDCHILDREN, ctypes.byref(count)
        ):
            return {}
        if count.value == 0 or count.value > 4096:
            return {}
        buf_size = ctypes.sizeof(TI_FINDCHILDREN_PARAMS) + ctypes.sizeof(wintypes.ULONG) * (count.value - 1)
        buf = ctypes.create_string_buffer(buf_size)
        params = ctypes.cast(buf, ctypes.POINTER(TI_FINDCHILDREN_PARAMS)).contents
        params.Count = count.value
        if not self.dll.SymGetTypeInfo(
            self.process, base, type_id.value, TI_FINDCHILDREN, buf
        ):
            return {}
        child_array = ctypes.cast(buf, ctypes.POINTER(wintypes.ULONG * count.value)).contents
        out: dict[str, int] = {}
        for child_id in child_array:
            name_ptr = ctypes.c_void_p()
            if not self.dll.SymGetTypeInfo(
                self.process, base, child_id, TI_GET_SYMNAME, ctypes.byref(name_ptr)
            ):
                continue
            try:
                child_name = ctypes.wstring_at(name_ptr.value)
            finally:
                if hasattr(self.dll, "LocalFree"):
                    self.dll.LocalFree(name_ptr)
            if child_name not in field_names:
                continue
            offset = wintypes.ULONG()
            if self.dll.SymGetTypeInfo(
                self.process, base, child_id, TI_GET_OFFSET, ctypes.byref(offset)
            ):
                out[child_name] = int(offset.value)
        return out

    def close(self) -> None:
        if self.handle:
            self.dll.SymCleanup(self.process)
            self.handle = None

    def unload(self, path: Path) -> None:
        if self.handle and hasattr(self.dll, "SymUnloadModule64"):
            base, _ = self.modules.pop(path.name.lower(), (None, None))
            if base is not None:
                self.dll.SymUnloadModule64(self.process, base)


def download_pdb(identity: dict[str, Any], cache: Path) -> tuple[Path | None, str | None]:
    cv = identity["codeview"]
    if not cv.get("pdb") or not cv.get("guid_age"):
        return None, "no RSDS CodeView record (PDB download key unavailable)"
    url = f"{SYMBOL_PATH}/{cv['pdb']}/{cv['guid_age']}/{cv['pdb']}"
    # This is the layout understood by the srv* symbol path.
    destination = cache / cv["pdb"] / cv["guid_age"] / cv["pdb"]
    try:
        response = requests.get(url, timeout=30)
        response.raise_for_status()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(response.content)
        return destination, None
    except requests.RequestException as exc:
        return None, f"PDB download failed ({url}): {exc}"


def pdb_symbols(pdb_path: Path, pe: pefile.PE, names: tuple[str, ...]) -> tuple[dict[str, dict[str, Any]], str | None]:
    """Parse the public GSYM stream when dbghelp cannot expose a symbol.

    pdbparse is optional: dbghelp remains the primary path. This parser only
    accepts exact, unique public symbols and validates the resulting RVA in the
    PE code section.
    """
    try:
        import pdbparse
        pdb = pdbparse.parse(str(pdb_path), fast_load=True)
        pdb.STREAM_DBI.load()
        pdb._update_names()
        pdb.STREAM_GSYM = pdb.STREAM_GSYM.reload()
        pdb.STREAM_GSYM.load()
        pdb.STREAM_SECT_HDR = pdb.STREAM_SECT_HDR.reload()
        pdb.STREAM_SECT_HDR.load()
        found: dict[str, dict[str, Any]] = {}
        for name in names:
            matches = [s for s in pdb.STREAM_GSYM.globals if getattr(s, "name", None) == name]
            if len(matches) != 1:
                continue
            symbol = matches[0]
            section = pdb.STREAM_SECT_HDR.sections[symbol.segment - 1]
            rva = int(section.VirtualAddress) + int(symbol.offset)
            pe_section = section_for_rva(pe, rva)
            valid = bool(pe_section and pe_section["executable"])
            found[name] = {
                "name": name,
                "rva": rva if valid else None,
                "rva_hex": hex_value(rva) if valid else None,
                "section": pe_section,
                "method": "pdb",
                "evidence": "pdbparse PDBGlobalSymbolStream (exact unique symbol)",
                "confidence": "high" if valid else "UNKNOWN",
                "validation": {"valid": valid, "reason": "RVA is in executable section" if valid else "not executable"},
                "value": rva if valid else None,
            }
        return found, None
    except (ImportError, OSError, ValueError, AttributeError, KeyError, IndexError) as exc:
        return {}, f"PDB parse failed ({pdb_path}): {exc}"


def export_rva(pe: pefile.PE, name: str) -> int | None:
    try:
        for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if symbol.name and symbol.name.decode("ascii", "replace") == name:
                return int(symbol.address)
    except AttributeError:
        pass
    return None


def import_lookup(pe: pefile.PE) -> dict[int, dict[str, Any]]:
    """Return imported IAT slots keyed by image RVA.

    An IAT slot is a data pointer, but it can be the operand of an indirect
    call.  Keep it separate from writable-data candidates: PE section
    characteristics correctly describe .idata as read-only on this build.
    """
    result: dict[int, dict[str, Any]] = {}
    try:
        for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
            module = descriptor.dll.decode("ascii", "replace")
            for entry in descriptor.imports:
                slot_rva = int(entry.address - pe.OPTIONAL_HEADER.ImageBase)
                if entry.name:
                    function = entry.name.decode("ascii", "replace")
                    by = "name"
                else:
                    function = f"ordinal_{int(entry.ordinal)}"
                    by = "ordinal"
                result[slot_rva] = {
                    "module": module,
                    "function": function,
                    "import_target": f"{module}!{function}",
                    "by": by,
                    "ordinal": int(entry.ordinal) if not entry.name else None,
                }
    except AttributeError:
        pass
    return result


def _instruction_text(insn: Any) -> str:
    return f"{insn.mnemonic} {insn.op_str}".strip()


def _rip_references(insn: Any) -> list[tuple[str, int]]:
    """Return (access, target_rva) for RIP-relative memory operands."""
    operands = list(getattr(insn, "operands", ()))
    refs: list[tuple[str, int]] = []
    mnemonic = insn.mnemonic.lower()
    for index, operand in enumerate(operands):
        if operand.type != CS_OP_MEM or operand.mem.base != X86_REG_RIP:
            continue
        target = int(insn.address + insn.size + operand.mem.disp)
        if mnemonic == "lea":
            access = "address"
        elif mnemonic in ("call", "jmp"):
            access = "read (indirect control flow)"
        elif mnemonic in ("mov", "movabs", "movzx", "movsx", "movsxd", "vmovdqa", "vmovdqu"):
            access = "write" if index == 0 else "read"
        elif mnemonic in ("cmp", "test"):
            access = "read"
        elif mnemonic in ("inc", "dec", "neg", "not", "xadd", "xchg", "add", "sub", "adc", "sbb", "and", "or", "xor"):
            access = "read/write" if index == 0 else "read"
        else:
            # Keep unusual RIP-relative operands visible rather than silently
            # treating them as an import or as the requested slot.
            access = "read/write"
        refs.append((access, target))
    return refs


def _function_instructions(pe: pefile.PE, function_rva: int, budget: int = 4096) -> tuple[list[Any], list[str]]:
    """Disassemble a routine's reachable basic blocks through ret instructions."""
    section = section_for_rva(pe, function_rva)
    if not section or not section["executable"]:
        return [], [f"RVA {hex_value(function_rva)} is not executable"]
    section_end = int(section["rva_end"], 16)
    # A malformed function must not make profiling scan an entire image.
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    instructions: list[Any] = []
    errors: list[str] = []
    pending = [function_rva]
    decoded_addresses: set[int] = set()
    while pending and len(instructions) < budget:
        block_start = pending.pop(0)
        if block_start in decoded_addresses:
            continue
        if not (int(section["rva_start"], 16) <= block_start < section_end):
            continue
        blob = pe.get_data(block_start, min(0x10000, max(0, section_end - block_start)))
        for insn in md.disasm(blob, block_start):
            if insn.address in decoded_addresses:
                break
            decoded_addresses.add(insn.address)
            instructions.append(insn)
            if len(instructions) >= budget:
                errors.append(f"instruction budget {budget} reached at {hex_value(insn.address)}")
                break
            mnemonic = insn.mnemonic.lower()
            if mnemonic in ("ret", "retf"):
                break
            if mnemonic == "jmp":
                # Follow direct intra-section jumps, but do not linear-scan
                # beyond an indirect import/tail thunk.
                if insn.operands and insn.operands[0].type == CS_OP_IMM:
                    target = int(insn.operands[0].imm)
                    if section_for_rva(pe, target) and section_for_rva(pe, target)["executable"]:
                        pending.append(target)
                break
            # Conditional branches have a fall-through path and a reachable
            # target. The fall-through is decoded by this block; enqueue the
            # target so a short branch around a call is not missed.
            if mnemonic.startswith("j") and insn.operands and insn.operands[0].type == CS_OP_IMM:
                target = int(insn.operands[0].imm)
                if section_for_rva(pe, target) and section_for_rva(pe, target)["executable"]:
                    pending.append(target)
        if len(instructions) >= budget:
            break
    instructions.sort(key=lambda item: item.address)
    if not instructions:
        errors.append("Capstone decoded no instructions")
    return instructions, errors


def _initial_value(pe: pefile.PE, target_rva: int, target_section: dict[str, Any]) -> dict[str, Any]:
    section = next(
        (s for s in pe.sections if s.Name.rstrip(b"\0").decode("ascii", "replace") == target_section["name"]
         and int(s.VirtualAddress) <= target_rva < int(s.VirtualAddress) + max(int(s.Misc_VirtualSize), int(s.SizeOfRawData))),
        None,
    )
    if section is None:
        return {"kind": "unavailable", "text": "section bytes unavailable"}
    offset = target_rva - int(section.VirtualAddress)
    if offset < 0 or offset + 8 > int(section.SizeOfRawData):
        return {"kind": "BSS", "text": "BSS, valor inicial não presente no arquivo"}
    data = pe.get_data(target_rva, 8)
    if len(data) < 8:
        return {"kind": "BSS", "text": "BSS, valor inicial não presente no arquivo"}
    return {"kind": "file", "bytes": data.hex(" "), "qword_hex": hex(struct.unpack_from("<Q", data)[0])}


def _candidate_semantics(instructions: list[Any], index: int, access: str) -> tuple[int, str]:
    insn = instructions[index]
    score = 0
    reasons: list[str] = []
    if insn.mnemonic.lower() == "call" and "indirect control flow" in access:
        score += 100
        reasons.append("RIP-relative pointer is consumed directly by call")
    elif insn.mnemonic.lower() == "jmp" and "indirect control flow" in access:
        score += 80
        reasons.append("RIP-relative pointer is consumed directly by jmp")
    elif access in ("read", "read/write", "read (indirect control flow)"):
        # A load immediately followed by an indirect call is the common
        # compiler form for a global function-pointer slot.
        loaded_register = None
        if insn.mnemonic.lower() in ("mov", "movabs", "movzx", "movsx", "movsxd") and insn.operands:
            destination = insn.operands[0]
            if destination.type == CS_OP_REG:
                loaded_register = destination.reg
        for later in instructions[index + 1:index + 8]:
            if (loaded_register is not None and later.mnemonic.lower() == "call"
                    and later.operands and later.operands[0].type == CS_OP_REG
                    and later.operands[0].reg == loaded_register):
                score += 40
                reasons.append("loaded pointer is followed by an indirect register call")
                break
    if insn.mnemonic.lower() == "lea":
        reasons.append("LEA address reference; not itself a function-pointer load")
    return score, "; ".join(reasons) or "RIP-relative data reference"


def analyze_export(pe: pefile.PE, module_name: str, name: str, depth_limit: int = 2) -> dict[str, Any]:
    """Analyze an exported routine and direct-call helpers in the same image."""
    result: dict[str, Any] = {
        "module": module_name,
        "export": name,
        "export_rva": None,
        "references": [],
        "candidates": [],
        "helpers": [],
        "errors": [],
    }
    imports = import_lookup(pe)
    function_rva = export_rva(pe, name)
    result["export_rva"] = hex_value(function_rva)
    if function_rva is None:
        result["errors"].append(f"export {name} was not found")
        return result
    queue: list[tuple[int, int, str]] = [(function_rva, 0, "export")]
    seen: set[int] = set()
    all_instructions: list[tuple[Any, str, int]] = []
    while queue:
        routine_rva, depth, origin = queue.pop(0)
        if routine_rva in seen or depth > depth_limit:
            continue
        seen.add(routine_rva)
        instructions, errors = _function_instructions(pe, routine_rva)
        result["errors"].extend(f"{origin}: {error}" for error in errors)
        if origin != "export":
            result["helpers"].append({"rva": hex_value(routine_rva), "depth": depth, "errors": errors})
        for index, insn in enumerate(instructions):
            all_instructions.append((insn, origin, index))
            for access, target in _rip_references(insn):
                target_section = section_for_rva(pe, target)
                reference = {
                    "module": module_name,
                    "routine": origin,
                    "instruction_rva": hex_value(insn.address),
                    "bytes": insn.bytes.hex(" "),
                    "disassembly": _instruction_text(insn),
                    "access": access,
                    "target_rva": hex_value(target),
                    "section": target_section,
                    "instruction_section": section_for_rva(pe, int(insn.address)),
                    "initial_value": _initial_value(pe, target, target_section) if target_section else None,
                }
                import_info = imports.get(target)
                if import_info is not None:
                    reference["import"] = import_info
                    reference["import_target"] = import_info["import_target"]
                score, semantic_reason = _candidate_semantics(instructions, index, access)
                reference["semantic_score"] = score
                reference["semantic_reason"] = semantic_reason
                result["references"].append(reference)
                if target_section and target_section["writable"] and target_section["name"] not in (".idata", ".edata"):
                    candidate = dict(reference)
                    candidate["score"] = score
                    result["candidates"].append(candidate)
            if insn.mnemonic.lower() == "call" and insn.operands and insn.operands[0].type == CS_OP_IMM:
                helper_rva = int(insn.operands[0].imm)
                helper_section = section_for_rva(pe, helper_rva)
                if depth < depth_limit and helper_section and helper_section["executable"]:
                    queue.append((helper_rva, depth + 1, f"helper_{hex_value(helper_rva)}"))

    # Identical references can occur when a helper is reached by more than one
    # call site. Keep every instruction, but select by unique target below.
    by_target: dict[int, list[dict[str, Any]]] = {}
    for candidate in result["candidates"]:
        by_target.setdefault(int(candidate["target_rva"], 16), []).append(candidate)
    target_groups = []
    for target, refs in by_target.items():
        best = max(refs, key=lambda item: item["score"])
        target_groups.append({
            "target_rva": hex_value(target),
            "section": best["section"],
            "references": refs,
            "best_score": best["score"],
            "best_evidence": best,
        })
    result["candidate_targets"] = sorted(target_groups, key=lambda item: (-item["best_score"], item["target_rva"]))
    for group in result["candidate_targets"]:
        group["decision"] = "pending"
        group["decision_reason"] = "candidate requires global uniqueness and function-pointer semantics"
    result["iat_references"] = [
        reference
        for reference in result["references"]
        if reference.get("import") is not None
        and "indirect control flow" in reference.get("access", "")
    ]
    return result


def disassemble_slot(pe_modules: dict[str, tuple[pefile.PE, str]], name: str) -> dict[str, Any]:
    """Find a writable global slot, including direct-call helpers, offline."""
    result: dict[str, Any] = {
        "rva": None,
        "rva_hex": None,
        "module": None,
        "section": None,
        "method": "pattern",
        "confidence": "UNKNOWN",
        "evidence": None,
        "error": None,
        "validation": {"valid": False, "reason": "no unique validated data reference"},
        "analyses": [],
    }
    if Cs is None:
        result["error"] = "capstone is not installed; install with: python -m pip install capstone"
        return result
    for pe, module_name in pe_modules.values():
        analysis = analyze_export(pe, module_name, name)
        result["analyses"].append(analysis)
    groups = [
        group
        for analysis in result["analyses"]
        for group in analysis.get("candidate_targets", [])
    ]
    result["references"] = [
        reference
        for analysis in result["analyses"]
        for reference in analysis.get("references", [])
    ]
    result["iat_references"] = [
        reference
        for analysis in result["analyses"]
        for reference in analysis.get("iat_references", [])
    ]
    result["candidates"] = [
        candidate
        for analysis in result["analyses"]
        for candidate in analysis.get("candidates", [])
    ]
    if len(groups) != 1:
        for analysis in result["analyses"]:
            for group in analysis.get("candidate_targets", []):
                group["decision"] = "rejected"
                group["decision_reason"] = (
                    "multiple writable targets remain; this reference is not sufficient to identify the hook slot"
                )
        result["evidence"] = {
            "decision": "UNKNOWN",
            "reason": "no candidate has unique function-pointer semantics",
            "candidates": [group["best_evidence"] for group in groups],
            "iat_references": result["iat_references"],
        }
        result["error"] = (
            f"expected one unique writable candidate target, found {len(groups)}; "
            "imports/read-only references were discarded"
        )
        return result
    group = groups[0]
    best = group["best_evidence"]
    semantically_justified = int(group["best_score"]) >= 40
    confidence = "high" if semantically_justified else "medium"
    group["decision"] = "accepted"
    group["decision_reason"] = "unique writable target with function-pointer call evidence" if semantically_justified else "unique writable target, but no direct function-pointer call evidence"
    target = int(group["target_rva"], 16)
    result.update(
        rva=target,
        rva_hex=group["target_rva"],
        module=best["module"],
        section=group["section"],
        confidence=confidence,
        validation={
            "valid": bool(group["section"].get("writable") and best.get("section", {}).get("writable")
                           and best.get("section", {}).get("name") not in (".idata", ".edata")
                           and best.get("instruction_section", {}).get("executable")),
            "reason": "unique writable target; referencing instruction is in an executable module section",
            "instruction_section": section_for_rva(
                next(pe for pe, module in pe_modules.values() if module == best["module"]),
                int(best["instruction_rva"], 16),
            ),
        },
        evidence={
            "export": name,
            "module": best["module"],
            "instruction_rva": best["instruction_rva"],
            "bytes": best["bytes"],
            "disassembly": best["disassembly"],
            "target": best["target_rva"],
            "access": best["access"],
            "semantic_reason": best["semantic_reason"],
            "instruction_section": best.get("instruction_section"),
            "initial_value": best["initial_value"],
        },
    )
    return result


def make_item(symbol: dict[str, Any] | None, requested: str) -> dict[str, Any]:
    if symbol is None:
        return {
            "value": None,
            "rva_hex": None,
            "method": "unavailable",
            "confidence": "UNKNOWN",
            "evidence": f"symbol {requested} was not resolved",
            "validation": {"valid": False, "reason": "no symbol or validated pattern"},
        }
    valid = bool(symbol.get("section") and symbol["section"].get("executable"))
    symbol["validation"] = {
        "valid": valid,
        "reason": "RVA is in executable section" if valid else "not executable",
    }
    symbol["value"] = symbol.get("rva") if valid else None
    return symbol


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a local Windows kernel compatibility profile")
    parser.add_argument("--system-dir", type=Path, default=Path(r"C:\Windows\System32"))
    parser.add_argument("--ntoskrnl", type=Path)
    parser.add_argument("--win32kbase", type=Path)
    parser.add_argument("--win32kfull", type=Path)
    parser.add_argument("--win32k", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent / "generated")
    parser.add_argument("--symbol-cache", type=Path, default=Path(tempfile.gettempdir()) / "pubg-symbol-cache")
    parser.add_argument("--no-symbol-server", action="store_true", help="do not initialize dbghelp's Microsoft symbol server")
    return parser.parse_args()


def load_pe(path: Path) -> pefile.PE:
    if not path.is_file():
        raise FileNotFoundError(path)
    return pefile.PE(str(path), fast_load=False)


def emit_header(profile: dict[str, Any], path: Path) -> None:
    nt = profile["modules"].get("ntoskrnl.exe", {})
    wk = profile["modules"].get("win32kbase.sys", {})
    wkfull = profile["modules"].get("win32kfull.sys", {})
    wksys = profile["modules"].get("win32k.sys", {})
    values = profile["values"]

    def identity_line(module: dict[str, Any]) -> str:
        cv = module.get("codeview", {})
        def quoted(value: Any) -> str:
            text = "" if value is None else str(value)
            return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'
        return (
            f"    {{ {quoted(module.get('name'))}, {quoted(module.get('file_version'))}, "
            f"{module.get('file_size', 0)}u, {module.get('time_date_stamp', 0)}u, {module.get('size_of_image', 0)}u, "
            f"{module.get('check_sum', 0)}u, {quoted(cv.get('guid'))}, {cv.get('age', 0) or 0}u }},"
        )

    def item(name: str) -> tuple[int, bool]:
        obj = values.get(name, {})
        value = obj.get("value")
        return int(value or 0), value is not None and obj.get("confidence") != "UNKNOWN"

    alloc, alloc_ok = item("MmAllocateIndependentPages")
    protect, protect_ok = item("MmSetPageProtection")
    free, free_ok = item("MmFreeIndependentPages")
    slot, slot_ok = item("NtUserSetSysColors_global_slot")
    slot_meta = values.get("NtUserSetSysColors_global_slot", {})
    slot_kind = str(slot_meta.get("kind") or "UNKNOWN")
    slot_import_target = str(slot_meta.get("import_target") or "")
    dtb, dtb_ok = item("EPROCESS.DirectoryTableBase")
    user_dtb, user_dtb_ok = item("EPROCESS.UserDirectoryTableBase")
    lines = [
        "// Generated by tools/profiles/extract_profile.py; do not edit.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "#define PROFILE_GENERATED_VERSION 1u",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "typedef struct ModuleIdentity { const char* name; const char* file_version; uint32_t file_size; uint32_t time_date_stamp; uint32_t size_of_image; uint32_t check_sum; const char* pdb_guid; uint32_t pdb_age; } ModuleIdentity;",
        "typedef struct CompatibilityProfile {",
        "    const char* profile_id; ModuleIdentity ntoskrnl; ModuleIdentity win32kbase; ModuleIdentity win32kfull; ModuleIdentity win32k;",
        "    uint32_t mm_allocate_independent_pages_rva; uint8_t has_mm_allocate_independent_pages_rva;",
        "    uint32_t mm_set_page_protection_rva; uint8_t has_mm_set_page_protection_rva;",
        "    uint32_t mm_free_independent_pages_rva; uint8_t has_mm_free_independent_pages_rva;",
        "    uint32_t nt_user_set_sys_colors_global_slot_rva; uint8_t has_nt_user_set_sys_colors_global_slot_rva; const char* nt_user_set_sys_colors_global_slot_kind; const char* nt_user_set_sys_colors_global_slot_import_target;",
        "    uint32_t eprocess_directory_table_base_offset; uint8_t has_eprocess_directory_table_base_offset;",
        "    uint32_t eprocess_user_directory_table_base_offset; uint8_t has_eprocess_user_directory_table_base_offset;",
        "} CompatibilityProfile;",
        "static const CompatibilityProfile generated_profile = {",
        f'    "{profile["profile_id"]}",',
        identity_line(nt),
        identity_line(wk),
        identity_line(wkfull),
        identity_line(wksys),
        f"    0x{alloc:X}u, {1 if alloc_ok else 0},",
        f"    0x{protect:X}u, {1 if protect_ok else 0},",
        f"    0x{free:X}u, {1 if free_ok else 0},",
        f"    0x{slot:X}u, {1 if slot_ok else 0},",
        f'    "{slot_kind}", "{slot_import_target}",',
        f"    0x{dtb:X}u, {1 if dtb_ok else 0},",
        f"    0x{user_dtb:X}u, {1 if user_dtb_ok else 0},",
        "};",
        "#ifdef __cplusplus",
        "} /* extern \"C\" */",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    nt_path = args.ntoskrnl or args.system_dir / "ntoskrnl.exe"
    wk_path = args.win32kbase or args.system_dir / "win32kbase.sys"
    wkfull_path = args.win32kfull or args.system_dir / "win32kfull.sys"
    wksys_path = args.win32k or args.system_dir / "win32k.sys"
    optional_paths = [(wkfull_path, "win32kfull.sys"), (wksys_path, "win32k.sys")]
    try:
        nt_pe = load_pe(nt_path)
        wk_pe = load_pe(wk_path)
    except (OSError, pefile.PEFormatError) as exc:
        print(f"error: cannot read PE input: {exc}", file=sys.stderr)
        return 2

    nt_identity = module_identity(nt_path, nt_pe)
    wk_identity = module_identity(wk_path, wk_pe)
    optional_pes: dict[str, tuple[Path, pefile.PE]] = {}
    for optional_path, module_name in optional_paths:
        if optional_path.is_file():
            try:
                optional_pes[module_name] = (optional_path, load_pe(optional_path))
            except (OSError, pefile.PEFormatError) as exc:
                print(f"warning: cannot read optional PE input {optional_path}: {exc}", file=sys.stderr)
    profile_id = f"ntoskrnl_{nt_identity.get('file_version') or 'unknown'}_{nt_identity['time_date_stamp']:08X}"
    profile: dict[str, Any] = {
        "schema_version": 1,
        "profile_id": profile_id,
        "generated_by": "tools/profiles/extract_profile.py",
        "machine": {"os_version": None, "display_version": None},
        "modules": {"ntoskrnl.exe": nt_identity, "win32kbase.sys": wk_identity},
        "values": {},
        "events": [],
        "errors": [],
    }
    for module_name, (optional_path, optional_pe) in optional_pes.items():
        profile["modules"][module_name] = module_identity(optional_path, optional_pe)

    cache = args.symbol_cache
    cache.mkdir(parents=True, exist_ok=True)
    dbg = DbgHelp(cache)
    try:
        dbg.load(nt_path, nt_pe) if not args.no_symbol_server else None
        dbg.load(wk_path, wk_pe) if not args.no_symbol_server else None
        for optional_path, optional_pe in optional_pes.values():
            dbg.load(optional_path, optional_pe) if not args.no_symbol_server else None
        symbol_names = ("MmAllocateIndependentPages", "MmSetPageProtection", "MmFreeIndependentPages")
        resolved = {name: dbg.symbol("ntoskrnl.exe", name) for name in symbol_names}
        downloaded_pdb: Path | None = None
        # SymLoadModuleEx is lazy. If the first lookup did not cause dbghelp to
        # obtain the PDB, explicitly download the exact CodeView identity and
        # retry from the standard srv cache layout.
        if not args.no_symbol_server and not any(resolved.values()):
            pdb_path, pdb_error = download_pdb(nt_identity, cache)
            if pdb_error:
                profile["errors"].append(pdb_error)
            else:
                downloaded_pdb = pdb_path
                profile["events"].append(f"PDB fallback downloaded: {pdb_path}")
                dbg.unload(nt_path)
                dbg.load(nt_path, nt_pe)
                resolved = {name: dbg.symbol("ntoskrnl.exe", name) for name in symbol_names}
        if downloaded_pdb and not all(resolved.values()):
            parsed, parse_error = pdb_symbols(downloaded_pdb, nt_pe, symbol_names)
            if parse_error:
                profile["errors"].append(parse_error)
            for name in symbol_names:
                if resolved[name] is None and name in parsed:
                    resolved[name] = parsed[name]
        for name in symbol_names:
            profile["values"][name] = make_item(resolved[name], name)

        type_offsets = dbg.type_field_offset(
            "ntoskrnl.exe", "_EPROCESS", {"DirectoryTableBase", "UserDirectoryTableBase"}
        )
        for field in ("DirectoryTableBase", "UserDirectoryTableBase"):
            offset = type_offsets.get(field)
            profile["values"][f"EPROCESS.{field}"] = {
                "value": offset,
                "offset_hex": hex_value(offset),
                "method": "dbghelp SymGetTypeInfo" if offset is not None else "unavailable",
                "confidence": "high" if offset is not None else "UNKNOWN",
                "evidence": "_EPROCESS child field" if offset is not None else "SymGetTypeFromName/SymGetTypeInfo did not expose field",
                "validation": {"valid": offset is not None, "reason": "type information" if offset is not None else "not extracted"},
            }

        slot_modules: dict[str, tuple[pefile.PE, str]] = {"win32kbase.sys": (wk_pe, "win32kbase.sys")}
        for module_name, (_optional_path, optional_pe) in optional_pes.items():
            slot_modules[module_name] = (optional_pe, module_name)
        slot = disassemble_slot(slot_modules, "NtUserSetSysColors")
        slot_symbol = dbg.symbol("win32kbase.sys", "NtUserSetSysColors")
        slot["symbol_attempt"] = (
            slot_symbol
            if slot_symbol
            else "SymFromName(win32kbase.sys!NtUserSetSysColors) did not expose a usable global slot"
        )
        profile["values"]["NtUserSetSysColors_global_slot"] = slot
        profile["errors"].extend(dbg.errors)
    finally:
        dbg.close()

    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows NT\CurrentVersion") as key:
            profile["machine"]["os_version"] = winreg.QueryValueEx(key, "CurrentBuildNumber")[0]
            profile["machine"]["display_version"] = winreg.QueryValueEx(key, "DisplayVersion")[0]
    except (OSError, ImportError) as exc:
        profile["errors"].append(f"OS identity unavailable: {exc}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.output_dir / f"{profile_id}.json"
    # Offline runs must not erase previously validated values merely because
    # the optional symbol server was disabled. Reuse an older value only when
    # the current item is unresolved and the profile identity is unchanged.
    try:
        if json_path.is_file():
            previous = json.loads(json_path.read_text(encoding="utf-8"))
            if previous.get("profile_id") == profile_id:
                for key, previous_item in previous.get("values", {}).items():
                    current_item = profile["values"].get(key, {})
                    if (current_item.get("confidence") == "UNKNOWN"
                            and previous_item.get("confidence") != "UNKNOWN"):
                        profile["values"][key] = previous_item
                        profile["events"].append(f"Preserved validated value offline: {key}")
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        profile["errors"].append(f"previous profile unavailable: {exc}")
    json_path.write_text(json.dumps(profile, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    emit_header(profile, args.output_dir / "profiles_generated.h")
    print(f"profile_id: {profile_id}")
    print(f"JSON: {json_path}")
    print(f"header: {args.output_dir / 'profiles_generated.h'}")
    for key, item in profile["values"].items():
        print(f"{key}: {item.get('rva_hex', item.get('offset_hex')) or 'UNKNOWN'} [{item.get('method')}] {item.get('confidence')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
