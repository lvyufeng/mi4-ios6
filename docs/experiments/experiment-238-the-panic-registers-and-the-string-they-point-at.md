# Experiment 238 — the Panic Registers, and the String They Point At

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: not archived separately; the values below are the run's own output

## The change

Experiment 237 read the three panic globals and all three were zero, which is what
`handle_debugger_trap`'s restore predicts. So the message has to be read where it cannot have been
cleared — **the registers**, which the vector trampoline does not touch: it loads `SP` from a literal
and branches to the handler without pushing anything, so the handler's first statement runs with the
trapping instruction's registers intact.

Not `r0`–`r3`: `panic_trap_to_debugger` moves its first four arguments into callee-saved registers
at `+0x10` and they stay there, and `DebuggerSaveState` pushes and restores `r4`–`r9` and `sl`
without writing any of them, so `r8`, `r9` and `r10` survive both the call and the trap:

```
  r9   = panic_format_str  - the panic message
  r8   = panic_args        - what `panic()` passed as `va_list *panic_args`
  r10  = sl                - panic_options_mask
```

*(Corrected by experiment 239: `sl` and `r4` are the two halves of the 64-bit `panic_options_mask`,
and the caller is `r5` — `stm sp, {r4, sl}` at the `DebuggerTrapWithState` call and `str r5, [sp,
#12]`. The reading below is unaffected: `sl` and `r4` are both zero because those are the two halves
of a zero mask.)*

`fleh_undef` grew three register reads, three keys, and a six-word dump of the format string:

```c
    __asm__ volatile ("mov %0, r9" : "=r"(r_fmt));
    __asm__ volatile ("mov %0, r8" : "=r"(r_args));
    __asm__ volatile ("mov %0, r10" : "=r"(r_caller));

    entry_kv("xnu_entry_trap_r9_fmt", r_fmt);
    entry_kv("xnu_entry_trap_r8_args", r_args);
    entry_kv("xnu_entry_trap_sl_caller", r_caller);
```

**`r4` was tried and removed.** The first version read `r4` as `panic_options_mask`; the disassembly
of the result showed `fleh_undef`'s own prologue emitting `mov r4, lr` before the first statement,
clobbering the original value, so the read was reporting the function's own saved `lr`. The key was
deleted rather than kept with a caveat.

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_undef_pc=0x0022d2c8
 xnu_entry_trap_r9_fmt=0x0028ca03
 xnu_entry_trap_r8_args=0x0029be88
 xnu_entry_trap_sl_caller=0x00000000
 xnu_entry_fmt_w0=0x72667a22
 xnu_entry_fmt_w1=0x203a6565
 xnu_entry_fmt_w2=0x65657266
 xnu_entry_fmt_w3=0x20676e69
 xnu_entry_fmt_w4=0x61766e69
 xnu_entry_fmt_w5=0x2064696c
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own.

The six words are the message, little-endian, and it is twelve characters of it:

```
  22 7a 66 72   "zfr"
  65 65 3a 20   "ee: "
  66 72 65 65   "free"
  69 6e 67 20   "ing "
  69 6e 76 61   "inva"
  6c 69 64 20   "lid "
  ->  "zfree: freeing invalid "
```

## The condition is `zalloc.c:1208`

```
$ sed -n '1205,1210p' external/xnu-4570.1.46/osfmk/kern/zalloc.c
	if (__improbable(!is_sane_zone_element(zone, element)))
		panic("zfree: freeing invalid pointer %p to zone %s\n",
		      (void *) element, zone->zone_name);
```

`free_to_zone`, the function that adds an element to a zone's free list, was handed an element that
its own sanity check rejected. The image has exactly three callers of `free_to_zone` —
`zcram+0x3a0`, `zcram+0x444` and `zfree+0x50c` — so the panic is one of those, and `r8` in the same
run says which arguments it was given.

`r10 = 0` corroborates the register reading rather than being needed for it: `panic_trap_to_debugger`
puts `panic_options_mask` in `sl`, and zero is `panic()`'s options mask. `r8 = 0x0029be88` is a
16 KB `intstack` address (`intstack D 298000`, `intstack_top D 29c000`), i.e. `panic`'s own frame on
the boot stack, which is what a `va_list` in a live caller should be.

## Cost

**No object was linked** — the undefined list is still the same 645 symbols. The trap address moved
`0x0022d268 → 0x0022d2c8`, so `fleh_undef` grew **96 bytes**; the image is unchanged otherwise.

| | exp-237 | now |
| --- | --- | --- |
| `DebuggerTrapWithState` | 0x0022d268 | **0x0022d2c8** |
| `fleh_undef` growth | — | **+96 B** of `.text` |
| entry objects linked | 75 | 75 |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |
| entry `.bin` | 703352 B | **703352 B** |

## What is next: the arguments, and the question they answer

`zfree: freeing invalid pointer %p to zone %s` names two things: the element and the zone. Both are
in the `va_list` `r8` points at, so one more experiment reads them — **and one dereference further
in than this experiment first read them**: `r8` is a `va_list *`, so the word at it is the list's
`__ap` and not the first argument. Experiment 239 measured exactly that, twelve bytes into `panic`'s
own frame. — and one more number on the side: `zone_map_min_address` and `zone_map_max_address`, the two globals
`is_sane_zone_ptr` compares the element against. Experiment 239 is that run, and the reason it
matters is that a check that cannot pass is a different finding from a check that correctly failed.

## Reproduce

```bash
# the change: three register reads and a six-word dump
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -n 'xnu_entry_trap_r9_fmt' stages/stage90/xnu_arm_boot/entry_stubs.c

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20

# the string, and the condition that printed it
python3 - <<'PY'
w = [0x72667a22, 0x203a6565, 0x65657266, 0x20676e69, 0x61766e69, 0x2064696c]
print("".join(chr(b) for x in w for b in x.to_bytes(4, "little")))
PY
grep -n 'zfree: freeing invalid' external/xnu-4570.1.46/osfmk/kern/zalloc.c

# every caller of free_to_zone in the image
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e238.dis
grep -nE 'bl\t[0-9a-f]+ <free_to_zone>' /tmp/e238.dis

# the stack the va_list lives on
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -wE 'intstack|intstack_top'
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
