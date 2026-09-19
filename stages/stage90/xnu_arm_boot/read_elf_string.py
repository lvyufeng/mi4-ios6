#!/usr/bin/env python3
"""Read a string (or a 32-bit pointer) out of a linked ELF by virtual address.

The tool exists because `objdump -s` prints only the sections it chooses and prints
the same address space twice over (hex rows plus an ASCII column), which is how a
"<unmapped>" reading got invented once already. This walks the program headers,
builds the real VMA->file-offset map, and reads through it.

  read_elf_string.py <elf> str <addr> [maxlen]
  read_elf_string.py <elf> ptr <addr> [maxlen]     # read a pointer, then the string it points at
  read_elf_string.py <elf> ptrs <addr> <count>     # read `count` pointers
"""
import struct
import subprocess
import sys


def load(elf):
    ph = subprocess.run(["arm-none-eabi-readelf", "-lW", elf], capture_output=True, text=True).stdout
    segs = []
    for line in ph.splitlines():
        m = line.split()
        if len(m) >= 6 and m[0] == "LOAD":
            off, vaddr, filesz = int(m[1], 16), int(m[2], 16), int(m[4], 16)
            segs.append((vaddr, vaddr + filesz, off, filesz))
    data = open(elf, "rb").read()
    return segs, data


def rd(segs, data, addr, n):
    for va, vend, off, filesz in segs:
        if va <= addr and addr + n <= vend and addr + n <= va + filesz:
            o = off + (addr - va)
            return data[o:o + n]
    return None


def cstr(segs, data, addr, maxlen=400):
    out = b""
    for k in range(maxlen):
        b = rd(segs, data, addr + k, 1)
        if b is None:
            return out.decode("latin1") + "<unmapped@+%d>" % k
        if b[0] == 0:
            return out.decode("latin1")
        out += b
    return out.decode("latin1") + "<no NUL>"


def main():
    elf = sys.argv[1]
    segs, data = load(elf)
    mode = sys.argv[2]
    addr = int(sys.argv[3], 16)
    if mode == "str":
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 400
        print(hex(addr), repr(cstr(segs, data, addr, n)))
    elif mode == "ptr":
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 400
        raw = rd(segs, data, addr, 4)
        if raw is None:
            print(hex(addr), "<unmapped>")
            return
        p = struct.unpack("<I", raw)[0]
        print(hex(addr), "->", hex(p), repr(cstr(segs, data, p, n)))
    elif mode == "ptrs":
        cnt = int(sys.argv[4])
        for i in range(cnt):
            raw = rd(segs, data, addr + 4 * i, 4)
            p = struct.unpack("<I", raw)[0] if raw else 0
            print("[%d] %s" % (i, hex(p)), repr(cstr(segs, data, p, 200)) if p else "")
    else:
        sys.exit("mode must be str|ptr|ptrs")


if __name__ == "__main__":
    main()
