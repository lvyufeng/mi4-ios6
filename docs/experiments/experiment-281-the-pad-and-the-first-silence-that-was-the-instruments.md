# Experiment 281 — the pad, and the first silence that turned out to be the instrument's

**Step:** not an object. This is the experiment that bisects 280's silence — the one where the walk
stops linking objects and starts asking the image questions — and it produced two things: the NOP pad
in `entry_epilogue`'s cache sweep, and the `AFTER` variant of the checkpoint instrument.
**Prediction:** that the silence lives in `[arm_init+0x1d8, arm_init+0x340)` — `arm_vm_init`'s body
plus the eighteen real calls `arm_init` makes after it returns.
**Result:** the interval was bisected down to a *four-byte `memmove`* that cannot hang, and the reason
it looked like one is the subject of experiment 282: the report path itself. Nothing in this
experiment advances the frontier; everything in it is instrument work, and the instrument is what was
broken.

## Why the silence was not where it looked

280's plain run stopped nowhere: the payload's whole ladder completed, the jump line was written, and
after it the entry image wrote not one byte — no `stub_hit`, no `exception: <vector>`, no epilogue
record. The three checkpoint runs that followed (`cpu_data_init` at step 1, `arm_vm_init` at step 18,
`machine_startup` at step 19) narrowed it to the interval between the last two, and the middle one has
to be read carefully: `--wrap` *replaces* the function, so a checkpoint at `arm_vm_init` proves the run
reaches the call *site* and says nothing about the function's 0x790 bytes.

## The pad

The frontier could not be a missing symbol. 279's own run reported `stub_hit=klist_init` in an image
whose first 0xbbbc0 bytes are instruction-identical to 280's, and `klist_init` is reached through
`machine_startup`, `kernel_bootstrap` and `ipc_init` — so in 279 the boot ran past everything 280's
silence covers. The code is the same; what differs is *data layout*, and there is exactly one piece of
code in the image that XNU writes to:

```
cpu.c:570-580   bcopy_phys(vtop(&BootArgs_paddr),       gPhysBase + (unsigned)&ResetHandlerData.boot_args
                                                              - (unsigned)&ExceptionLowVectorsBase, 4)
                bcopy_phys(vtop(&CpuDataEntries_paddr), gPhysBase + (unsigned)&ResetHandlerData.cpu_data_entries
                                                              - (unsigned)&ExceptionLowVectorsBase, 4)
```

XNU computes those destinations from its own link, where the vectors blob *is* the first thing in the
image. Here `ExceptionLowVectorsBase` is at `0x800dc4bc`, `gPhysBase` is `0x80000000`, the delta
resolves to `0x2404`/`0x2408`, and the two writes land **inside `entry_epilogue` at `0x80002348`** — the
function that writes every log line this walk has ever read. `nm` between `0x80002280` and `0x80002600`
finds exactly one symbol, so there is no doubt about the enclosure. The two instructions destroyed were
`way_shift = 32u - n` and `way = 0`, and the values written are addresses, which decode as register-form
`ANDS` instructions with `cond=HI` that set neither register: the sweep then counts ways from whatever
`lr` held, which is bounded but astronomically long, and the epilogue never reaches its ram-console
write. **A hang inside the reporting path looks exactly like a hang in the boot.**

The fix belongs on this side: an address XNU computes from its own link has no reason to move, but where
this image puts its code is this image's business. The pad is placed **first** in the sweep block — as
early in `entry_epilogue` as the function's own prologue allows — and is deliberately wider than the
eight bytes at risk, because the requirement is only that it *contain* 0x80002404 and 0x80002408, and
everything added before it pushes it towards them.

## The `AFTER` variant, and what it is for

The plain checkpoint is terminal at the call site, so it can say that a function was called and never
what it did; the `SKIP` variant runs the real function but can only report from a *later* call, which a
function with one call site does not have. `STAGE90_ENTRY_CHECKPOINT_AFTER=1` calls the real function,
records **`cp_ret`**, and reports after it — so a single-site function becomes a value probe:

- a report with `cp_ret=<value>` means the callee returned, and the value is the answer;
- a silence means the callee did **not** return, which is a positive finding because the plain
  checkpoint on the same symbol has already proved the call site is reached.

It composes with `SKIP` (first `n` calls pass through, call `n+1` runs for real and reports), and it has
one limitation measured in the built image rather than argued: the wrapper's frame sits where the callee
reads *stack* arguments, so `AFTER` on a callee with more than four arguments reads the caller's garbage
— `bcopy_phys` is exactly such a callee, and `SKIP`'s pass-through is safe for it only because GCC
compiles `return __real_<sym>(...)` as a sibling call (`add sp, sp, #24` / `b bcopy_phys`), which
restores the frame before the callee runs.

## What the runs measured

```
pmap_map_cpu_windows_copy                 SILENT (one call site, and it is not reached)
mmu_kvtop_wpreflight                      reports: caller 0x80036050, cp_arg0 0x80002408,
                                          cp_arg1 0x80000000, cp_arg2 0x80800000
mmu_kvtop_wpreflight + AFTER=1            reports: cp_ret=0x80002408 - the preflight returns
bcopy SKIP=2 AFTER=1                      reports: cp_calls=3, args 0x80300000/0x80304000/
                                          0x4000, caller 0x80018600 = arm_vm_init+0xb0
```

The `AFTER` run is the one that makes the instrument worth having: `cp_ret=0x80002408` is a direct
reading of `gVirtBase` (identity with `gPhysBase`) *and* the proof that `mmu_kvtop_wpreflight`'s first
call returns success, which is the first conjunct of `bcopy_phys`'s fast path. And the `SKIP=2` run's
`caller_v` refuted the prediction it was built on — call 3 is `arm_vm_init`'s `bcopy`, not the fast
path's — while *naming itself* through its arguments, which is the property that makes `SKIP` safe to
use without a reliable call census.

**Safety on every run in this experiment:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` in all 25 places, `failure_mask=0x00000000` in all 87
contracts, zero real aborts, and the device returned to Android on its own.

**Next:** experiment 282 — the four runs that showed the pad was only half a fix, that the half which
mattered was the *report*, and that the corruption's weapon is `r7`.
