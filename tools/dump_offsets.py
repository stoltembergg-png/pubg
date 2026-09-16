#!/usr/bin/env python3
"""Generate a C++ offsets header from a C++ dump or a JSON mapping."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable


HEADER = """// gerado automaticamente - não editar manualmente
// Source: {source}
#pragma once

#include <cstdint>

namespace OFFSET {{
{entries}}}  // namespace OFFSET
"""

CPP_ENTRY = re.compile(
    r"^(?:(?:inline|static)\s+)?(?:constexpr|const)\s+"
    r"(?:std::)?(?:u?int(?:8|16|32|64)_t|unsigned\s+\w+|size_t)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*=\s*(?P<value>[^;]+);\s*$"
)
IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")
INTEGER = re.compile(r"^(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*$")

# Names used by the SDK dump that are kept under the project's historical
# names in the canonical header.  This only changes the generated API names;
# values are taken from the input dump or the verified SDK overrides below.
NAME_ALIASES = {
    "XenuineDecrypt": "Decrypt",
    "GNamesPtr": "GNames_offset",
    "ChunkSize": "ElementsPerChunk",
    "LocalPlayer": "LocalPlayers",
    "ObjID": "ObjectID",
    "DecryptNameIndexRor": "NameIsROR",
    "DecryptNameIndexXorKey1": "NameIndexXor1",
    "DecryptNameIndexXorKey2": "NameIndexXor2",
    "DecryptNameIndexSval": "NameIndexOne",
    "DecryptNameIndexDval": "NameIndexTwo",
    "Health_keys0": "HealthXorKey0",
    "Health_keys1": "HealthXorKey1",
    "Health_keys2": "HealthXorKey2",
    "Health_keys3": "HealthXorKey3",
    "Health_keys4": "HealthXorKey4",
    "Health_keys5": "HealthXorKey5",
    "Health_keys6": "HealthXorKey6",
    "Health_keys7": "HealthXorKey7",
    "Health_keys8": "HealthXorKey8",
    "Health_keys9": "HealthXorKey9",
    "Health_keys10": "HealthXorKey10",
    "Health_keys11": "HealthXorKey11",
    "Health_keys12": "HealthXorKey12",
    "Health_keys13": "HealthXorKey13",
    "Health_keys14": "HealthXorKey14",
    "Health_keys15": "HealthXorKey15",
    "DroppedItemGroupUItem": "DroppedItemGroup_UItem",
}

COMPATIBILITY_ALIASES = {
    "Offset": "ObjectID",
    "LastRenderTimeOnScreen": "Eyes",
    "Health": "Health1",
    "CameraFov": "CameraCacheFOV",
    "CameraRot": "CameraCacheRotation",
    "CameraPos": "CameraCacheLocation",
}

# Offsets confirmed against the local SDK/DUMP for build 2609.1.1.93.  The
# input dump is intentionally kept as the source of legacy/non-reflected
# values, while these entries prevent known reflected fields from retaining
# stale offsets from an older layout.
SDK_VERIFIED_OVERRIDES = {
    "CurrentLevel": 0x410,
    "ObjectID": 0x18,  # UObject::InternalIndex_enc
    "AcknowledgedPawn": 0x4C8,
    "PlayerCameraManager": 0x4F0,
    "ViewTarget": 0x1690,
    "CameraCacheFOV": 0x478,       # APlayerCameraManager(0x440 CacheEntry)+0x10 POV +0x28 FOV
    "CameraCacheRotation": 0x46C,  # APlayerCameraManager::POV.Rotation
    "CameraCacheLocation": 0x47C,  # APlayerCameraManager::POV.Location
    "PlayerArray": 0x430,
    "PlayerState": 0x448,
    "MyHUD": 0x4E8,
    "PlayerInput": 0x568,
    "InputYawScale": 0x66C,
    "bShowMouseCursor": 0x678,
    "ComponentLocation": 0x214,  # USceneComponent::RelativeLocation
    "ComponentToWorld": 0x260,
    "CharacterMovement": 0x628,
    "CharacterState": 0x10CC,
    "CharacterName": 0x2B98,
    "LastTeamNum": 0x1370,
    "WeaponProcessor": 0xA10,
    "Mesh": 0x640,
    "RootComponent": 0x2E8,
    "ReplicatedMovement": 0x78,
    "VehicleRiderComponent": 0x2130,
    "TimeTillExplosion": 0x844,
    "bIsScoping_CP": 0x865,
    "bIsReloading_CP": 0x75D,
    "ControlRotation_CP": 0x674,
    "LeanLeftAlpha_CP": 0x6BC,
    "LeanRightAlpha_CP": 0x6C0,
    "bIsFreefalling_CP": 0x8EE,
    "bIsParachuting_CP": 0x8EF,
    "bEmergencyPickup_Flying_CP": 0x8F0,
    "bIsReviving_CP": 0x8F3,
    "bIsSwimming_CP": 0x8F6,
}

# These names are consumed by legacy code but have no matching reflected
# field in the supplied SDK.  Keep the value for compatibility and make the
# uncertainty explicit instead of silently presenting it as SDK-verified.
SDK_NOT_FOUND = {
    "Actors",
    "GameInstance",
    "LocalPlayers",
    "PlayerController",
    "TeamNumber",
    "Eyes",
    "WorldToMap",
    "PlayerStatistics",
    "Health1",
    "Health2",
    "Health3",
    "Health4",
    "Health5",
    "Health6",
    *(f"HealthXorKey{i}" for i in range(16)),
}

# Reflected fields which are used by the reader but were not present in the
# legacy offset dump.  They are emitted in the same canonical namespace so
# generation remains deterministic without modifying the local dump file.
SDK_EXTRA_OFFSETS = {
    "ComponentVelocity": 0x378,
}


class OffsetInputError(ValueError):
    """Raised when an input dump does not contain valid offsets."""


def parse_integer(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise OffsetInputError(f"{name}: offset must be an integer")

    text = str(value).strip()
    if not INTEGER.fullmatch(text):
        raise OffsetInputError(f"{name}: invalid integer value {text!r}")

    normalized = re.sub(r"[uUlL]+$", "", text)
    number = int(normalized, 16 if normalized.lower().startswith("0x") else 10)
    if number < 0:
        raise OffsetInputError(f"{name}: offset cannot be negative")
    if number > 0xFFFFFFFFFFFFFFFF:
        raise OffsetInputError(f"{name}: offset does not fit in uint64_t")
    return number


def validate_name(name: Any) -> str:
    if not isinstance(name, str) or not IDENTIFIER.fullmatch(name):
        raise OffsetInputError(f"invalid offset name: {name!r}")
    return name


def make_offsets(items: Iterable[tuple[Any, Any]]) -> list[tuple[str, int]]:
    offsets: list[tuple[str, int]] = []
    seen: set[str] = set()
    for raw_name, raw_value in items:
        name = validate_name(NAME_ALIASES.get(raw_name, raw_name))
        if name in seen:
            raise OffsetInputError(f"duplicate offset name: {name}")
        seen.add(name)
        offsets.append((name, parse_integer(raw_value, name)))

    if not offsets:
        raise OffsetInputError("input does not contain any offsets")
    return offsets


def parse_cpp(text: str) -> list[tuple[str, int]]:
    items: list[tuple[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        content = line.strip()
        if not content or content.startswith("//"):
            continue
        match = CPP_ENTRY.fullmatch(content)
        if not match:
            raise OffsetInputError(f"line {line_number}: expected a constexpr offset declaration")
        items.append((match.group("name"), match.group("value").strip()))
    return make_offsets(items)


def parse_json(text: str) -> list[tuple[str, int]]:
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise OffsetInputError(f"invalid JSON: {exc}") from exc

    if isinstance(data, dict) and "offsets" in data:
        data = data["offsets"]

    if isinstance(data, dict):
        return make_offsets(data.items())
    if isinstance(data, list):
        items: list[tuple[Any, Any]] = []
        for entry in data:
            if not isinstance(entry, dict) or "name" not in entry or "value" not in entry:
                raise OffsetInputError("JSON list entries must contain name and value")
            items.append((entry["name"], entry["value"]))
        return make_offsets(items)
    raise OffsetInputError("JSON root must be an object or an array")


def load_offsets(path: Path) -> list[tuple[str, int]]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise OffsetInputError(f"cannot read input {path}: {exc}") from exc

    if path.suffix.lower() == ".json":
        return parse_json(text)
    return parse_cpp(text)


def render(offsets: Iterable[tuple[str, int]], source: Path) -> str:
    offset_list = list(offsets)
    rendered_names = {name for name, _ in offset_list}
    entries = "".join(
        f"    inline constexpr std::uint64_t {name} = 0x{SDK_VERIFIED_OVERRIDES.get(name, value):X};"
        f"{' // TODO: not found in SDK 2609.1.1.93 - verify with runtime' if name in SDK_NOT_FOUND else ''}\n"
        for name, value in offset_list
    )
    entries += "".join(
        f"    inline constexpr std::uint64_t {name} = 0x{value:X};\n"
        for name, value in SDK_EXTRA_OFFSETS.items()
        if name not in rendered_names
    )
    entries += "".join(
        f"    inline constexpr const std::uint64_t& {name} = {target};"
        f"{' // TODO: not found in SDK 2609.1.1.93 - verify with runtime' if target in SDK_NOT_FOUND else ''}\n"
        for name, target in COMPATIBILITY_ALIASES.items()
    )
    return HEADER.format(source=source.name, entries=entries)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate a C++ OFFSET namespace from an offset dump.")
    parser.add_argument("--input", required=True, type=Path, help="C++ constexpr dump or JSON file")
    parser.add_argument("--output", required=True, type=Path, help="generated C++ header")
    parser.add_argument("--check", action="store_true", help="fail when output is not up to date; do not write")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        offsets = load_offsets(args.input)
        generated = render(offsets, args.input)
    except OffsetInputError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.check:
        try:
            existing = args.output.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"error: cannot read output {args.output}: {exc}", file=sys.stderr)
            return 1
        if existing != generated:
            print(f"outdated: {args.output}", file=sys.stderr)
            return 1
        print(f"up to date: {args.output}")
        return 0

    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated, encoding="utf-8", newline="\n")
    except OSError as exc:
        print(f"error: cannot write output {args.output}: {exc}", file=sys.stderr)
        return 1
    print(f"generated {args.output} ({len(offsets)} offsets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
