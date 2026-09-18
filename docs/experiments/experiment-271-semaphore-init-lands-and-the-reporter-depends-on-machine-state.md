# Experiment 271 — `semaphore_init` lands, and the reporter is wrong in a way that depends on the machine state

**Step:** link `osfmk/kern/sync_sema.o` into the entry image.
**Prediction:** `stub_hit=mk_timer_init`, at `ipc_bootstrap+0x190`.
**Result:** `stub_hit=mk_timer_init`, `xnu_entry_stub_caller=0x800ac0b4` = `ipc_bootstrap+0x190` — the
fourteenth prediction in a row. And the same run is the sharpest measurement this project has made of
its own reporting path: **`entry_kv` writes wrong characters during the run and right ones in the
epilogue, from the same instruction, in the same image, for the same value.**

## The change

One object. `osfmk/kern/sync_sema.c` (manifest:587), `osfmk_kern_sync_sema.o` — 4567 bytes of text,
no data, 12 of bss, 32 definitions and 36 references. **5 resolved, 3 added.**

The five are `semaphore_init`, `semaphore_create`, `semaphore_destroy`, `kdp_sema_find_owner` and —
the interesting one — **`semaphore_max`**, which the image had been carrying as a storage stand-in.
The three added (`port_name_to_semaphore`, `port_name_to_thread`, `thread_syscall_return`) are the
syscall surface, none of them on the init path.

| | before (270) | after |
| --- | --- | --- |
| undefined | 846 | 844 |
| function stubs | 770 | 769 |
| storage stand-ins | 76 | 75 |
| text | 930244 | 935012 |
| image bytes | 1032048 | 1048432 |
| bss end | 0x801306c8 | 0x801346c8 |
| args | +1253376 | +1269760 |
| headroom | 1898808 | 1882424 |
| payload bytes | 1526504 | 1542888 |

`semaphore_init` is three instructions of substance and two calls, both real since 250:

```
ldr semaphore_max ; mov r0, #64 ; mov r2, #64 ; lsl r1, r,max, #6 ; bl zinit
str -> semaphore_zone ; bl zone_change(Z_NOENCRYPT, TRUE)
```

## `semaphore_max` is not a constant, and it is not zero

`sync_sema.c:67` declares `unsigned int semaphore_max;` with no initializer, so this object
contributes a `.bss` variable holding zero — and linking it changes nothing about the *value*, since
the image's stand-in was zero-initialized too. What makes the value non-zero is a **writer**:
`scale_setup()` in `osfmk/kern/startup.c`, which `kernel_bootstrap` calls as its first substantive
act, long before `ipc_bootstrap`, and which ends with

```c
	ipc_space_max = SPACE_MAX;   ipc_port_max  = PORT_MAX;
	ipc_pset_max  = SET_MAX;     semaphore_max = SEMAPHORE_MAX;
```

It is real code in this image — `scale_setup` is a function of its own at 0x8000e004, and its last
store is `str r0, [r1]` with `r1 = 0x8012f780`, which `nm` on the image says is `semaphore_max`. So
the read in `semaphore_init` sees `PORT_MAX >> 1`, computed a moment earlier by that same function —
**and `PORT_MAX` is `task_max * 3 + thread_max * 3 + 40000`, which is `task_max` again.** With the
stand-in at zero that is 44608 and `semaphore_max` is 22304; with the real 512 it would be 46144 and
23072.

That is the **fourth** consumer of the same zero stand-in, after 269's voucher zone, 270's two
importance zones, and the three `*_max` globals here — and it also means this step asks `zinit` for a
**1.4 MB zone** (22304 × 64), the largest single `max_mem` any of these steps has requested.

## The report is wrong, and the measurement says why

`entry_stub_hit` writes the caller through `entry_kv` and the epilogue prints it. This run's record
came out as

```
 xnu_entry_stub_caller=0x800:<0;4
```

where the prediction is `0x800ac0b4`. The four wrong characters are not noise, and they are not
random: `entry_kv` writes a non-decimal digit as `'a' + (d - 10)` = **0x57 + d**, and a decimal one
as `'0' + d` = **0x30 + d**. `0x30 + 0xa = 0x3a` `':'`, `0x30 + 0xc = 0x3c` `'<'`,
`0x30 + 0xb = 0x3b` `';'` — exactly the three characters observed. So the string says the
*conditional* add took the decimal branch for every non-decimal nibble, and **the true value is
recoverable from the corruption**: `:<;` decodes to a, c, b, which is `0x800ac0b4` exactly. The
prediction is confirmed by the mangled string as well as by the correct one.

The compiled code cannot do that, and that is checkable rather than arguable. The whole image holds
exactly **one** `add rN, rN, #87` — there is no second copy of the loop — and the bytes in the ELF
are the ones the source says:

```
8000211c: e3530009   cmp  r3, #9
80002120: e2834057   add  r4, r3, #87      ; 0x57
80002124: 92834030   addls r4, r3, #48     ; 0x30, taken only when r3 <= 9
```

So the run measured a discrepancy between what the image contains and what the CPU did with it.
Rather than argue about the cause, the step made the value travel **three roads** and printed all
three. The next run:

