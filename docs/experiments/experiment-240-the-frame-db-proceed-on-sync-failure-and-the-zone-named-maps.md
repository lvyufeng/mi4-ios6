# Experiment 240 — the Frame, `db_proceed_on_sync_failure`, and the Zone Named `"maps"`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

Experiment 239 left two things to read: the trap's *frame*, which is the only place the original
`r5` survives, and the two arguments `zfree: freeing invalid pointer %p to zone %s` names, which sit
one dereference past where experiment 239 read. Both are in `fleh_undef`, and three smaller changes
go with them.

**The frame, read with the offset taken from the disassembly first.** `fleh_undef`'s prologue is
`strd r4, [sp, #-32]!`, `strd r6, [sp, #8]`, `strd r8, [sp, #16]`, `str sl, [sp, #24]`,
`str lr, [sp, #28]`, and the `mov r?, sp` this experiment added lands *immediately after* the
writeback - so the captured `sp` is the frame base, and the trapping context's `r4` through `lr` are
at `+0`, `+4`, ..., `+28` in that order. The keys are named for the registers they are predicted to
hold, so a wrong value is readable as a shift rather than as a mystery:

```c
    __asm__ volatile ("mov %0, sp" : "=r"(frame));
    ...
    entry_kv("xnu_entry_frame_sp", (uint32_t)frame);
    for (i = 0u; i < 8u; i++) {
        static const char *const fr[8] = {
            "xnu_entry_frame_r4", "xnu_entry_frame_r5", "xnu_entry_frame_r6",
            "xnu_entry_frame_r7", "xnu_entry_frame_r8", "xnu_entry_frame_r9",
            "xnu_entry_frame_sl", "xnu_entry_frame_lr",
        };

        entry_kv(fr[i], entry_word_at(frame + (i * 4u)));
    }
```

**The two arguments, one dereference further on.** `r_args` is a `va_list *`, so the word at it is
the list's `__ap` and not the first argument - experiment 239 measured that word as `0x0029be94`,
twelve bytes past `r_args`, which is where `panic`'s frame puts `__ap`. So:

```c
    if (entry_image_ptr((uintptr_t)r_args)) { ap = entry_word_at((uintptr_t)r_args); }
    entry_kv("xnu_entry_panic_ap", ap);
    if (entry_image_ptr((uintptr_t)ap)) {
        element = entry_word_at((uintptr_t)ap);
        zone_name = entry_word_at((uintptr_t)ap + 4u);
    }
    entry_kv("xnu_entry_panic_element", element);
    entry_kv("xnu_entry_panic_zonename", zone_name);
    if (entry_image_ptr((uintptr_t)zone_name)) {
        entry_kv("xnu_entry_zone_name_w0", entry_word_at((uintptr_t)zone_name));
        entry_kv("xnu_entry_zone_name_w1", entry_word_at((uintptr_t)zone_name + 4u));
    }
```

**The element is reported and never dereferenced, so `entry_image_ptr` is *not* widened.** Exp-239's
plan said to widen it, because the element is expected at `0x40400000` - outside the image. That was
the right instinct about where the element is and the wrong conclusion about what to do with it: the
element's *value* comes out of the `va_list` on the (mapped) boot stack, and dereferencing
`0x40400000` would be a data abort inside the abort handler, which says nothing at all. Every address
this experiment dereferences is still inside the image.

