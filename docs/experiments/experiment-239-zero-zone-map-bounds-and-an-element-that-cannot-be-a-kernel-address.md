# Experiment 239 — Zero Zone-Map Bounds, and an Element That Cannot Be a Kernel Address

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

Experiment 238 named the condition — `zfree: freeing invalid pointer %p to zone %s`, `zalloc.c:1208`
— and this run reads the two things the format string names, plus the two globals the check
compares the element against:

```c
extern uint32_t zone_map_max_address;
extern uint32_t zone_map_min_address;
    entry_kv("xnu_entry_zone_map_min", zone_map_min_address);
    entry_kv("xnu_entry_zone_map_max", zone_map_max_address);

    arg0 = 0u;
    arg1 = 0u;
    if (entry_image_ptr((uintptr_t)r_args)) {
        arg0 = entry_word_at((uintptr_t)r_args);
        arg1 = entry_word_at((uintptr_t)r_args + 4u);
    }
    entry_kv("xnu_entry_panic_arg0", arg0);
    entry_kv("xnu_entry_panic_arg1", arg1);
```

`entry_word_at` reads four bytes one at a time rather than with a single `ldr`: an unaligned `ldr`
is legal on ARMv7 only while `SCTLR.A` is clear, and a fault handler that faults says nothing at
all. The dump of the word at a pointer is guarded by `entry_image_ptr`, because a wild pointer here
would fault inside the fault handler.

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_undef_pc=0x0022d3c8
 xnu_entry_trap_r9_fmt=0x0028cbf3
 xnu_entry_trap_r8_args=0x0029be88
 xnu_entry_trap_sl_caller=0x00000000
 xnu_entry_zone_map_min=0x00000000
 xnu_entry_zone_map_max=0x00000000
 xnu_entry_panic_arg0=0x0029be94
 xnu_entry_panic_arg1=0x00000020
 xnu_entry_fmt_w0=0x72667a22
 ... w1..w5 = "ee: freeing invalid "
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own. `r8 = 0x0029be88` is the same value experiment
238 measured, which is the cross-check that both runs read the same caller's frame.

**Both zone-map bounds are zero, and that is the finding this experiment exists for.**

## The measurement was one dereference short, and the key name was wrong

`xnu_entry_panic_arg0=0x0029be94` is not the element, and `0x00000020` is not the zone name, and the
reason is visible in the two lines that produced them.

`r8` is `va_list *panic_args` (`osfmk/kern/debug.c:559`), not the `va_list` itself, so the first word
at `r8` is the list's `__ap` — a pointer to the first argument — and the word after it is whatever
follows the struct in `panic`'s frame. `panic`'s own frame settles it to the byte:

```
0022d4f4 <panic>:
  22d4f8:	push	{fp, lr}
  22d4fc:	sub	sp, sp, #20
  22d500:	add	ip, sp, #28
  22d504:	stm	ip, {r1, r2, r3}      # the three varargs, at sp+28
  22d50c:	mov	r2, #0                # reason
  22d510:	mov	r3, #0                # ctx
  22d514:	str	r1, [sp, #16]         # va_list.__ap = sp+28 = struct+12
  22d520:	stmib	sp, {r1, lr}          # options 0,0 then lr
  22d524:	add	r1, sp, #16           # &va_list
  22d528:	bl	22dc14 <panic_trap_to_debugger>
```

The `va_list` struct lives at `panic`'s `sp+16` and its `__ap` is `sp+28` — **twelve bytes in**,
which is exactly the offset measured: `0x0029be94 - 0x0029be88 = 12`. So `__ap` points at the saved
`r1`, and the element and the zone name are one dereference further:

```
  element   = *(uint32_t *) __ap
  zone_name = *(uint32_t *)(__ap + 4)
```

The same frame also shows that `0x0029be94` is a live stack address rather than anything to do with
zones: `intstack D 298000` and `intstack_top D 29c000` are XNU's 16 KB boot stack in `.data`, and
`panic`'s frame sits 364 bytes below its top.

