# Experiment 235 — No Stub at All, an Undefined Instruction, and the Two `udf`s in the Image

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt` (the run's own capture was not archived separately)

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
 xnu_entry_kv_written=0x00000000
```

No `stub_hit=` line — **the first run in this method that ended without one.** `failure_mask=0x00000000`
in every contract that reports one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned to
Android on its own.

The two lines above are the whole of what this run said about where it went. `xnu_entry_kv_written=0`
is exact and it is the important one: **the KV buffer was not touched at all.** `entry_stub_hit` writes
the symbol name into that buffer and then calls `entry_epilogue`, and `entry_epilogue` never returns,
so a run that ends with the buffer empty ended without reaching a stub. Every previous result in this
method carried a name, a length, and a path; this one carries nothing.

## What the entry image can say about an undefined instruction, and what it could not

`fleh_undef` is the undefined-instruction vector handler in `entry_stubs.c`. At the time of this run
it was:

```c
void fleh_undef(void) { entry_epilogue("exception: undefined instruction"); }
```

— the address of the faulting instruction was in `LR_undef`, four bytes past it, and the handler
printed the message and stopped without reading either. The next thing to do was therefore to make it
say where.

The first question was whether the fault was even XNU's. An ARM `udf` is a deliberate trap, and a
misaligned or undefined encoding produced by the compiler is also possible, so the image was searched
for the instruction:

```
$ arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep 'udf'
  22d1a8:	e7ffdefe 	udf	#65006	; 0xfdee
  22d418:	e7ffdefe 	udf	#65006	; 0xfdee
```

**Exactly two, and both are the same construct:**

```
  22d1a4:	bl	22d1e8 <DebuggerSaveState>
  22d1a8:	udf	#65006	; 0xfdee          <- inside DebuggerTrapWithState (0x22d180)
  22d1ac:	bl	208220 <current_processor>

  22d414:	bl	22d1e8 <DebuggerSaveState>
  22d418:	udf	#65006	; 0xfdee          <- inside DebuggerWithContext (0x22d348)
  22d41c:	bl	208220 <current_processor>
```

`DebuggerSaveState` then the trap is what `Debugger()` is. There is no third `udf` in a 703 KB image,
so whatever the run did, it did it to itself: **`exception: undefined instruction` here means XNU
entered the debugger**, and the two candidate call sites are `0x22d1a8` and `0x22d418` — until the
handler says which. That is the whole of experiment 236, and the comment written into `fleh_undef`
records the reason it was worth an experiment: *a trap named is a bug found; a trap unnamed is a run
spent guessing.*

## Cost

`out/xnu_kernel_obj/bsd_kern_subr_prf.o` (`bsd/kern/subr_prf.c`) — **1996 bytes of text, 4 of data,
68 of `.rodata.str1.1`, no `.bss` at all, 22 definitions, 20 references**. The smallest object taken on
since experiment 229's `libkern_gen_OSAtomicOperations.o`, which also had no `.bss`:

```
resolved (1):  snprintf

added  (7):    constty (B, storage)  proc_session  session_rele  tputchar
               tty_lock  tty_unlock  ttycheckoutq

639 -> 645 undefined          (639 + 7 - 1)
```

`639 + 7 - 1 = 645` exactly. Six of the seven arrive as function stubs and one as storage — `constty`
is `B 2d9ac0 4` in the image and the other six are `T ... c` — so the counters move `554 - 1 + 6 = 559`
functions and `85 + 1 = 86` storage. This is the first *increase* in the undefined count since
experiment 231, and the reason is mundane: `subr_prf.c` is the printf implementation, and a printf
implementation calls a tty.

| | exp-234 | now |
| --- | --- | --- |
| entry objects linked | 74 | 75 (`bsd_kern_subr_prf.o`) |
| entry text | 589329 B | **591505 B** |
| entry image | 686960 B | **703352 B** |
| entry `.bss` | 0x002a74e8–0x002d5d08 (190496 B) | 0x002ab4f0–0x002d9d48 (**190552 B**) |
| undefined | 639 | **645** |
| stubs | 554 functions, 85 storage | **559 functions, 86 storage** |
| boot_args offset | +880640 | **+897024** |
| headroom below `topOfKernelData` | 1221368 B | **1204920 B** |
| payload text | 1179146 B | **1195538 B** |

The entry image grew 16392 bytes while its `.text` grew 2176 and its `.bss` grew 56, and the payload
grew by exactly the same 16392 — the payload embeds the entry image, so the two moving together by the
byte is a check that they are measuring the same thing. The remaining 14160 bytes of the image's growth
are not text and were not measured; the term is recorded rather than explained, and nothing in this
document rests on it. What is measured is that the same 16392 appears in both columns.

## Why the step was taken at all

The prediction written into `build_entry.sh` before the build was `kmem_alloc_kobject`, deferred from
experiment 234 — "the call the last run stopped twenty bytes before". The run never got there. But the
step was not wasted and the object was not the wrong one: the frontier at the time was `snprintf`, and
`snprintf` is defined by `subr_prf.c`. Linking the object that defines the symbol the last run named is
the method, and the method was followed; what changed is only that the run stopped a second earlier,
on a trap rather than on a stub.

## What is next: nothing in the frontier, until the trap is named

No `stub_hit` means the frontier method cannot advance. There is no symbol to link: the run did not
stop at an unprovided function, it stopped at an instruction the CPU refused, and until that
instruction is located and its cause understood, linking more objects would be guessing.

The step that follows is therefore not an object but an instrument: **make `fleh_undef` report
`LR_undef` and `SPSR`.** The vector trampoline branches rather than calls, so `LR_undef` survives
untouched from the trapping instruction and holds the return address — the trap's own address plus
four. Two keys, `xnu_entry_undef_lr` and `xnu_entry_undef_spsr`, turn "an undefined instruction
happened" into "an undefined instruction happened at this address, in this processor mode", and the
image has only two candidate addresses to match it against.

## Reproduce

```bash
# the step: 1 resolved, 7 added, 639 -> 645, stubs 554fn/85st -> 559fn/86st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_KERN_SUBR_PRF_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/PA.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/PB.txt
comm -23 <(LC_ALL=C sort -u /tmp/PA.txt) <(LC_ALL=C sort -u /tmp/PB.txt)   # snprintf
comm -13 <(LC_ALL=C sort -u /tmp/PA.txt) <(LC_ALL=C sort -u /tmp/PB.txt)   # the seven
wc -l /tmp/PA.txt /tmp/PB.txt                                              # 639 and 645

# ... and it ran, and there is no stub_hit line
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14

# the two instructions the image can trap on, and where they are
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -B1 'udf'
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -E 'DebuggerTrapWithState|DebuggerWithContext'

# the object's own shape
arm-none-eabi-size -A out/xnu_kernel_obj/bsd_kern_subr_prf.o
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w constty      # B, 4 bytes
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
