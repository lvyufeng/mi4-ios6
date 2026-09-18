# Experiment 290 — `osfmk_arm_machine_task.o`, an empty function, and name strings that live in `.text`

**Step:** link the object that defines `machine_task_init` — `osfmk/arm/machine_task.c`,
`osfmk_arm_machine_task.o` — the name the 289 run stopped at. The smallest step the walk has taken in
a hundred experiments: 324 bytes of `.text` for five functions.
**Prediction:** that the step overshoots, because `machine_task_init`'s whole body is empty — the
frontier moves to the next stub call in `task_create_internal`, `ipc_task_init`, caller
`task_create_internal + 0x224`.
**Result:** exactly that, with all three stub counts and the image size exact —
**`stub_hit=ipc_task_init` at `xnu_entry_stub_caller=0x800bfde8`**.

## The object is empty in the only place that matters

```
324 bytes of .text for five functions, five definitions, six references.

resolved   5   machine_task_set_state   machine_task_get_state   machine_task_init
               machine_task_terminate   machine_thread_inherit_taskwide
               - all five already *stubs* in this image, so the object retires all five
added      2   ads_zone            (osfmk/arm/pcb.c:58   - storage, 4 bytes)
               copy_debug_state    (osfmk/arm/pcb.c:361  - function)
```

`machine_task_init` sits at object offset 0x140 — the **last four bytes** of that `.text`, because its
body is `machine_task.c:171-175`: three `__unused` parameters and not one statement.

```c
void
machine_task_init(__unused task_t new_task,
		  __unused task_t parent_task,
		  __unused boolean_t memory_inherit)
{
}
```

A `bx lr` and nothing else, which is why this step cannot stop on anything of its own.

Three of the six references (`bzero`, `zalloc`, `zfree`) are real, and the fourth
(`machine_thread_set_state`) is already a stub — so under `under = (stub list ∪ undefined list)`, the
rule 288 had to correct, it is not new. **Both added names come from the same object**,
`osfmk/arm/pcb.c`, which is not linked: this step obliges its own successor, and the ledger says so
before the next step discovers it. `ads_zone` is a storage stand-in — four zero bytes standing for a
pointer-sized `zone_t` — and what kind of value it is matters later: `machine_task_set_state` and
`machine_task_terminate` pass it to `zalloc` and `zfree`. Nothing on this boot's path calls either, so
the object can wait; the first `task_terminate` cannot.

## The prediction is a place, not a name

The call this step makes real returns immediately, so the frontier is the next stub call in
`task_create_internal`, whose stub calls 289's ledger had already listed in address order. The eight
instructions between the return and the next call, read out of the linked image, contain no `bl`:

```
800bfdc4  add r0, r4, #504   800bfdc8  mov r5, #0           800bfdcc  str r0, [r4,#504]
800bfdd0  mov r1, sl         800bfdd4  str r0, [r4,#508]    800bfdd8  mov r0, r4
800bfddc  str r5, [r4,#512]  800bfde0  str r5, [r4,#524]    800bfde4  bl ipc_task_init
```

The falsifier was named before the build: a report naming any of the five names this object defines
would mean `machine_task_init` faulted or called something — impossible for a `bx lr`, so it would be
a statement about the stub generator or the pad rather than about the step.

## The build

```
                      predicted        measured
undefined             864              864
function stubs        770              770
storage               94               94
image                 1198056          1198056
__bss_start           0x80123c08       0x80123c08
text                  1082652          1082456
```

All three counts, `__bss_start`, and the **unchanged image size** exact — the first step in three
where `.data` did not move, as predicted (324 bytes of read-only content is well inside the slack).

Text is where the prediction is instructive: **+128, not the +324 the object's own sizes suggest.**
`entry.ld:46` places `*(.rodata .rodata.*)` **inside the `.text` output section**, and the generated
stub object's symbol-name strings live there. This step retires five stub bodies (5 × 0x18 = 0x78) and
five symbol *names*, and adds one of each plus `ads_zone`'s name — so the object's 324 bytes arrive net
of a name-string region that also shrank. The ledger records the measured 128 rather than a byte split
that does not close exactly; the general rule it yields is what matters: **`text size` grows by the
object's `.text` plus or minus the names entering and leaving the stub object, so a name-retiring step
always undershoots a body-only prediction.**

bss end 0x8015a3c8 → 0x8015a408, headroom 1727544 → 1727480.

## The pad moved down, which is why the derived check exists

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x8000244c and 0x80002450 (ResetHandlerData - ExceptionLowVectorsBase = 0x2448),
and both land inside what it skips
```

0x2448 against 289's 0x24c0 — **down 0x78, the first time the value has moved down.** The interval can
be read exactly rather than described: `nm` says **386 symbols at 0x18 bytes apart** sit between
`ExceptionLowVectorsBase` (0x800ecbf4) and `ResetHandlerData` (0x800ef03c) — the interval *is* the
generated stub object, so it shrinks when stubs are retired, exactly as it grew when they were added.
The values printed across the experiments that have printed one — 0x2404 (281), 0x24a8 (288), 0x24c0
(289), 0x2448 (290) — all land inside a pad spanning 0x23d4 … 0x25d4.

**This is the point of 288's fix.** A comparison against the literals 0x2404/0x2408 would have failed
the build at 288 or been edited to 0x24c0 and then failed here; the derived form simply reports the new
number and keeps checking. And the check has a floor as well as a ceiling: if a future step retires
enough stubs to push the writes below 0x800023d4, the build stops, and the fix then is to **move** the
pad rather than widen it.

## The run

```
stub_hit=ipc_task_init        xnu_entry_stub_caller=0x800bfde8
```

`0x800bfde8` is `task_create_internal + 0x224` — the return address of the `bl ipc_task_init` at
0x800bfde4, predicted before the device was touched. The image also shows this step's own effect
directly: `machine_task_init` was a **stub** at 0x800ede1c in the 289 image and is **real** at
0x800c8c3c in this one.

So one run measured that the empty function was entered and returned, that nothing between the two
calls in `task_create_internal` can stop, and that the frontier is the second of the ten stub calls
that function makes.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301114 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 291 — `osfmk/kern/ipc_tt.c` (`osfmk_kern_ipc_tt.o`) for `ipc_task_init`:
**12676 bytes of `.text`, 60 definitions, 53 references**, the largest step since 288, and one that
should retire a block of `ipc_*` stubs rather than one. `osfmk/arm/pcb.c`'s two names are still
waiting, but nothing on this path calls the functions that use them.