**`xnu_entry_trap_sl_caller` is a misnomer and `r10` is not the caller.** `panic_trap_to_debugger`
loads `r4` and `sl` from `[sp+56]` and `[sp+60]`, and both are handed to `DebuggerTrapWithState` as
the *two halves of the 64-bit* `db_panic_options` — `stm sp, {r4, sl}`. The caller is `r5`, loaded
from `[sp+64]` and stored at `[sp+12]`. Experiment 238's document reads the three as
"`r10 = sl = panic_options_mask`", which is half right — `sl` is the mask's high word, not the
caller — and the correction matters because `r5` is the register that names the panicking call
site. It is clobbered by `fleh_undef`'s own prologue (`mrs r5, SPSR`), but
the prologue *saves* it — `strd r4, [sp, #-24]!` — so it is readable in the next run.

`sl = 0` still holds as evidence, it just says something smaller than the key name claims: the
options mask `panic()` passes is zero.

## Why the bounds are zero: `zone_init` cannot run in this image

```
$ grep -n 'zone_map_min_address\|zone_map_max_address' osfmk/kern/zalloc.c
340:vm_offset_t     zone_map_min_address = 0;  /* initialized in zone_init */
341:vm_offset_t     zone_map_max_address = 0;
2958:	zone_map_min_address = zone_min;
2959:	zone_map_max_address = zone_max;
```

Two declarations at zero and one writer. The writer is `zone_init`, its only caller in the image is
`vm_mem_bootstrap+0x204`, and the image's own `zone_init` really does store them:

```
  26eee8:	str	r1, [r0]      # r0 = 0x002bb268, zone_map_min_address
  26ef00:	str	r2, [r0]      # r0 = 0x002bb26c, zone_map_max_address
```

so a non-zero reading was possible and the zero is the boot's answer, not the instrument's. The path
to `+0x204` runs through two stubs first:

```
$ arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -E 'kext_alloc_init|kmem_init|kmem_suballoc'
kext_alloc_init T 27ea58 c        #  12 bytes, body: b entry_stub_hit
kmem_init       T 27eac4 c        #  12 bytes, body: b entry_stub_hit
kmem_suballoc   T 27ead0 c        #  12 bytes, body: b entry_stub_hit
```

`vm_mem_bootstrap` calls `kmem_init` at `+0x074` and `kext_alloc_init` at `+0x1a4`, both **before**
`zone_init` at `+0x204`, and `zone_init`'s own first call is `kmem_suballoc` at `+0x48`. A stub ends
the run — `entry_stub_hit` never returns — so **`zone_init` has never executed in this image**, and
the two globals it is the only writer of have never been anything but zero.

## The check that fired is the first one, and it cannot pass at this image's addresses

`free_to_zone` inlines `is_sane_zone_element` → `is_sane_zone_ptr`, and the image shows the test
order and the failure:

```
  26fde8:	cmp	r6, #0
  26fdec:	beq	26fec8                # element == 0 is sane
  26fdf0:	cmn	r6, #65536
  26fdf4:	bge	26fe30                # not pmap_kernel_va -> PANIC
  26fdf8:	ands	r1, r6, #3
  26fdfc:	bne	26fe30                # misaligned -> PANIC
  26fe00:	ldr	r1, [sl, #160]        # zone->flags
  26fe04:	and	r1, r1, #10           # collectable, allows_foreign
  26fe08:	cmp	r1, #2
  26fe0c:	bne	26fe44                # not (collectable && !allows_foreign) -> sane
  26fe10:	cmp	r0, r6                # r0 = zone_map_min_address
  26fe14:	bhi	26fe30                # min > element -> PANIC
  ...
  26fe2c:	bcc	26fe44                # element + elem_size - 1 < max -> sane
  26fe30:	ldr	r2, [sl, #168]        # zone->zone_name   <- the panic's second argument
  26fe40:	bl	22d4f4 <panic>
```

`cmn r6, #0x10000 / bge` is `pmap_kernel_va`, and it is the **first** test — before the zone flags
and before the bounds. `pmap_kernel_va(VA)` is `VA >= VM_MIN_KERNEL_ADDRESS && VA <=
VM_MAX_KERNEL_ADDRESS` (`osfmk/arm/pmap.h:371`, `osfmk/mach/arm/vm_param.h:169-170`), which is
`[0x80000000, 0xFFFEFFFF]` — a compile-time constant that has nothing to do with where the image was
linked.