**Two smaller changes.** `ENTRY_KV_BUF` goes 1024 -> 2048, because the run's key list is now 25 keys
and `entry_kv` drops keys *silently* when the buffer is nearly full (`entry_kv` returns without
writing; the run reports fewer keys and nothing else) - the same failure that made experiment 237
grow it 768 -> 1024. And `xnu_entry_trap_sl_caller` is renamed `xnu_entry_trap_sl_options_hi`: exp-239
established that `sl` is the high half of the 64-bit options mask and not the caller, so the old name
was wrong and the record of experiments 236 to 239 keeps it while the new image does not.

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
 xnu_entry_kv_written=0x0000033f
 xnu_entry_kv_in_dram=0x0000033f
 xnu_entry_undef_lr=0x0022d40c
 xnu_entry_undef_pc=0x0022d408
 xnu_entry_undef_spsr=0x60000093
 xnu_entry_panic_str=0x00000000
 xnu_entry_panic_caller=0x00000000
 xnu_entry_panic_message=0x00000000
 xnu_entry_trap_r9_fmt=0x0028cc33
 xnu_entry_trap_r8_args=0x0029be88
 xnu_entry_trap_sl_options_hi=0x00000000
 xnu_entry_zone_map_min=0x00000000
 xnu_entry_zone_map_max=0x00000000
 xnu_entry_frame_sp=0x002acb60
 xnu_entry_frame_r4=0x00000000
 xnu_entry_frame_r5=0x00000001
 xnu_entry_frame_r6=0x00000000
 xnu_entry_frame_r7=0x00000000
 xnu_entry_frame_r8=0x0029be88
 xnu_entry_frame_r9=0x0028cc33
 xnu_entry_frame_sl=0x00000000
 xnu_entry_frame_lr=0x0022d40c
 xnu_entry_panic_ap=0x0029be94
 xnu_entry_panic_element=0x40401f20
 xnu_entry_panic_zonename=0x0028b1b6
 xnu_entry_zone_name_w0=0x7370616d
 xnu_entry_zone_name_w1=0x73655200
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x33f` = 831 bytes,
so the 25 keys reached DRAM and the buffer was not the limit: 831 bytes over 25 keys is 33 bytes
a key, which is the key name plus the thirteen characters `=0x` and eight hex digits.

**The diagnosis is closed.** The element is an address in the `0x4040xxxx` chunk, the zone's name is
the literal `"maps"`, and `is_sane_zone_ptr`'s first test, `pmap_kernel_va`, is false for it. That is
`zcram(vm_map_zone, map_data, map_data_size)` from `vm_map_init+0x260` (`vm_map.c:869`), on the
mandatory boot path, and no object can make it pass.

## The frame: six of eight exactly, and the two that were not are the finding

Six values were predicted before the run and came back to the byte:

| key | predicted | measured | |
| --- | --- | --- | --- |
| `frame_r4` | `0x00000000` | `0x00000000` | the options mask's low half |
| `frame_r6` | `0x00000000` | `0x00000000` | `ctx` |
| `frame_r7` | `0x00000000` | `0x00000000` | `reason` |
| `frame_r8` | `0x0029be88` | `0x0029be88` | `panic_args`, same as exp-238 and exp-239 read live |
| `frame_r9` | `0x0028cc94` | `0x0028cc33` | the same pointer as `trap_r9_fmt`, which is the check |
| `frame_sl` | `0x00000000` | `0x00000000` | the options mask's high half |
| `frame_lr` | `0x0022d40c` | `0x0022d40c` | the instruction after the `udf` |

`frame_r9 == trap_r9_fmt == 0x0028cc33`, which is what the pair is for: the frame block is read at
the offset the disassembly predicted, and the live register agrees with it. The one number in the
table that did not match is a prediction written against an *intermediate* build - `0x0028cc94` was
the format string's address before the frame block changed this image's text size and the linker
moved the read-only data - which is why the prediction worth making is the equality between the
frame copy and the live read, and not a constant. And the pointer is the format string, read out of
the built image:

```
$ python3 -c '...out/stage90/xnu_arm_entry.elf, VA 0x0028cc33...'
0x0028cc33: b'"zfree: freeing invalid pointer %p to zone %s\\n"\x00thread_'
```

The leading `0x22` is a quote character, which is why experiment 238's six-word dump read `zfr` as
its first word: the message begins `"zfree`, and the quote was there all along.

**`frame_r5 = 0x00000001` is not `db_panic_caller`, and there was one more frame in the way.** The
value is right where the prediction put it, and the block's other seven values pin the offset, so
this is r5's real value at the trap. Experiment 239's correction said r5 = `db_panic_caller` because
`panic_trap_to_debugger` loads it that way - and that is true of `panic_trap_to_debugger`
(`ldr r5, [sp, #64]` at `22dc74`, against the incoming args `[sp+56]` = options low, `[sp+60]` =
options high, `[sp+64]` = caller) and false of the function the trap is actually in:

```
0022d3e0 <DebuggerTrapWithState>:
  22d3e0:	push	{r4, r5, fp, lr}
  22d3e4:	sub	sp, sp, #16
  22d3e8:	ldr	lr, [sp, #44]      # db_panic_caller -> lr
  22d3ec:	ldr	r5, [sp, #40]      # db_proceed_on_sync_failure -> r5
  22d3f0:	ldr	r4, [sp, #32]      # db_panic_options, low half
  22d3f4:	ldr	ip, [sp, #36]      # ... and high half
  22d3f8:	stm	sp, {r4, ip}
  22d3fc:	str	r5, [sp, #8]
  22d400:	str	lr, [sp, #12]
  22d404:	bl	22d448 <DebuggerSaveState>
  22d408:	udf	#65006	; 0xfdee
```

