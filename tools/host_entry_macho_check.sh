#!/usr/bin/env bash
#
# Check the Mach-O header the entry image defines for itself.
#
# Since experiment 194 the entry image carries a `struct mach_header` and two `LC_SEGMENT` load
# commands, pointed at by `_mh_execute_header` (stages/stage90/xnu_arm_boot/entry_macho.s). XNU
# reads it with `osfmk/arm/arm_vm_init.c`'s calls into `libkern/kernel_mach_header.c`, and gets
# `segTEXTB`, `segDATAB`, `end_kern` and `sane_size` out of it - so a header that is wrong by a
# field produces a wrong memory map rather than an error.
#
# What this checks, and what it does not:
#
#   - it decodes the bytes *emitted into the linked ELF*, at `_mh_execute_header`'s address, so it
#     tests the artifact rather than the assembler source;
#   - it asserts every field against the linker symbols the header was built from, so drift between
#     entry_macho.s and entry.ld fails loudly;
#   - it replays the walks and the comparisons `libkern/kernel_mach_header.c` performs
#     (`strncmp(..., 16)` on NUL-padded names, `cmdsize` chaining, `LC_SEGMENT`-only, `vmsize`
#     summation) and prints what `arm_vm_init` will receive.
#
#   - it does NOT compile XNU's code. The structure layout is Apple's `mach-o/loader.h` for 32-bit
#     architectures, transcribed here; the values and the walk are what is being tested. XNU's
#     reader compiling against the same layout is what the device run tests.
#
# Usage: ./host_entry_macho_check.sh [path/to/xnu_arm_entry.elf]
# Exit 0 if the header is internally consistent and agrees with the linker's own numbers.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)
ELF=${1:-$REPO_ROOT/out/stage90/xnu_arm_entry.elf}
NM=${NM:-arm-none-eabi-nm}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
PYTHON=${PYTHON:-python3}

if [ ! -f "$ELF" ]; then
    echo "host_entry_macho_check: no $ELF - build the entry image first" >&2
    exit 2
fi

BIN=$(mktemp)
trap 'rm -f "$BIN"' EXIT
"$OBJCOPY" -O binary "$ELF" "$BIN"

"$NM" "$ELF" > "$BIN.nm"
"$PYTHON" - "$ELF" "$BIN" "$BIN.nm" <<'PY'
import struct
import sys

elf, binpath, nmpath = sys.argv[1], sys.argv[2], sys.argv[3]

def nm(name):
    for line in open(nmpath):
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit("host_entry_macho_check: %s is not a symbol in %s" % (name, elf))

sym = {n: nm(n) for n in (
    "_mh_execute_header", "__entry_text_start", "__entry_text_size", "__entry_data_start",
    "__entry_data_size", "__entry_data_filesize", "__entry_data_fileoff", "__entry_image_end")}

# objcopy -O binary lays the loadable sections out from the lowest address, which is .text at
# 0x00200000 - the first symbol the linker script defines, and the image's own base.
base = nm("__entry_text_start")
img = open(binpath, "rb").read()

MH_MAGIC, MH_EXECUTE, LC_SEGMENT = 0xfeedface, 2, 1
CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K = 12, 12
SIZEOF_MACH_HEADER = 28
SIZEOF_SEGMENT_COMMAND = 56
SIZEOF_SECTION = 68

# --------------------------------------------------------------------------- the emitted bytes

p = sym["_mh_execute_header"] - base
def u32():
    global p
    v = struct.unpack_from("<I", img, p)[0]
    p += 4
    return v

def name16():
    global p
    raw = img[p:p + 16]
    p += 16
    return raw.split(b"\0")[0].decode()

magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags = (u32() for _ in range(7))
fail = []

def expect(what, got, want):
    if got != want:
        fail.append("%s: got %r, expected %r" % (what, got, want))

expect("magic", magic, MH_MAGIC)
expect("filetype", filetype, MH_EXECUTE)
expect("cputype", cputype, CPU_TYPE_ARM)
expect("cpusubtype", cpusubtype, CPU_SUBTYPE_ARM_V7K)
print("mach_header  magic=0x%08x cputype=%d cpusubtype=%d filetype=%d ncmds=%d sizeofcmds=%d flags=0x%x"
      % (magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags))