This run's own capture says where that is:

```
MI4IOS6_STAGE90_XNU xnu_entry_args_virtBase=0x00200000
MI4IOS6_STAGE90_XNU xnu_entry_args_physBase=0x00200000
MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x00800000
```

`virtBase = physBase = 0x00200000` — deliberate identity, and `entry.ld`'s header says why: it makes
every `LOAD_PHYS_ADDR` conversion in `start.s` an identity and makes `_start`'s page tables identity
tables, which is what lets the entry epilogue disable the MMU and keep executing. Two consequences
follow from it, both arithmetic:

```
  arm_vm_init.c:496   vm_kernel_slide = gVirtBase - 0x80000000       = 0x80200000
  arm_vm_init.c:134   MEM_SIZE_MAX                                    = 0x40000000
  arm_vm_init.c:505   pmap_bootstrap((gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000)
                                                                      = 0x40400000
```

`virtual_space_start` — where the kernel steals its own virtual memory — lands at **0x40400000**,
below `VM_MIN_KERNEL_ADDRESS`. So every address XNU's own allocator hands out is rejected by its own
kernel-address test, and `vm_kernel_slide` is `0x80200000`, the kernel reported as slid the whole
width of the address space. Neither is a missing object; both are the image's base.

## Which free: the first one on the straight-line path

`free_to_zone` has exactly three callers in the image — `zcram+0x3a0`, `zcram+0x444`,
`zfree+0x50c` — and the run stopped before `kmem_init` (`vm_mem_bootstrap+0x074`), because a stub
terminates the run and this one did not terminate at a stub. So the panic is inside `vm_page_bootstrap`
(`+0x01c`), `zone_bootstrap` (`+0x02c`), `vm_object_bootstrap` (`+0x03c`) or `vm_map_init` (`+0x05c`).
Walking the image's call graph from `vm_mem_bootstrap`, following only calls not behind a conditional
branch and stopping at stubs, reaches exactly one free on that window:

```
  vm_mem_bootstrap -> vm_map_init -> zcram
```

and `vm_map_init+0x260` is the first of its three `zcram` calls:

```
  vm_map.c:869   zcram(vm_map_zone, (vm_offset_t)map_data, map_data_size);
```

`map_data` comes from `vm_map_steal_memory` (`vm_map.c:806`), which calls
`pmap_steal_memory(round_page(10 * sizeof(struct _vm_map)))` — 4096 bytes — and that is the **first**
`pmap_steal_memory` in the boot, so it returns `virtual_space_start` itself:

```
  map_data = 0x40400000
```

`vm_map_zone` is a zone that cannot fail any *other* test: `zone_change(vm_map_zone, Z_COLLECT,
FALSE)` and `Z_FOREIGN, TRUE` are called before the `zcram` (`vm_map.c:851-853`), so the
`collectable && !allows_foreign` branch is not taken at all, and the element `map_data + offset` is a
multiple of the zone's 160-byte element size, so the alignment test passes. **`pmap_kernel_va` is the
only test that can have failed**, and the panic's second argument is then the zone's name — which
`zinit` was given as the literal **`"maps"`** (`vm_map.c:811`).

## Cost

**No object was linked**; the undefined list is still 645 symbols. `fleh_undef` grew 256 bytes,
measured the same way as the last two runs, by the trap address moving:

| | exp-238 | now |
| --- | --- | --- |
| `DebuggerTrapWithState` | 0x0022d2c8 | **0x0022d3c8** |
| `fleh_undef` | — | **238 B** (0x202550) |
| build's reported text size | 591633 B (exp-236) | **592689 B** |
| entry objects linked | 75 | 75 |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |
| entry `.bin` | 703352 B | **703352 B** |

Across experiments 237 to 239 the trap address moved `0x0022d1a8 → 0x0022d3c8`: **544 bytes of entry
text**, all of it inside `fleh_undef`, and not one byte of image size.

## What is next

Two things, and the first is a measurement while the second is the stage it points at.

