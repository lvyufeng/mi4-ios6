# Experiment 236 — the Trap Named: XNU Panicked, and the Message Is in DRAM

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`fleh_undef` in `stages/stage90/xnu_arm_boot/entry_stubs.c` read nothing. It now reads two registers
and reports three values:

```c
void fleh_undef(void)
{
    uint32_t lr_undef, spsr;

    __asm__ volatile ("mov %0, lr" : "=r"(lr_undef));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));

    entry_kv("xnu_entry_undef_lr", lr_undef);
    entry_kv("xnu_entry_undef_pc", lr_undef - 4);
    entry_kv("xnu_entry_undef_spsr", spsr);
    entry_epilogue("exception: undefined instruction");
}
```

The vector trampoline branches rather than calls, so `LR_undef` survives untouched from the trapping
instruction and holds its address plus four; `SPSR` holds the processor state the trap happened in.
`xnu_entry_undef_pc` is `lr_undef - 4` rather than a fourth register read because that is the address
that matches the disassembly, and the disassembly is what the answer has to be compared against.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
 xnu_entry_kv_written=0x0000005f
 xnu_entry_kv_in_dram=0x0000005f
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_undef_lr=0x0022d1ac
 xnu_entry_undef_pc=0x0022d1a8
 xnu_entry_undef_spsr=0x60000093

No errors detected
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x5f = 95`. Still no
`stub_hit=` line.

## The address is the `udf` in `DebuggerTrapWithState`

```
$ arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -E 'DebuggerTrapWithState|DebuggerWithContext'
DebuggerTrapWithState T 22d180 68
DebuggerWithContext   T 22d348 184

$ sed -n '/  22d1a4:/,/  22d1ac:/p' entry.dis
  22d1a4:	bl	22d1e8 <DebuggerSaveState>
  22d1a8:	udf	#65006	; 0xfdee          <- xnu_entry_undef_pc
  22d1ac:	bl	208220 <current_processor>   <- xnu_entry_undef_lr
```

`0x0022d1a8` is the first of the image's two `udf #65006` instructions, `LR_undef` is the instruction
after it, and both match to the byte. This is the `DebuggerTrapWithState` arm of the pair that
experiment 235 found and could not tell apart.

**`DebuggerTrapWithState` has exactly one caller and it has exactly three.**

```
$ grep -n 'bl\s*22d180' entry.dis      # DebuggerTrapWithState
  22dbb4:	bl	22d180 <DebuggerTrapWithState>      # in panic_trap_to_debugger

$ grep -n 'bl\s*22d9f4' entry.dis      # panic_trap_to_debugger
  22d308:	bl	22d9f4 <panic_trap_to_debugger>     # in panic
  22dbec:	bl	22d9f4 <panic_trap_to_debugger>     # in panic_with_options
  22dc34:	bl	22d9f4 <panic_trap_to_debugger>     # in panic_context
```

So the run called `panic()` — or `panic_with_options()` or `panic_context()`, which share the path.
**XNU reached a fatal condition and took its own panic path, on the device, in real code.** Every
previous result in this sequence recorded which symbol the image could not provide; this one records
that XNU ran far enough to make a judgement about its own state.

`xnu_entry_undef_spsr=0x60000093` corroborates the reading without being needed for it:

```
  bits 31..28  0110    N=0 Z=1 C=1 V=0     - flags from the last comparison
  bit  24      J=0     ARM state, not Thumb
  bit  7       I=1     IRQ masked
  bit  6       F=0     FIQ enabled
  bit  8       A=0     asynchronous abort not masked
  bits 4..0    0x13    Supervisor mode
```

Supervisor mode, IRQ masked, ARM state — a normally-running kernel that decided to panic. Not an
abort, not a fault, not a stray branch into an alignment hole.

## No stub_hit means the frontier method cannot advance

There is no symbol to link. The run did not stop at an unprovided function that a future step could
provide; it stopped at an instruction XNU chose to execute. Linking more objects would be guessing,
and the next step has to be an instrument that reads what XNU said before it panicked.

**The message is a pointer, and it is an argument to the function the trap is inside.** `panic()`
itself is a 72-byte wrapper — it builds a `va_list` on the stack and calls `panic_trap_to_debugger`,
and prints nothing:

```
0022d2d4 <panic>:
  22d2e4:	stm	ip, {r1, r2, r3}              # the varargs
  22d304:	add	r1, sp, #16
  22d308:	bl	22d9f4 <panic_trap_to_debugger>
  22d318:	bx	lr
```

and `panic_trap_to_debugger`'s first parameter is the format string (`osfmk/kern/debug.c:559`):

```c
void
panic_trap_to_debugger(const char *panic_format_str, va_list *panic_args, unsigned int reason,
        void *ctx, uint64_t panic_options_mask, unsigned long panic_caller)
```

which it passes on, because `DebuggerTrapWithState` — the function the trap is inside — is declared
(`osfmk/kern/debug.h:560`) as taking it as its third argument:

```c
kern_return_t DebuggerTrapWithState(debugger_op db_op, const char *db_message,
        const char *db_panic_str, va_list *db_panic_args,
        uint64_t db_panic_options, boolean_t db_proceed_on_sync_failure,
        unsigned long db_panic_caller);
```

