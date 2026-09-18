# Experiment 194 — The image describes itself in a Mach-O header, `arm_vm_init` ran its first half, and the frontier is `vm_set_page_size`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_set_page_size

No errors detected
```

and `stage90_xnu_entry_stub_no_exception=0x00000001` earlier in the log — the run did not fault on
the way. The stop is exactly the one predicted before the run: `arm_vm_init`'s segment lookups, its
`__DATA,__const` handling and the long block of `vm_*` stores between them all ran, and the first
thing after them that nothing defines is `vm_set_page_size`.

## Why an ELF had to grow a Mach-O header

Experiment 193's frontier was `arm_vm_init`, and the object is `osfmk/arm/arm_vm_init.o` — but
linking it alone would not have produced a stop. Its first real call is

```c
	segTEXTB = (vm_offset_t) getsegdatafromheader(&_mh_execute_header, "__TEXT", &segSizeTEXT);
```

and from there `end_kern`, `sane_size`, `avail_start` and the `vm_kernel_*` globals are all derived
from the answers. Those readers are `libkern/kernel_mach_header.c`, and the header they walk is
`_mh_execute_header` — which in this image was a generated storage stub, a pointer to zeroed bytes.

That is not a slow path, it is a crash, and the disassembly says exactly where:

```
    1988:	bl	getsectbynamefromheader
    1990:	mov	r5, r0
    1998:	ldr	r6, [r0, #32]      ; sectDCONST->addr, with sectDCONST = NULL
    19ac:	ldr	r0, [r0, #36]      ; sectDCONST->size
```

`getsectbynamefromheader` returns NULL for a header whose `ncmds` is 0, and `arm_vm_init:427` is
`sectCONSTB = sectDCONST->addr` with no check — a load from address 0x20, outside XNU's page tables.
So the step is three things that only work together: the object, the object that reads the header,
and the header.

## The header

`stages/stage90/xnu_arm_boot/entry_macho.s` defines `_mh_execute_header` as a real `struct
mach_header` with two `LC_SEGMENT` commands. Every number in it comes from `entry.ld`:

```
mach_header  magic=0xfeedface cputype=12 cpusubtype=12 filetype=2 ncmds=2 sizeofcmds=180 flags=0
  LC_SEGMENT '__TEXT' cmdsize=56   vmaddr=0x00200000 vmsize=0x00023f60 fileoff=0x0     filesize=0x23f60  maxprot=7 initprot=5 nsects=0
  LC_SEGMENT '__DATA' cmdsize=124  vmaddr=0x00223f60 vmsize=0x00015fa8 fileoff=0x23f60 filesize=0x10628 maxprot=7 initprot=3 nsects=1
    section  '__DATA' '__const'  addr=0x00239f08 size=0 offset=0 align=0 flags=0x0
```

Three decisions in it are worth stating rather than leaving in the source.

**Two segments, because the image has two.** `__TEXT` is `.text`'s real extent — and the Mach-O
header itself lives inside it, where a real kernel's header lives — and `__DATA` is the real
writable region, `.data` through the end of `.bss`, with `vmsize` covering the zero-filled part and
`filesize` stopping where it begins. Those are the values, not approximations of them: the linker
script exports the addresses and the sizes, and the sizes are computed *in the script* rather than
in the assembly, because gas cannot fold the difference of two linker symbols into one word and a
script expression can.

**The six segments this image does not have are absent rather than zero-length.** `__LINKEDIT`,
`__KLD`, `__LAST`, `__PRELINK_TEXT` and `__PRELINK_INFO` are all missing, so
`getsegdatafromheader` returns a null pointer and writes a zero size, and `arm_vm_init` stores those
zeroes into `vm_slinkedit`, `vm_kext_base`, `vm_kext_top` and the rest. A real kernel has them and
this one does not; that is the truth about the image rather than a gap to paper over with
fake addresses.

**`__DATA` carries one section and it is empty.** `getsectbynamefromheader(&_mh_execute_header,
"__DATA", "__const")` must not return NULL — that is the `ldr r6, [r0, #32]` above — so the section
exists, with `addr` at the end of `.bss` and `size` 0. This image's read-only data is in `.text`, so
there is no `__DATA,__const` region to protect, and the zero size then propagates: the block's own
sanity check (`sectSizeCONST == 0` → `doconstro = FALSE`) makes `arm_vm_prot_init` map `__DATA` as a
single blob. A fabricated non-empty section would claim read-only data that does not exist and would
have had `arm_vm_page_granular_RNX` protect a range that is really writable.

The header is checked on the host before the device ever sees it, by
`tools/host_entry_macho_check.sh`, which decodes the bytes *emitted into the linked ELF* at
`_mh_execute_header`'s address and asserts every field against the linker's own symbols:

```
  getsegdatafromheader(__TEXT")          -> 0x00200000, size 0x00023f60
  getsegdatafromheader(__DATA")          -> 0x00223f60, size 0x00015fa8
  getsegdatafromheader(__LINKEDIT")      -> NULL, size 0
  ...
  getlastaddr()                          -> 0x00239f08  (end_kern = round_page of this)
OK: the header is internally consistent, every field matches the linker's own symbols
```

It also replays the walks and the `strncmp(..., 16)` comparisons `kernel_mach_header.c` performs.
It does not compile XNU's code — the structure layout is Apple's `mach-o/loader.h` for 32-bit
architectures, transcribed, and the device run is what tests XNU's reader against it. The run is
the answer: execution passed `arm_vm_init+0x1a8` (`ldr r6, [r0, #32]`) to reach
`arm_vm_init+0x1a90`'s `bl vm_set_page_size`, and it cannot pass the first without `sectDCONST`
being a real section.

## A duplicate definition the link found

`entry_stubs.c` had defined `gVirtBase`, `gPhysBase` and `gPhysSize` since before XNU's own
`arm_vm_init` was in the image, so that the name meant something to the objects that read it. With
`arm_vm_init.o` linked, both definitions exist and the link fails. The three are now behind
`#ifndef STAGE90_ENTRY_REAL_ARM_INIT`, and the comment that was already there explains why they were
never `const` — `arm_vm_init:351-353` **assigns** them, in place, and a stand-in in `.rodata` would
have faulted the moment it ran. Storage stubs went from 45 to 42 for the same reason.

## The measurement

Measured by linking the two objects, then an empty object in their place — both directions link
cleanly, which is exp-190's rule and is satisfied here because the probe stands on `patch_low_glo`,
which neither object defines:

```
resolved (14): arm_vm_init  arm_vm_prot_finalize  avail_end  avail_start  end_kern
               gPhysBase  gPhysSize  gVirtBase  max_mem  mem_size  static_memory_end
               vm_kernel_slid_base  vm_kernel_slide  vm_kernel_slid_top
added     (9): cpu_tte  _lastkerneldataconst  _lastkerneldataconst_padsize
               last_kernel_symbol  patch_low_glo_static_region  pmap_bootstrap
               pmap_init_pte_page  pmap_init_pte_static_page  vm_set_page_size
329 -> 324 undefined
```

The nine are the pmap and the VM, plus three Mach-O artifacts that only mean something inside a real
kernel image — `_lastkerneldataconst` and its padsize are used by the `__DATA,__const` block, and
`last_kernel_symbol` is `vm_kernel_top`. They become storage stubs and are read as zeroes, which is
what they are in an image that does not have them.

## Cost

| | exp-193 | now |
| --- | --- | --- |
| entry objects linked | 33 | 35 (`osfmk/arm/arm_vm_init.o`, `libkern/kernel_mach_header.o`) |
| plus | — | `entry_macho.s`, a new file in the image |
| entry text | 137452 B | 147308 B |
| entry image | 215136 B | 215144 B |
| entry `.bss` | 0x00234580 – 0x00239ec8 (22856 B) | 0x00234588 – 0x00239f08 (22912 B) |
| undefined | 321 | 324 |
| stubs | 276 functions, 45 storage | 282 functions, 42 storage |
| boot_args offset | +241664 | +241664 |
| payload text | 706950 B | 706958 B |

The image grew 8 bytes for 9856 bytes of new text, because the two objects' text landed inside the
`.text` region without crossing the boundary into `.data`. `xnu_entry_checks=5` /
`xnu_entry_failures=0` re-checked exp-175's four invariants with `args` still at +241664, and
`.bss` end at 0x00239f08 leaves 1.5 MB of the window unused.

## What is next: `vm_resident.o`, and the pmap behind it

`vm_set_page_size` is `osfmk/vm/vm_resident.c:480` — twelve statements, 52 bytes, and no calls at
all except a `panic` on a page size that is not a power of two. But its *object* is
`osfmk_vm_vm_resident.o`: **31692 bytes of text, 104 of data and 5772 of `.bss` across 113
references** — by a wide margin the largest object in this sequence, and the first from the VM
proper rather than the ARM layer. Linking it will add something like a hundred stubs at once. That
is a real change of scale, and it is worth knowing before the run rather than after: the entry
image has roughly 1.5 MB of room left inside XNU's window, so the size is not the problem — the
problem is that a step that adds 113 obligations has 113 ways to be the wrong step.

The stop after it is already known from the disassembly. `vm_set_page_size` returns, `set_mmu_ttb`,
`set_mmu_ttb_alternate` and `flush_mmu_tlb` are real, the `vm_*` stores are stores, and the next
call is **`pmap_bootstrap`**, which is `osfmk/arm/pmap.c`. So the probe moves to `pmap_bootstrap`,
and the run after that reaches the pmap — which is where the kernel starts building page tables for
memory that is not this image.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 147308, image 215144, 324 undefined, 282 stubs
./tools/host_entry_macho_check.sh
#   ... every field matches the linker's own symbols; getsegdatafromheader(__TEXT") -> 0x00200000

# what the two objects cost: 14 resolved, 9 added. The probe stands on `patch_low_glo`, which
# neither object defines, so both directions link.
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_ARM_VM_INIT_OBJ=/tmp/empty.o \
   STAGE90_ENTRY_KERNEL_MACH_HEADER_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 14 resolved
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 9 added

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|stub_no_exception\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -5

# the load the header exists to prevent: NULL sectDCONST, offset 0x20
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o | \
   awk '/<arm_vm_init>:/{f=1} f{print} f&&/^$/{exit}' | sed -n '/1988:/,/19b0:/p'
sed -n '405,432p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c

# the header and the script symbols it is built from
sed -n '1,60p' stages/stage90/xnu_arm_boot/entry_macho.s
grep -n '__entry_' stages/stage90/xnu_arm_boot/entry.ld
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep __entry_

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_vm_vm_resident.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_vm_vm_resident.o | wc -l
arm-none-eabi-nm -S out/xnu_kernel_obj/osfmk_vm_vm_resident.o | grep vm_set_page_size
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it, and
the device returned to Android on its own.
