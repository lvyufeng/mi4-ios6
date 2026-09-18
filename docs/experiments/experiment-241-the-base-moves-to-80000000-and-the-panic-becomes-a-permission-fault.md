# Experiment 241 — the Base Moves to 0x80000000, and the Panic Becomes a Permission Fault

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: the image runs where XNU's kernel-address test says memory is

Experiments 236 to 240 ended the same way every time: `is_sane_zone_ptr`'s first test,
`pmap_kernel_va` = `[0x80000000, 0xFFFEFFFF]`, is false for every address an image linked at
0x00200000 produces - `virtual_space_start` came out at 0x40400000 and `vm_kernel_slide` at
0x80200000 - so the first mandatory `free_to_zone` on the boot path panicked. No object can fix
that. The base has to move to where the device's RAM starts, which is 0x80000000
(`RAM_PHYS_BASE`) and where this SoC's kernel normally loads.

Five things changed, and only one of them is the number.

**1. The base is written down once.** It was in `build_entry.sh` *and* in `entry.ld`'s `. =`
statement. `entry.ld` no longer defines it - it uses a symbol the build injects, and a link that
forgets to inject it fails rather than producing an image at zero:

```bash
# build_entry.sh
ENTRY_BASE=0x80000000
run arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" --defsym=ENTRY_BASE=$ENTRY_BASE ...
```

```
# entry.ld
ENTRY(_start)
SECTIONS
{
    . = ENTRY_BASE;    /* --defsym, from build_entry.sh; see the header */
```

A link without the `--defsym` fails with ``undefined symbol `ENTRY_BASE' referenced in
expression`` and exit 1 - measured, not assumed. `tools/entry_closure.py` links the same script and
now passes `--defsym=ENTRY_BASE=0` deliberately: it discovers which symbols a set of objects leaves
undefined, and that answer does not depend on where the image lands.

**2. `entry_image_ptr` is the linker's extent, not two magic numbers.** It used to test
`[0x00200000, 0x04000000)`, and the base move would have made that false for the whole image -
turning every guarded read in `fleh_undef` into a *skipped* one, silently, because a guard that
declines to dereference and a guard that is wrong about where the image is look identical in a log:

```c
extern char __entry_text_start[];
extern char __entry_image_end[];
    return (p >= (uintptr_t)__entry_text_start) && (p < (uintptr_t)__entry_image_end);
```

**3. Nothing else needed a number changed.** The layout above the image is derived from the image's
own size, so moving the base moved all of it and none of it out of step - the build reports the same
`args +901120, topOfKernelData +2097152, tree +4194304, window 8388608` as at the old base. The
payload's window mapping (`mmu.c`, a loop over `STAGE90_XNU_ENTRY_BASE` and `STAGE90_XNU_ENTRY_SIZE`)
and the boot args (`xnu_entry_jump.c`, `virtBase = physBase = STAGE90_XNU_ENTRY_BASE`) are macros
over the generated header. `start.s` needs no change either, and the reason is worth stating: it
loads `physBase`, `virtBase` and `memSize` **out of the boot args** (`start.s:103-105`), maps the
1 MB section around its own `pc` as V=P, and maps `[virtBase, virtBase + memSize)` to
`[physBase, physBase + memSize)`. With the two equal and both equal to the link address, every
conversion is still the identity - and the epilogue's trick of disabling the MMU and continuing to
execute survives, because the image was physically *copied* to its base and never relocated.

**4. The comments that carried the old base's arithmetic were updated**, including the one that
said the epilogue's guarantee depends on `[physBase, physBase + memSize)` being
`[0x00200000, 0x00a00000)`.

**5. The Mach-O check needed no change**: `tools/host_entry_macho_check.sh` reads the base out of
the ELF as `__entry_text_start` rather than hard-coding it, which is why it followed the move
without an edit.

## The result

```
MI4IOS6_STAGE90_XNU xnu_entry_copied_to=0x80000000
 xnu_entry_copied_bytes=0x000abb78
 xnu_entry_bss_bytes=0x0002ed58
 xnu_entry_device_tree_pa=0x80400000
 xnu_entry_device_tree_len=0x00007358
 xnu_entry_args_pa=0x800dc000
 xnu_entry_args_virtBase=0x80000000
 xnu_entry_args_physBase=0x80000000
 xnu_entry_args_memSize=0x00800000
 xnu_entry_args_topOfKernelData=0x80200000

MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: exception: data abort
 xnu_entry_kv_written=0x0000004c
 xnu_entry_kv_in_dram=0x0000004c
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_data_abort_dfar=0x80300000
 xnu_entry_data_abort_dfsr=0x0000080f

No errors detected
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x4c` = 76 bytes:
the data-abort handler reports two keys and nothing else, against `fleh_undef`'s 25.

**The `udf` in `DebuggerTrapWithState` is gone.** Four runs in a row ended there, reached only
through `panic_trap_to_debugger`; this one does not, so `free_to_zone`'s check passed the address it
was handed and the `zfree` panic is not what stopped XNU. That is the result the stage was for.
There is also no `stub_hit=` line, so no unprovided function was reached either: the run ends at an
instruction that faulted, not at a symbol the image lacks.

## `DFSR = 0x0000080f` is a write to a read-only page

The data-abort handler already reported `DFAR` and `DFSR`; decoding the second one is what places
the fault:

```
  0x0000080f
  bit 11  WnR = 1                          a WRITE, not a read
  bit 10  FS[4] = 0, bits 3..0 = 0b1111
  FS = 0b01111                             permission fault, level 2 (page)
