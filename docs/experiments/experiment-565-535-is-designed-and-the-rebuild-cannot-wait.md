# 565: 535 is designed, and deliberately not built - with the reason a rebuild cannot wait

A **pre-registration** for the arm after 533, written while the device is off the bus. Nothing is built,
nothing is edited under `xnu_arm_boot/`, and the frozen pair is untouched. It exists because of a
constraint that is easy to violate in exactly the way the waiting time invites, and because the one
already-built switch that looks like 535 is **not** 535.

## 1. The constraint, measured: any edit under `xnu_arm_boot/` before the run blocks the gate

The natural use of the wait is "prepare 535 while the phone charges". That ends the arm. The gate's
entry-sources clause recomputes the manifest over `stages/stage90/xnu_arm_boot/` (`find . -maxdepth 1
-type f`, hashed) and compares it **in both directions** against what `build_entry.sh` recorded
(`preflight_boot_check.sh:549`), then refuses:

```
fail "the entry image is not the build of these sources: $ENTRY_SRC_NAMES ..."
```

and the refusal's own text is the part that matters here: the gate cannot tell a *recipe* change from a
*compiler input* change (`:601-620`, 539's repair), so it names the test. Editing `entry_trace.c` - which
is what 535 needs - is a compiler input, so the test it names goes the second way: the entry bin's hash
differs from the record's `STAGE90_XNU_ENTRY_SHA256`, and then:

> "If the hash differs, the arm really is a new one and `./build.sh` is owed ... it does not reproduce
> (408) and would spend the frozen boot image."

So the sequence "edit → rebuild entry → gate" produces a **different** `xnu_arm_entry.bin`, and satisfying
the blob clause then requires `./build.sh`, which destroys `1daaf44e624563694e…`. **533's arm is one
artifact, frozen and armed; there is no version of preparing 535 in this tree that does not spend it.**
The order is forced: run 533, read it, *then* build. That is what "not built in advance" means as a
constraint rather than as a preference, and it is the same shape as 540 §1's trap - a refusal whose
prescribed remedy costs the freeze.

(What *is* free, and was not done: a doc, a design note, a switch-name decision, a clause review. Anything
outside `xnu_arm_boot/`.)

## 2. `STAGE90_XNU_EXIT_POC_FLUSH` is not 535, and it looks exactly like it

There is already a built, switched, documented intervention with a `FlushPoC_Dcache` in this wrapper -
517's, default 0, and the frozen 533 arm carries it **off** (`540`'s switch table). Reading its own
comment, it does the thing 547 §5 asks for:

> "the *invalidate* is what does the work, discarding the L2's pre-window copies before `SCTLR.C` makes
> them live again" (`entry_trace.c:1885-1888`)

**But its site is one call earlier than the seam 547 §5 names.** In the wrapper it is the first thing in
the body, before `mov %0, sp` and before `__real_platform_cache_idle_exit()`:

```c
void __wrap_platform_cache_idle_exit(void)
{
    uint32_t sp;
#if STAGE90_XNU_EXIT_POC_FLUSH
    FlushPoC_Dcache();          /* <- wrapper top: BEFORE the real exit runs, so BEFORE its push */
#endif
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    ...
    __real_platform_cache_idle_exit();
```

while 547 §5's seam is **inside** the real exit, at its own `bl FlushPoU_Dcache` (`0x800462d8`, return
address `0x800462dc`) - i.e. **after** `push {fp, lr}` and **before** `pop {fp, pc}`. The two differ in
exactly the thing 546's mechanism is about: **the push is what leaves the line the pop reads.** A flush
before the push cannot cover a line the push has not written yet; a flush before the call can only
discard copies that predate the exit's own frame.

So flipping `STAGE90_XNU_EXIT_POC_FLUSH=1` and calling it 535 would test a **different hypothesis while
wearing this one's name** - one value with two definitions, this project's most-repeated class, in the
place where a mis-attributed run costs a power press and may cost the log (517's flag-on run did not come
back at all). **535's seam is the inner call site, and reaching it means wrapping a call inside
`platform_cache_idle_exit`, not adding a call to the wrapper.**

## 3. What 535 will be, conditioned on 533's result - stated now so it cannot be fitted afterwards

547 §4's three rows decide this, and the third row is the one that would stop 535 as designed:

| 533's result | what 535 is |
| --- | --- |
| dies at the pop, log present, device returns | the enable is confirmed as the difference between a recovered death and a hang: 535 is the inner-seam PoC invalidate, `FlushPoU_Dcache`'s return address `0x800462dc` as the identification |
| dies at the pop, no log, no return | the capture's shape is back in play (533 differs from 520's cell by the capture alone) and the next bisection is 520's publisher shape; **535 waits** |
| **returns with no pop death at all** | 546's mechanism is wrong for the enable-off cells and **535 must not be built as designed** |

Two constraints carry over from 547 §5 and §3 regardless of the row: the operation is a **PoC invalidate
of the L2**, and it **must not re-enable allocation outside the coherency domain** - 522's wrapper write
did, and the two cells carrying it produced no log, so "set `SCTLR.C` in the wrapper" is not available to
535 whatever else it does.

## 4. What this step is not

Not a build, not a source edit, not a device action, not a prediction about 533 (533 §5 and 547 §4 already
are that, and 547 §4 still stands after 558/559/560 corrected its key name). It changes no file the gate
hashes and does not move the frozen pair (`1daaf44e624563694e…` / `f202f2465886aba6…`). It is filed as 565
because 564 was the last number used by this session and 563 by the peer's step in the same window; if the
peer has taken 565, this doc renumbers and nothing else does.