**So the panic string pointer is in the trap's own frame.** And it is cached as a global too:
`debugger_panic_str` is a real `B` object in this image at `0x002b51ac`, read on every character by
`putchar`, which clears `constty` while a panic is in progress. The caveat to carry in is that
`debugger_panic_str` is only the *cache*: `handle_debugger_trap` restores it to `NULL` on its way out
(`debug.c:947`) and it runs before `DebuggerTrapWithState`, so by the trap the global may be clear and
the frame argument is the one that survives.

**The message printing is real, and the stub inside its output path was not taken.** The callback is
`putchar` — `bsd/kern/subr_prf.c`'s `static void putchar(int c, void *arg)`, linked by experiment 235
and real code here. Its source dispatches on a flag word to five places, and the disassembly matches:

```
$ awk '/<putchar>:/{f=1} f&&/^$/{exit} f{print}' entry.dis | grep -E 'bl\t|blx\t'
  27d81c:	bl	22cc98 <log_putc>           # TOLOG        - into XNU's log buffer
  27d840:	bl	22c8d0 <log_putc_locked>    # TOLOGLOCKED  - the same buffer, locked
  27d870:	blx	r1                          # TOCONS       - (*v_putc)(c)
  27d884:	strb	r4, [r0]                    # TOSTR        - into a caller's buffer
  27d89c:	bl	27f414 <tputchar>           # TOTTY        - a 12-byte STUB in this image
```

`log_putc` and `log_putc_locked` are real; the `TOTTY` branch calls `tputchar`, one of the seven stubs
experiment 235 added. The run did not stop at `tputchar`, which admits two readings and this
experiment does not distinguish them: either the message was printed before the trap with `TOTTY`
clear, or the printing happens *inside* the debugger, after the `udf` — which is where
`debugger_collect_diagnostics` calls `_doprnt(debugger_panic_str, debugger_panic_args, consdebug_putc, 0)`
(`debug.c:743`), through `consdebug_putc` rather than `putchar`. Either way the message is produced
after the trap in the second reading and before it in the first, and in both the bytes go somewhere
in DRAM.

## Cost

**No object was linked.** The change is 128 bytes of entry-image `.text` and nothing else:

| | exp-235 | now |
| --- | --- | --- |
| entry objects linked | 75 | 75 |
| entry text | 591505 B | **591633 B** |
| entry image | 703352 B | **703352 B** |
| entry `.bss` | 0x002ab4f0–0x002d9d48 (190552 B) | unchanged |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |
| boot_args offset | +897024 | **unchanged** |
| headroom below `topOfKernelData` | 1204920 B | **1204920 B** |
| payload text | 1195538 B | **1195538 B** |

The image is byte-for-byte the same size in both directions, and the payload text is identical,
because four `entry_kv` calls and two register moves fit inside padding the linker had already
reserved. This is the first experiment in the sequence that cost no bytes of image at all.

## What is next: read the panic message

Three routes, and the third is the one to try first because it needs no new subsystem:

1. **The string pointer, from the trap frame.** `DebuggerTrapWithState`'s third parameter is
   `db_panic_str`, so it is in the trap's arguments, and `debugger_panic_str` (`0x002b51ac`) caches it
   as a global that `putchar` reads. An `fleh_undef` that reports `debugger_panic_str`, and then the
   first words of whatever it points at, names the condition in one run. The caveat to carry in is
   that `handle_debugger_trap` restores the global to `NULL` on its way out (`debug.c:947`) and it
   runs before `DebuggerTrapWithState`, so the cached global may be clear by the trap — which is why
   the frame argument and the per-CPU debugger context are the fallbacks to read in the same run.

2. **The log buffer.** XNU's `log_putc` writes into the log buffer in DRAM, which this payload's memory
   map already covers. Reading its head/tail indices, or checksumming a window of it, answers "was the
   message written, and how long is it" without the console path at all — and unlike the pointer, it
   cannot have been cleared, because a log entry is copied in rather than pointed at.

3. **The three call paths.** `panic_with_options` and `panic_context` are distinct functions from
   `panic`, and they exist to carry a file and line. If the string identifies the condition but not
   the call site, disassembling which of the three was entered is the next question, and the answer is
   in their arguments rather than their addresses.

Until one of those names the condition, the frontier is `panic` and the method has nothing to link.

## Reproduce

```bash
# the change: 128 bytes of entry text, nothing else
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'text size|image bytes|stubs:' out/stage90/xnu_arm_entry.txt

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -16
#   ... xnu_entry_undef_pc=0x0022d1a8 xnu_entry_undef_spsr=0x60000093

# the address, and the only path that reaches it
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e236.dis
grep -n 'udf' /tmp/e236.dis                                     # two, this one first
grep -n 'bl	22d180' /tmp/e236.dis                            # DebuggerTrapWithState's only caller
grep -n 'bl	22d9f4' /tmp/e236.dis                            # its three

# the printf path, and the stub inside it
awk '/<putchar>:/{f=1} f&&/^$/{exit} f{print}' /tmp/e236.dis | grep -E 'bl\t|blx\t'
python3 tools/xnu_entry_callwalk.py --elf out/stage90/xnu_arm_entry.elf --list-stubs | grep tputchar
grep -n 'static void putchar' -A 26 external/xnu-4570.1.46/bsd/kern/subr_prf.c      # TOCONS/TOTTY/TOLOG/...
grep -n 'DebuggerTrapWithState' external/xnu-4570.1.46/osfmk/kern/debug.h           # db_panic_str is arg 3
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w debugger_panic_str
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