```

Two facts follow, and they are the whole of what this run establishes about the fault:

- **The faulting address is in the window, 3 MB above the new base.** `0x80300000` is inside
  `[0x80000000, 0x80800000)` and above `avail_start`, which `arm_vm_init.c:373,399` puts at
  `gPhysBase + 4 pages + 6 pages` - 0x8020A000 before the pre-initialization loop below adds its
  two pages, so 0x8020C000 after.
- **The mapping is a page, not a section.** Everything `_start` installs in this window is a 1 MB
  *section* with `AP_RWNA` - kernel read-write - because `memSize` is 8 MB and every 1 MB boundary
  is hit (`start.s:198-206`, the `mapveqp` path; the `mapveqpL2` page path at `:209` is only for a
  `memSize` that is not 1 MB aligned). A section mapping cannot produce a *page* permission fault.
  So by the time of the fault XNU's own pmap was live and had mapped `0x80300000` with an L2 page
  entry that is not writable.

That is as far as the two numbers go, and it is worth saying what they do *not* say: they do not
name the instruction, so they do not say whether the fault is inside the protection code that
created the mapping or in a later write into a region it had already protected. Both are real code
in this image - the pmap half of `arm_vm_init` is linked in full:

```
$ arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -E 'pmap_bootstrap|arm_vm_prot_init|pmap_init_pte_page|pmap_enter_options'
arm_vm_prot_init    T 800167d0 1280     # 4736 bytes of per-page protection setup
pmap_bootstrap      T 80020e8c 880      # 2176 bytes
pmap_enter_options  T 80023238 1064     # 4196 bytes
pmap_init_pte_page  T 800249fc 104      # 260 bytes
```

and the region the fault is in is exactly the one `arm_vm_init.c:512-530` exists to pre-cover:

```c
	/*
	 * To avoid recursing while trying to init the vm_page and object
	 * mechanisms, pre-initialize kernel pmap page table pages to cover this address range:
	 *    2MB + FrameBuffer size + 3MB for each 256MB segment
	 */
	off_end = (2 + (mem_segments * 3)) << 20;
	...
	for (off = 0, va = (gVirtBase+MEM_SIZE_MAX+0x3FFFFF) & 0xFFC00000; off < off_end; off += ARM_TT_L1_PT_SIZE) {
```

which with `mem_segments = (mem_size + 0x0FFFFFFF) >> 28 = 1` for this 8 MB window is `off_end` =
5 MB - and 3 MB is inside `[2 MB, 5 MB)`. That loop maps **`virtual_space_start`, which is now
0xC0000000**, so it is not itself the writer at 0x80300000; it is the same "2 MB + 3 MB per segment"
shape, and the coincidence is worth naming rather than resolving here.

**So the next experiment reads the faulting instruction.** `fleh_dataabt` is the one handler that
still reports only the hardware's own two registers; every other handler in this file names itself,
and `fleh_undef` grew a frame read for exactly this reason. The same two additions answer this:

- `lr_abt` and `lr_abt - 8` - for a data abort, `LR_abt` is the *second* instruction after the
  faulting one, so `lr - 8` is the address to look up in the disassembly. That is the name of the
  code, and it is one dereference of a log line away.
- `TTBR0`, `TTBR1`, `TTBCR` and `SCTLR` - which page tables are live and whether the MMU's
  write-allocate/alignment bits are what `_start` left. If `TTBR0` is `topOfKernelData`
  (0x80200000) the fault is inside `arm_vm_init`'s own setup; if it is somewhere else, the pmap has
  taken the tables over and the fault is later. `SCTLR`'s `XP`/`TEX`/`AFE` bits also say whether a
  write to a read-only page is even expected to fault the way it did.

## Cost

| | exp-240 | now |
| --- | --- | --- |
| `ENTRY_BASE` | 0x00200000 | **0x80000000** |
| entry text | 592753 B | **592785 B** |
| entry image | 703352 B | **703352 B** |
| entry `.bss` | 0x800ab4f0–0x800da248 (191832 B) | **unchanged, moved with the base** |
| layout offsets | args +901120, topOfKernelData +2097152, tree +4194304, window 8388608 | **unchanged** |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |

32 bytes of entry text, for the `entry_image_ptr` change: two linker symbols instead of two
immediates. The image's own bytes are identical in size and the payload was rebuilt from the same
regenerated header - the window loop and the boot args are macros, so the payload's diff is the
base and nothing else.

## Reproduce

```bash
# the change: the base is now a --defsym, and the linker script defines no base of its own
grep -n 'ENTRY_BASE' stages/stage90/xnu_arm_boot/build_entry.sh
grep -n 'ENTRY_BASE' stages/stage90/xnu_arm_boot/entry.ld

# ... and a link without it fails rather than landing at zero
arm-none-eabi-ld -T stages/stage90/xnu_arm_boot/entry.ld -nostdlib -o /tmp/nobase.elf \
    out/stage90/xnu_arm_entry_macho.o ; echo "exit=$?"      # undefined symbol `ENTRY_BASE'

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'text size|image bytes|bss|layout' out/stage90/xnu_arm_entry.txt

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20

# the decode: a write, to a level-2 page mapping that is not writable
python3 -c 'd=0x0000080f; print(hex(d), "WnR" if d & 0x800 else "read", "FS=%#x"%((d>>10&1)<<4 | d&0xf))'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e241.dis
grep -n 'mapveqp' external/xnu-4570.1.46/osfmk/arm/start.s

# where avail_start is, and what the fault address is relative to it
sed -n '370,402p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
sed -n '506,536p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
grep -n 'define ARM_TT_L1_PT_SIZE' external/xnu-4570.1.46/osfmk/arm/proc_reg.h
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