```
 xnu_entry_stub_caller_v=0x800ac0b4          value, via entry_write_kv (a .rodata table read)
 xnu_entry_stub_caller_digits=0x00000032     where the digits are in g_kv_buf
 xnu_entry_stub_caller_w0=0x3c3a3030         the bytes entry_kv stored: '0','0',':','<'
 xnu_entry_stub_caller_w1=0x0a343b30         ... '0',';','4','\n'
...
 xnu_entry_stub_caller=0x800:<0;4            written by entry_kv during the run
 xnu_entry_stub_caller_a=0x800:<0;4          the same value, same call site, one call later
 xnu_entry_stub_caller_e=0x800ac0b4          the same value, written by entry_kv from the epilogue
```

Three things follow, and each is a measurement rather than an inference:

1. **The bytes in `g_kv_buf` really are biased** (`w0`/`w1` read the buffer, not the log). The
   transfer to the ram console is not the culprit — and neither is the value.
2. **Two calls in a row in the same machine state give the same wrong answer.** It is not a
   per-invocation hazard; it is systematic for that state.
3. **The same instruction, at the same address, in the same image, gives the right answer from the
   epilogue.** The only difference is the machine state: during the run the entry image executes
   with XNU's caches on and XNU's page tables live; from the epilogue it executes with SCTLR.C and
   SCTLR.I clear and the mmu off, where every fetch and store goes straight to memory.

The epilogue's copy is a real call to the same `entry_kv` (0x80002024, `bl entry_kv` at 0x800024ac),
not an inlined second copy — which the `#87` count settles independently.

**So the observable is: instructions fetched from the entry window are not executed as the memory
says, while they are executed as the memory says when the I-cache is off.** On ARMv7-A that is the
documented consequence of the memory type: instruction fetches from Strongly-ordered or Device
memory are unpredictable, and this project's configuration is `SO_ONLY`. The entry image also
executes with SCTLR.I set after `_start`, so fetch goes through the I-cache, which is not required to
be coherent for a region whose attributes do not define it as cacheable.

**The mechanism is not yet measured and is not claimed here.** Two candidates fit the observation
equally well and are separable by one run:

- The entry window is mapped by **XNU's** page tables with a type for which instruction fetch is
  unpredictable — the fix would be in how the entry image is described to XNU, not in this image.
- The **I-cache holds lines for those physical addresses from something else that executed there**,
  and the fetch is being served from them — the fix would be an invalidate before the jump.

Both are checkable from what this image already records: the payload knows XNU's descriptors for the
window (`mmu_kvtop_wpreflight` and the two `wimg_bits_*` words are already stubs waiting for their
step), and the run can invalidate the I-cache immediately before the jump and see whether the bias
disappears.

## Why this matters beyond one record

This is the third consecutive step whose *report* was the wrong thing rather than the code it was
reporting on — 269's faulting handler, 269's garbage `why`, and now this — and it is the first where
the reporter is provably right or wrong depending on machine state. The defence that worked is the
one `mi4-measurement-defects` keeps arriving at: **carry the same value by two independent routes.**
`entry_write_kv` (a table read in `.rodata`, executed from the epilogue) produced the right answer on
every line of every report while `entry_kv`'s arithmetic was wrong, and the pair is what localizes
the fault to the state rather than to the code, the value, or the buffer.

The step's own result does not depend on the corrupt characters: the caller's *offset* is
`+0x190` — `bl mk_timer_init`'s return address — and the prediction was written as an offset for
exactly this reason.

One number in that report is worth reading correctly before it is misread: `xnu_entry_kv_written`
is `0x5e` (94) while `xnu_entry_kv_in_dram` is `0x82` (130). That is the register-held snapshot
versus the memory, and the difference is the epilogue's own extra `entry_kv` call extending
`g_kv_len` after the snapshot was taken. It is the pair doing its job, not a defect.

## Safety

All 87 contracts report `failure_mask=0x00000000`, all 25 that report it say
`persistent_write_attempted=0x00000000`, none says `1`, nothing is flashed — every run is a
non-persistent `fastboot boot` — and the device returned to Android on its own after each of the
three runs this step made.

## What is next

- `mk_timer_init` — `osfmk/kern/mk_timer.c` (manifest:572), `osfmk_kern_mk_timer.o`. In call order
  after it `ipc_bootstrap` **returns**: its instructions are `pop {r4, r5, fp, lr}` at `+0x194` and a
  tail `b host_notify_init` at `+0x198`, so a stop there has its `lr` pointing at
  `kernel_bootstrap`'s call site, not at anything inside `ipc_bootstrap`. That prediction must be
  written against `kernel_bootstrap`.
- The fetch question above, as its own experiment, with the two discriminating runs named.
- `osfmk/kern/task.o`, for `task_max`, now that four separate consumers of its zero value have been
  measured.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The stop is `stub_hit=mk_timer_init` in the results blob; `xnu_entry_stub_caller_e`,
`xnu_entry_stub_caller_v` and the two `w0`/`w1` words agree with
`ipc_bootstrap+0x190`, and the `entry_kv`-written `xnu_entry_stub_caller` does not.