**Experiment 240 reads the frame instead of the live registers.** `fleh_undef`'s prologue is
`strd r4, [sp, #-24]!`, `strd r6, [sp, #8]`, `str r8, [sp, #16]`, so the original `r4`, `r5`, `r6`,
`r7` and `r8` are all still in its own frame — which is the only way to get `r5`, since the prologue's
second instruction (`mrs r5, SPSR`) destroys it. Four of the five values are known in advance, which
makes the frame offset itself testable:

| | expected |
| --- | --- |
| `[sp+0]` — `r4`, low half of `db_panic_options` | `0x00000000` |
| `[sp+4]` — `r5`, `db_panic_caller` | **`0x0026fe44`** = `free_to_zone+0x148`, the instruction after the `bl panic` |
| `[sp+8]` — `r6`, `ctx` | `0x00000000` |
| `[sp+12]` — `r7`, `reason` | `0x00000000` |
| `[sp+16]` — `r8`, `panic_args` | `0x0029be88`, which experiments 238 and 239 both measured live |

and then the double dereference, with `entry_image_ptr` widened because the answer is expected to be
**outside** the image:

```
  element   in [0x40400000, 0x40401000)     # map_data + one element offset, inside a 4 KB chunk
  zone_name -> the literal "maps"
```

If the element comes back in that window and the name comes back `"maps"`, the diagnosis is closed.
If the element comes back near `0x002xxxxx`, the free is somewhere else and the walk is wrong.

**The stage that follows is the base.** Everything above says the same thing from four directions:
the entry image runs below `VM_MIN_KERNEL_ADDRESS`, so `pmap_kernel_va` is false for its every
address, `vm_kernel_slide` is `0x80200000`, and `virtual_space_start` is `0x40400000`. `free_to_zone`
is on the mandatory boot path and cannot pass. The fix is to link and run the image at a base
**at or above 0x80000000**, which is where XNU is designed to be and where the device's RAM already
starts — `RAM_PHYS_BASE` is `0x80000000` (`stage90.h:21`), and the kernel's usual physical load
address on this SoC is `0x80008000`. That is a change to `ENTRY_BASE`, to the payload's mapping of
the window, and to `physBase`/`virtBase` in the boot_args — a stage of its own, with the entry
epilogue's identity trick as the thing to re-derive rather than assume.

## Reproduce

```bash
# the change: three keys, a byte-at-a-time word reader, and a pointer guard
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
wc -l out/stage90/xnu_arm_entry_undef.txt                      # 645, unchanged

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -24

# the two bounds, and their only writer
grep -n 'zone_map_min_address\|zone_map_max_address' external/xnu-4570.1.46/osfmk/kern/zalloc.c
grep -n 'bl\t26ee54 <zone_init>' /tmp/e239.dis                  # vm_mem_bootstrap+0x204, one caller

# the stubs that stand in front of it, and the order
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -E 'kext_alloc_init|kmem_init|kmem_suballoc'

# the check that fired, and that it is first
awk '/<free_to_zone>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e239.dis | head -30
grep -n 'define pmap_kernel_va' -A2 external/xnu-4570.1.46/osfmk/arm/pmap.h
grep -n 'VM_MIN_KERNEL_ADDRESS\|VM_MAX_KERNEL_ADDRESS' external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h

# where the kernel's own virtual space begins in this image
sed -n '494,506p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
grep -n 'define MEM_SIZE_MAX' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
grep -n 'virtBase\|physBase' stages/stage90/xnu_entry_jump.c

# the first free on the straight-line path, and the data it is handed
python3 tools/xnu_entry_callwalk.py --root vm_mem_bootstrap
grep -n 'zcram(vm_map_zone' external/xnu-4570.1.46/osfmk/vm/vm_map.c
sed -n '/^vm_map_steal_memory(/,/^}/p' external/xnu-4570.1.46/osfmk/vm/vm_map.c
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -wE 'intstack|intstack_top'

# the frame offset the next experiment uses, straight out of this one
awk '/<panic>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e239.dis | head -12
echo $((0x0029be94 - 0x0029be88))                               # 12 = the va_list's __ap offset
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