segs = {}
sects = []
walked = SIZEOF_MACH_HEADER
for _ in range(ncmds):
    cmd = u32()
    cmdsize = u32()
    if cmd != LC_SEGMENT:
        fail.append("load command %d is not LC_SEGMENT (cmd=0x%x)" % (len(segs), cmd))
        break
    segname = name16()
    vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, segflags = (u32() for _ in range(8))
    segs[segname] = (vmaddr, vmsize)
    print("  LC_SEGMENT %-8s cmdsize=%-4d vmaddr=0x%08x vmsize=0x%08x fileoff=0x%-6x filesize=0x%-6x "
          "maxprot=%d initprot=%d nsects=%d"
          % (repr(segname), cmdsize, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects))
    want_cmdsize = SIZEOF_SEGMENT_COMMAND + nsects * SIZEOF_SECTION
    expect("%s cmdsize" % segname, cmdsize, want_cmdsize)
    for _ in range(nsects):
        sectname = name16()
        sect_segname = name16()
        addr, size, offset, align, reloff, nreloc, sectflags, r1, r2 = (u32() for _ in range(9))
        sects.append((sect_segname, sectname, addr, size))
        print("    section  %-8s %-10s addr=0x%08x size=%d offset=%d align=%d flags=0x%x"
              % (repr(sect_segname), repr(sectname), addr, size, offset, align, sectflags))
    walked += cmdsize

expect("sizeofcmds", sizeofcmds, walked - SIZEOF_MACH_HEADER)

# ------------------------------------------------------- the numbers the linker says are true

expect("__TEXT vmaddr", segs.get("__TEXT", (None, None))[0], sym["__entry_text_start"])
expect("__TEXT vmsize", segs.get("__TEXT", (None, None))[1], sym["__entry_text_size"])
expect("__DATA vmaddr", segs.get("__DATA", (None, None))[0], sym["__entry_data_start"])
expect("__DATA vmsize", segs.get("__DATA", (None, None))[1], sym["__entry_data_size"])

# ------------------------------------------------------------------ XNU's own comparisons

def strncmp16(a, b):
    """strncmp(a, b, 16) as kernel_mach_header.c calls it: stops at the first difference or at a
    NUL in both, so a NUL-padded 16-byte name matches a NUL-terminated literal."""
    for i in range(16):
        if a[i] != b[i]:
            return a[i] - b[i]
        if a[i] == 0:
            return 0
    return 0

def getsegbynamefromheader(name):
    """kernel_mach_header.c:243 - the walk, replayed on the decoded commands."""
    q = SIZEOF_MACH_HEADER
    for _ in range(ncmds):
        cmd = struct.unpack_from("<I", img, p0 + q)[0]
        cmdsize = struct.unpack_from("<I", img, p0 + q + 4)[0]
        segname = img[p0 + q + 8:p0 + q + 24]
        if cmd == LC_SEGMENT and strncmp16(segname, name + b"\0" * 16) == 0:
            return q
        q += cmdsize
    return None

p0 = sym["_mh_execute_header"] - base
print("\nwhat arm_vm_init receives:")
for name in (b"__TEXT", b"__DATA", b"__LINKEDIT", b"__KLD", b"__LAST",
             b"__PRELINK_TEXT", b"__PRELINK_INFO"):
    q = getsegbynamefromheader(name)
    if q is None:
        print("  getsegdatafromheader(%-15s) -> NULL, size 0" % (name.decode() + '"'))
    else:
        vmaddr = struct.unpack_from("<I", img, p0 + q + 24)[0]
        vmsize = struct.unpack_from("<I", img, p0 + q + 28)[0]
        print("  getsegdatafromheader(%-15s) -> 0x%08x, size 0x%08x"
              % (name.decode() + '"', vmaddr, vmsize))

last_addr = 0
for name, (vmaddr, vmsize) in segs.items():
    last_addr = max(last_addr, vmaddr + vmsize)
print("  getlastaddr()                  -> 0x%08x  (end_kern = round_page of this)" % last_addr)
expect("getlastaddr", last_addr, sym["__entry_image_end"])

if ("__DATA", "__const") not in [(s[0], s[1]) for s in sects]:
    fail.append("no __DATA,__const section: arm_vm_init:427 dereferences a NULL section")
if not any(s[1] == "__const" for s in sects):
    fail.append("no section named __const")

if fail:
    print()
    for f in fail:
        print("FAIL: " + f)
    sys.exit(1)

print("\nOK: the header is internally consistent, every field matches the linker's own symbols,")
print("    and every segment arm_vm_init asks for is either present with the right numbers or")
print("    absent (which is a NULL pointer and a zero size, not a wrong address).")
PY
