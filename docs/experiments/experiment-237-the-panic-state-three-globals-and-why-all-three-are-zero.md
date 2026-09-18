# Experiment 237 — the Panic State, Three Globals, and Why All Three Are Zero

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: not archived separately; the values below are the run's own output

## The change

Experiment 236 ended with the trap located — `xnu_entry_undef_pc=0x0022d1a8` is the `udf` inside
`DebuggerTrapWithState`, which is only reachable through `panic_trap_to_debugger`, so XNU panicked —
and with three candidate places the message could be. The first is XNU's own panic state, which is
cached in three globals rather than only passed as an argument:

```c
extern const char *debugger_panic_str;
extern const char *debugger_message;
extern unsigned long debugger_panic_caller;
```

They are `B` objects defined by `osfmk_kern_debug.o`, which is already linked, so declaring them
costs nothing and cannot go stale the way a hard-coded address could. `entry_stubs.c` grew three
reads and three keys, and `ENTRY_KV_BUF` went from 768 to 1024 because the run's key list had
outgrown the buffer:

```c
    entry_kv("xnu_entry_panic_str", (uint32_t)(uintptr_t)debugger_panic_str);
    entry_kv("xnu_entry_panic_caller", (uint32_t)debugger_panic_caller);
    entry_kv("xnu_entry_panic_message", (uint32_t)(uintptr_t)debugger_message);
```

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_undef_lr=0x0022d26c
 xnu_entry_undef_pc=0x0022d268
 xnu_entry_panic_str=0x00000000
 xnu_entry_panic_caller=0x00000000
 xnu_entry_panic_message=0x00000000
```

`failure_mask=0x00000000` in every contract that reports one, `safety_boundary_preserved=0x00000001`,
`mmu_unchanged=0x00000001`, `persistent_write_attempted=0x00000000` in all 25 contracts that report
it, and the device returned to Android on its own. Still no `stub_hit=` line: the trap is what ends
the run.

**All three are zero, and that is the answer the caveat in experiment 236 predicted.** The trap
address moved `0x0022d1a8 → 0x0022d268`, twenty-eight bytes into `DebuggerTrapWithState` later and
nothing else about the run changed, so the same panic is being read in the same place.

## Why zero, in the trace the run actually took

The globals are set, and then they are cleared, before the trap:

```
debug.c:905   debugger_panic_str  = <the format string>;   <- set
debug.c:947   debugger_panic_str  = NULL;                  <- restored
```

Both of those are inside `handle_debugger_trap`, which runs **before** `DebuggerTrapWithState` — it
is the function that decides whether to enter the debugger at all. So by the time the `udf` fires
the cache is clear, every time, and a non-zero reading here would have been the surprise rather than
the answer.

The frame argument is the one that survives, and the image says so directly. `DebuggerTrapWithState`
is declared (`osfmk/kern/debug.h:560`) as taking the panic string as its third parameter, and the
call site passes it:

```
$ grep -n 'bl\t22d180 <DebuggerTrapWithState>' entry.dis
  22dbb4:	bl	22d180 <DebuggerTrapWithState>      # in panic_trap_to_debugger
```

whose argument setup at `panic_trap_to_debugger+0x19c` loads `0x00288257` into the `db_message`
slot. Read straight out of the ELF, `0x00288257` is the string **`"panic"`** — which is exactly what
`handle_debugger_trap` is told the debugger op is, and it is not the message. The message is the
first argument, which the same function has already moved into `r9` (experiment 238 reads it there).

## Cost

**No object was linked** — the undefined list is the same 645 symbols as experiment 236, so the stub
set is the same set. What changed is `entry_stubs.c`'s own size, and the run measures it exactly:
the trap address is `DebuggerTrapWithState + 0x28` in both experiments, so the distance the symbol
moved *is* the growth of everything the linker placed before it, and only `fleh_undef` changed.

| | exp-236 | now |
| --- | --- | --- |
| `DebuggerTrapWithState` | 0x0022d180 | **0x0022d268** |
| `fleh_undef` growth | — | **+192 B** of `.text` |
| entry objects linked | 75 | 75 |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |
| entry `.bin` | 703352 B | **703352 B** |

The image file did not grow at all: the added text lands in the alignment gap between the end of
`.text` and the start of `.data`, which is 15808 bytes wide.

## What is next: the registers, because the globals are clear

The panic string is in `r9` when the trap fires. `panic_trap_to_debugger`'s first four arguments are
moved into callee-saved registers at `+0x10` and stay there — `r9 = panic_format_str`, `r8 = args`,
`r7 = reason`, `r6 = ctx` — and `DebuggerSaveState` pushes and restores `r4`–`r9` and `sl` without
touching any of them, so the vector trampoline lands in `fleh_undef` with `r9` intact. That is
experiment 238.

## Reproduce

```bash
# the change: three externs, three reads, ENTRY_KV_BUF 768 -> 1024
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
wc -l out/stage90/xnu_arm_entry_undef.txt                      # 645, unchanged

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#    ... xnu_entry_panic_str=0x00000000 xnu_entry_panic_caller=0x00000000

# the address moved by exactly the text added, and nothing else did
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w DebuggerTrapWithState

# the frame argument, and the string the *other* one points at
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e237.dis
grep -n 'bl\t22d180 <DebuggerTrapWithState>' /tmp/e236.dis
sed -n '900,950p' external/xnu-4570.1.46/osfmk/kern/debug.c      # set, then restored to NULL
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
