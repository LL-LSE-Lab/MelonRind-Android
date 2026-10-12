"""Small, reproducible ELF/ARM64 inspector; requires capstone for disassembly."""
import argparse
import struct
from pathlib import Path


class Elf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        assert self.data[:6] == b"\x7fELF\x02\x01", "requires little-endian ELF64"
        offset = struct.unpack_from("<Q", self.data, 40)[0]
        count, names_index = struct.unpack_from("<HH", self.data, 60)
        sections = [struct.unpack_from("<IIQQQQIIQQ", self.data, offset + i * 64) for i in range(count)]
        names = sections[names_index]
        strings = self.data[names[4]:names[4] + names[5]]
        self.sections = {}
        for section in sections:
            name = strings[section[0]:].split(b"\0", 1)[0].decode()
            self.sections[name] = section
        rela = self.sections[".rela.dyn"]
        self.relocs = {}
        for address, info, addend in struct.iter_unpack("<QQq", self.data[rela[4]:rela[4] + rela[5]]):
            if (info & 0xffffffff) == 1027:
                self.relocs[address] = addend

    def file_offset(self, address):
        for section in self.sections.values():
            if section[3] <= address < section[3] + section[5] and section[1] != 8:
                return section[4] + address - section[3]
        raise ValueError(hex(address))

    def pointer(self, address):
        return self.relocs.get(address, struct.unpack_from("<Q", self.data, self.file_offset(address))[0])

    def rtti(self, name):
        encoded = name.encode() + b"\0"
        pos = self.data.find(encoded)
        refs = [address for address, target in self.relocs.items() if target == pos]
        print("name", name, hex(pos), "refs", [hex(x) for x in refs])
        for ref in refs:
            typeinfo = ref - 8
            vtables = [address + 8 for address, target in self.relocs.items() if target == typeinfo and self.pointer(address - 8) == 0]
            print("typeinfo", hex(typeinfo), "vtables", [hex(x) for x in vtables])
            for vtable in vtables:
                print("vtable", hex(vtable))
                for i in range(36):
                    target = self.pointer(vtable + 8 * i)
                    if not 0x6400000 <= target <= 0x129c0000:
                        break
                    print(i, hex(target))

    def disasm(self, start, length):
        import capstone
        decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        offset = self.file_offset(start)
        for ins in decoder.disasm(self.data[offset:offset + length], start):
            print(f"{ins.address:08x}: {ins.mnemonic:9} {ins.op_str}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--rtti", action="append")
    parser.add_argument("--disasm", type=lambda x: int(x, 0))
    parser.add_argument("--length", type=lambda x: int(x, 0), default=256)
    args = parser.parse_args()
    elf = Elf(args.binary)
    if args.rtti:
        for name in args.rtti:
            elf.rtti(name)
    if args.disasm:
        elf.disasm(args.disasm, args.length)
