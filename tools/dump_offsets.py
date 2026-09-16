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
        name = validate_name(raw_name)
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
    entries = "".join(f"    inline constexpr std::uint64_t {name} = 0x{value:X};\n" for name, value in offsets)
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