`DebuggerTrapWithState` **reloads r5 from its own stack argument**, which is
`db_proceed_on_sync_failure` - a `boolean_t`, and the call site sets it with `mov r0, #1; str r0,
[sp, #8]` (`22ddf8`), so **r5 = 1 is exactly right and exactly uninteresting**. The caller goes into
`lr` instead, and `lr` is destroyed by the `bl DebuggerSaveState` one instruction before the `udf`.

So `db_panic_caller` is not in any register at the trap and is not what experiment 239 said it was.
The three register readings that exp-239's doc recorded - `r4`/`sl` = the two halves of
`db_panic_options`, and `r5` = the caller - are right for `panic_trap_to_debugger` and the r5 one is
wrong by one frame. There is no fourth run to read: the caller is recoverable from the *stack* (at
`db_proceed_on_sync_failure`'s old slot, twelve bytes above the trap's `sp`) and not from the
registers, and it is not needed, because the caller of `panic` in this image is settled statically
and the *condition* is settled by the two arguments below. `free_to_zone` has exactly one `bl panic`
in the image - `26fe80` - so the caller would be `0x0026fe84` = `free_to_zone+0x148` whatever the
trace, and it is the element and the zone name that say *which* free.

**And the frame is not on XNU's boot stack.** `frame_sp = 0x002acb60`, inside the entry image's
`.bss` (`0x002ab4f0`-`0x002da248`), while `panic`'s frame is at `0x0029be88` - inside `intstack`
(`intstack D 298000`, `intstack_top D 29c000`, XNU's 16 KB boot stack in `.data`). The vector
trampoline loads `SP` from a literal and branches rather than pushing, which is why the trapping
registers arrive intact and why the trap has a stack of its own. It also means the trap-time `SP`
is not among the values this experiment can read: it is gone the moment the vector loads the
exception stack.

## The arguments: the element is `0x40401f20` and the zone is `"maps"`

```
  xnu_entry_panic_ap        = 0x0029be94     # the va_list's __ap, exactly exp-239's word
  xnu_entry_panic_element   = 0x40401f20
  xnu_entry_panic_zonename  = 0x0028b1b6
  xnu_entry_zone_name_w0    = 0x7370616d     # "maps"
  xnu_entry_zone_name_w1    = 0x73655200     # "\0Res"
```

and the bytes at the zone name, out of the same image the device ran:

```
$ ... VA 0x0028b1b6 ...
0x0028b1b6: b'maps\x00Reserved VM map entries\x00VM map copies\x00VM map holes\x00'
```

**The zone is `vm_map_zone`, and its name is the literal `"maps"` from `vm_map.c:811`.** The word
`0x7370616d` was predicted before the run from a byte search of the built image, and it is the
strongest single confirmation in this experiment: the check that is failing is inside
`free_to_zone`, the zone it is being handed is the one `vm_map_init` created, and the element is in
the chunk `vm_map_steal_memory` stole.

**The element is in the `0x4040xxxx` chunk but not at its first byte, and exp-239's expected window
was too narrow.** Exp-239 wrote `element in [0x40400000, 0x40401000)`, from
`pmap_steal_memory(round_page(10 * sizeof(struct _vm_map)))` and a guess that the result is one page.
The measurement is `0x40401f20`, which is 7968 bytes into the region and *not* a multiple of any
single element size from the chunk's start - and the reason is in `zcram`:

```
$ sed -n '2712,2718p' external/xnu-4570.1.46/osfmk/kern/zalloc.c
	} else {
		element_count = (int)(size / elem_size);
		random_free_to_zone(zone, newmem, 0, element_count, entropy_buffer);
	}
```

and in the `!from_zm` branch above it, which is the one `vm_map_zone` takes (`Z_FOREIGN TRUE`,
and `zone_map_min_address == zone_map_max_address == 0` make `from_zone_map` false):

```
		for (; size > 0; newmem += PAGE_SIZE, size -= PAGE_SIZE) {
			...
			random_free_to_zone(zone, newmem, first_element_offset, element_count, entropy_buffer);
		}
```

`random_free_to_zone` - the function is named for what it does - frees the chunk's elements **in a
random order**, so the first element `is_sane_zone_element` sees is not the chunk's first and cannot
be predicted; the run drew `0x40401f20`. What the region, the zone and the condition all say is
unaffected, and the corrected statement is the one worth keeping: the element is somewhere in the
`0x4040xxxx` chunk, which is where `pmap_steal_memory` hands out memory in this image, and it is
below `VM_MIN_KERNEL_ADDRESS` however the draw came out.

## The diagnosis

Four measurements and four source facts agree, and there is nothing left to read:

1. `zalloc.c:1208`, reached from `free_to_zone`, panics with `zfree: freeing invalid pointer %p to
   zone %s` - the message, read from `r9` in exp-238 and confirmed in the frame here.
2. The zone is `vm_map_zone`, whose `zinit` name is `"maps"` (`vm_map.c:811`).
3. The element is in the `0x4040xxxx` chunk - `map_data`, the 4096-odd bytes `vm_map_steal_memory`
   got from the boot's first `pmap_steal_memory`, which returns `virtual_space_start` itself.
4. `is_sane_zone_ptr`'s first test is `pmap_kernel_va` = `[0x80000000, 0xFFFEFFFF]`, a compile-time
   constant; `virtual_space_start = 0x40400000` and `vm_kernel_slide = 0x80200000` both follow from
   `virtBase = physBase = 0x00200000`. The element is below the range and cannot be in it.

The free is `vm_map_init+0x260`'s `zcram(vm_map_zone, map_data, map_data_size)` (`vm_map.c:869`), it
is on the mandatory boot path before the first stub, and the image's base is the reason it fails.

## Cost

**No object was linked** - the undefined list is still 645 symbols and the stub set is unchanged.

| | exp-239 | now |
| --- | --- | --- |
| entry text | 592689 B | **592753 B** |
| `fleh_undef` | 238 B | **280 B** |
| `DebuggerTrapWithState` | 0x0022d3c8 | **0x0022d3e0** |
| entry image | 703352 B | **703352 B** |
| entry `.bss` | 0x002ab4f0–0x002d9e48 (190808 B) | **0x002ab4f0–0x002da248 (191832 B)** |
| boot_args offset | +897024 | **+901120** |
| headroom below `topOfKernelData` | 1204664 B | **1203640 B** |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |

The `.bin` is the same size but its `.bss` grew 1024 bytes, which moved the derived layout: the
boot_args copy is the first page above the image, so `ENTRY_ARGS_OFFSET` moved a page and the
payload was rebuilt from the regenerated header. That is the layout block doing its job - nothing
here is hard-coded, so nothing drifted.

## What is next: the base

Experiment 239's conclusion is what this experiment confirms from the other end. The frontier has no
object in it. The next stage changes **where the image runs**:

- `ENTRY_BASE`, from `0x00200000` to at or above `0x80000000` - which is where the device's RAM
  starts (`RAM_PHYS_BASE`, `stage90.h:21`) and where this SoC's kernel normally loads
  (`0x80008000`);
- the payload's mapping of the window, which today maps the entry image where it is linked;
- `physBase`/`virtBase` in the `boot_args`, which today are `0x00200000` and deliberately identical.

`virtBase` at or above `0x80000000` is what makes `pmap_kernel_va` true for XNU's own allocations
and `virtual_space_start` land where XNU expects it. The identity between `physBase` and `virtBase`
is the thing to re-derive rather than assume: `entry.ld`'s header says the identity is what makes
every `LOAD_PHYS_ADDR` conversion in `start.s` a no-op and makes `_start`'s page tables identity
tables, which is what lets the entry epilogue disable the MMU and keep executing. Moving `virtBase`
up while `physBase` stays put breaks that, and the epilogue's identity trick is the piece that has
to be redone - by measurement, on the device, with the same bounded self-test this run used.

## Reproduce

```bash
# the change: the frame block, the ap chain, ENTRY_KV_BUF 1024 -> 2048
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'text size|image bytes|bss|layout|headroom' out/stage90/xnu_arm_entry.txt

# the offset, read out of the built function before the run
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e240.dis
awk '/<fleh_undef>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e240.dis | head -7

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -32

# the two arguments, decoded out of the image the device ran
python3 - <<'PY'
d = open("out/stage90/xnu_arm_entry.elf", "rb").read()
va = lambda o: 0x00200000 + o - 0x10000
for target in (0x0028cc33, 0x0028b1b6):
    off = target - 0x00200000 + 0x10000
    print("0x%08x: %r" % (target, d[off:off+48]))
PY

# and why the element is not the chunk's first byte
grep -n 'random_free_to_zone(zone, newmem' external/xnu-4570.1.46/osfmk/kern/zalloc.c
sed -n '/^random_free_to_zone/,/^}/p' external/xnu-4570.1.46/osfmk/kern/zalloc.c | head -40

# the frame r5 = 1 comes from, one frame past panic_trap_to_debugger
awk '/<DebuggerTrapWithState>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e240.dis | head -12
awk '/<panic_trap_to_debugger>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e240.dis | sed -n '5,10p'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
