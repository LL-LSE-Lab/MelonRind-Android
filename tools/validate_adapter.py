"""Validate the checked-in 1.26.51.1 RTTI/vtable ABI contract."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from inspect_binary import Elf


def rtti_vtables(elf: Elf, name: str) -> list[int]:
    encoded = name.encode() + b"\0"
    pos = elf.data.find(encoded)
    if pos < 0:
        return []
    refs = [address for address, target in elf.relocs.items() if target == pos]
    result: list[int] = []
    for ref in refs:
        typeinfo = ref - 8
        for address, target in elf.relocs.items():
            if target != typeinfo or address < 8:
                continue
            if elf.pointer(address - 8) == 0:
                result.append(address + 8)
    return sorted(set(result))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    digest = hashlib.sha256(args.binary.read_bytes()).hexdigest().upper()
    if digest != args.sha256.upper():
        raise SystemExit(f"library SHA256 mismatch: {digest}")
    elf = Elf(args.binary)
    checks = {
        "hunger": ("17HudHungerRenderer", 17),
        "heart": ("16HudHeartRenderer", 17),
        "network": ("20ClientNetworkHandler", 144),
        "inventory": ("9Inventory", 7),
        "food": ("17FoodItemComponent", 2),
    }
    report: dict[str, object] = {"sha256": digest, "checks": {}}
    for key, (name, slot) in checks.items():
        tables = rtti_vtables(elf, name)
        if len(tables) != 1:
            raise SystemExit(f"{name}: expected one vtable, found {tables}")
        target = elf.pointer(tables[0] + slot * 8)
        if not (0x6400000 <= target <= 0x129C0000):
            raise SystemExit(f"{name}[{slot}] is not executable: {target:#x}")
        report["checks"][key] = {"vtable": hex(tables[0]), "slot": slot,
                                  "target": hex(target)}
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
