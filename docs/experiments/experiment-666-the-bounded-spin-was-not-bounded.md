# 666: the bounded spin was not bounded — an unsatisfiable guard found by building the arm that needed it, and the payload build that does reproduce

664 §4 concluded that the next press should force the **watchdog bite alone** (`STAGE90_HW_WATCHDOG_SELFTEST=1`:
arm the watchdog, do *not* arm the software dead-man, spin, come back on the bite), because the PS_HOLD
forced reset is already proven by every returning run while the bite has never been observed to fire.
This step built that arm — and building it is what found that the arm was not safe.

Nothing was sent anywhere and no device was touched. Every reading below is a build, a disassembly, a
host program, or the gate — all host-side.

## 1. The arm's own fallback was dead code, and the guard that was supposed to reach it could never fire

The self-test's whole safety argument is one paragraph, quoted from `stage90.h` at the macros that
carry it:

> *net fails -> the device returns at this deadline via `platform_reboot()`, which is the already-proven
> PS_HOLD path, and the log says so. Either way the device comes back without being touched … So the
> bound costs nothing in diagnostic power and removes the one run that could have left the phone dark.*

(The paragraph also understates the fallback: `platform_reboot()` is **PS_HOLD *and* a forced bite** —
`stage90_main.c:1067-1071` writes `MSM8974_PSHOLD = 0`, then `stage90_hw_watchdog_bite_now()` at
`hw_watchdog.c:314-315` writes BITE=1 then RST=1. So the deadline branch is doubly netted; §1 is about
that branch never being *reached*.)

`STAGE90_SELFTEST_DEADLINE_US` is `90000000u`, and `stage90_selftest_bounded_spin` (`stage90_main.c:1157-1181`)
tests it through `timebase_elapsed_us`. That function, at HEAD:

```c
/* timebase.c:34-43, HEAD */
/*
 * Stage3 proved CNTFRQ is 19,200,000 Hz on cancro. For short intervals used
 * here, low 32-bit deltas are enough. …
 */
uint32_t timebase_elapsed_us(uint64_t start, uint64_t end)
{
    uint32_t delta = (uint32_t)(end - start);
    return (delta * 5u) / 96u;        /* <- 32-bit product */
}
```

`delta * 5u` is unsigned 32-bit arithmetic, so it **wraps at 858,993,459 ticks — 44.74 s at 19.2 MHz**.
The wrap is not monotonic (the product cycles), so no caller can compensate for it; the only thing that
matters here is the ceiling it puts on the return value:

```
max timebase_elapsed_us = 4294967295 / 96 = 44,739,242 us
the deadline            =                       90,000,000 us
```

**So `elapsed >= 90000000u` cannot be true, the spin never breaks, and everything after it — the two
`log_kv32` calls and `platform_reboot()` — is unreachable.** The arm whose comment promises "the device
comes back without being touched" was the one arm that could not: with the bite dead, the only net left
is the software dead-man, which this arm deliberately does not arm.

### Measured three ways, because a number this consequential should not rest on one extractor

1. **In the built object.** The 32-bit product is visible: `add r0, r0, r0, lsl #2` is `delta * 5` in one
   32-bit register, followed by a `umull` reciprocal divide by 96. Disassembled from
   `out/stage90/stage90.elf` (`timebase_elapsed_us` at `0x9ac8` in that build).
2. **Against the guard, in the same object.** `stage90_main` materialises the deadline as
   `movw r6, #0x4a80` + `movt r6, #0x55d` (r6 = `0x055d4a80` = 90,000,000) and compares `cmp r0, r6` /
   `bcc` back to the loop — an **unsigned** `>=`, against a ceiling of 44,739,242. The constant exists;
   it is simply unreachable.
3. **On the exact types, on the host.** `uint32_t` delta, `(delta * 5u) / 96u`, swept over all 2^32
   inputs: the maximum return is **44,739,242 µs**, and at the 90 s delta (1,728,000,000 ticks) the old
   function returns **521,514 µs** — the wrapped value, which is why a broken guard of this shape does
   not look broken.

The claim in the old comment — *"for short intervals used here, low 32-bit deltas are enough"* — was
true of every caller **except the one that mattered**. `gic.c`'s windows are 20 ms and 50 ms,
`xnu_handoff.c`'s preflight is ~8 ms, `xnu_kernel.c`'s is 1.9 ms; only the self-test's deadline is above
44.74 s. This is the same defect class as the pattern this project keeps meeting: **a claim in a comment
is not a check**, and here the claim was the *reason* the wrong arithmetic was chosen.

## 2. The repair: exact, 32-bit, and verified exhaustively

The obvious repair is to widen the product:

```c
return (uint32_t)(((uint64_t)delta * 5u) / 96u);      /* does NOT link here */
```

**It does not link, and that is measured rather than assumed.** GCC 10.3 emits a call to
`__aeabi_uldivmod` for a 64-bit dividend, and this payload is freestanding — no libgcc — so the link
fails with `undefined reference to __aeabi_uldivmod` in five places (`timebase.c`'s three functions and
`gic.c`'s `stage90_arm_pc_sampling_watchdog`). The old comment's worry about "a runtime 64-bit division
helper" was therefore **correct**; it was the *product* that never needed the width.

So the division stays 32-bit and is made exact by splitting the dividend at the divisor. For
`elapsed = delta * 5 / 96` with `delta = 96q + r`:

```
delta * 5 = 480q + 5r,  and 480q / 96 = 5q exactly (480 = 96 * 5)
=> floor(delta * 5 / 96) = 5q + floor(5r / 96)
```

```c
/* timebase.c, now */
uint32_t timebase_elapsed_us(uint64_t start, uint64_t end)
{
    const uint32_t delta = (uint32_t)(end - start);
    const uint32_t q = delta / 96u;
    const uint32_t r = delta % 96u;
    return q * 5u + (r * 5u) / 96u;
}
```

Both terms are small — `q <= 44,739,242` so `5q <= 223,696,210`, and `5r < 480` — and the `/ 96u` and
`% 96u` stay the reciprocal sequence GCC already generates, so **no libgcc symbol is referenced**: the
linked image has **no undefined symbols at all**, which is the check the old comment asked for and never
performed.

**Verified exhaustively, not sampled.** A host program on the exact types compares the decomposition
against a 64-bit reference over **all 2^32 inputs**: **0 mismatches**. The ceiling is **223,696,213 µs**
(2^32 − 1 ticks), and the three values this project turns on all come out exactly:

```
elapsed_us(1,728,000,000 ticks) = 90,000,000 us   -> the guard is now reachable, exactly at the deadline
elapsed_us(0xffffffff)          = 223,696,213 us  -> the full range a uint32_t return can hold
usec_to_ticks(90,000,000)       = 1,728,000,000   -> the round trip closes
```

The same decomposition is applied to the inverse conversion, and **a duplicate definition was removed
rather than a second copy fixed**: `gic.c` carried its own `timer_usec_to_ticks`, byte-identical to
`timebase.c`'s `usec_to_ticks`. The two are now one function, `timebase_usec_to_ticks`, declared in
`stage90.h` — so a future fix in one place cannot leave the other wrong. That inverse is *latent* rather
than live (every caller passes 500 µs or 100 ms), and it is fixed anyway for that reason.

## 3. The structural half: the deadline's reachability is now a build failure

A corrected conversion leaves the *next* deadline free to be unreachable from the other side, and the
guarantee this deadline exists for is a safety property, so the bound is checked rather than described.
`stage90.h`, at the macro:

```c
#define STAGE90_SELFTEST_DEADLINE_US 90000000u

#if (STAGE90_SELFTEST_DEADLINE_US * 96u / 5u) > 4294967295u
#error "STAGE90_SELFTEST_DEADLINE_US exceeds the 32-bit tick delta timebase_elapsed_us takes"
#endif
```

Three things about this check are deliberate and each was measured:

* **The `#define` comes first.** An `#if` written *above* it would read the macro as 0, pass for every
  input, and leave a guard that prints nothing and guards nothing — 663 m663's shape, and the first
  draft of this very block had it in that order. Caught by reading the block back before building.
* **It is falsified, not asserted.** Raising the deadline in place to `300000000u` (5.76e9 ticks, past
  2^32) makes the preprocessor emit the `#error` and the build refuse; with the real value the same
  invocation is silent. A guard that cannot fail is not a check, and the only way to know is to try to
  make it fail.
* **A second check on the `uint32_t` return was written and then deliberately removed.** Ticks are 19.2×
  microseconds, so bounding the ticks already bounds the microseconds at 223,696,213 µs: the second
  `#if` could never fire on its own. One check that can fail beats two where one is a restatement of the
  other, and the removal is stated in the comment so the absence is not read as an oversight.

## 4. The arm, and the two things building it found

The arm was built with
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1 -DSTAGE90_HW_WATCHDOG_SELFTEST=1' ./build.sh` — both
switches, because `STAGE90_XNU_ENTRY=1` is the gate's own requirement for a payload that jumps into XNU
(533's defect), and the self-test arm is still that payload.

**Finding 1: the self-test's placement is load-bearing, and the gate is what said so.** The first build
put the spin where the code had always put it — immediately after the watchdog is armed, **before**
`build_stage90_apple_dt()`. The payload was built, parked, verified against the record and recorded in
`revert-set.txt` … and the gate then refused it:

> `REFUSING: the boot image carries 'up_style_idle_exit=1' 1 time(s) and the payload 1, where 515's
> repair must be in two command lines …`

A spin that never returns makes `build_stage90_apple_dt` unreachable; it is `static`; the compiler drops
it and its string literal with it; and the image then carries **one** copy of the token where 515's
repair needs two (XNU's `PE_boot_args()` reads `boot_args.c`'s `CommandLine`, the port's contracts read
the `/chosen` `boot-args` property). Measured before and after: `1 occurrence(s)` in `stage90-qcdt.img`
and `1` in `stage90.bin` for the first build, **`2` and `2`** for the second.

The repair is a placement, not an exemption: the self-test now sits **after** the DT build. Nothing is
lost — the watchdog is still armed at the top of `stage90_main`, before anything that can hang, so the
builder runs under the hardware net — and the arm becomes the *same payload as every other arm of this
stage*, at two command lines.

**Finding 2: the build.sh clause this step had added to work around Finding 1 was deleted.** It skipped
the 515 leg for a self-test build and **printed the skip**, on the reasoning that the leg's subject (the
`/chosen` copy in `stage90_main.o`) was absent by construction. The gate then showed that reasoning to be
wrong: the gate re-runs that property itself, from the image, so a skip could only ever have let through
an image the gate refuses. A build-time check that disagrees with a gate that also runs is not a
convenience, it is a hole. With the placement fixed the leg passes with no clause at all — and the leg
and the gate now agree, which is the only reason to have both.

The recorded arm, measured after the second build:

| file | sha256 | bytes |
| --- | --- | --- |
| `stage90-qcdt.img` | `ef0361a2bd761eb7…` | 8,540,160 |
| `stage90.bin` | `fdcea7eea75c44d2…` | 6,015,252 |
| `stage90.elf` | `20e2c0fd0d8aed55…` | 6,077,272 |
| `stage90-build-config.txt` | `7d54be1ee31dd244…` | 681 |
| `xnu_arm_entry.bin` | `a43304f267f47f65…` | 5,519,996 |

and it is parked at `out/stage90/frozen/armed-selftest-wdog-ef0361a2/`, recorded as the set
`armed-selftest-wdog-ef0361a2` in `revert-set.txt` (11 files; `verify_revert_set.sh` exit 0 against **both**
the live tree and the park). **The superseded first build is left in place and named** in the record, with
its hash and the reason, and it is not a set in that file — a withdrawn artifact is recorded, not tidied.

**The arm is identifiable from the payload's own record, and for this arm that is not true of any other.**
`stage90-build-config.txt` is byte-identical (`6c2b6038…`) across the 574, sleeper and acting arms because
it is the *payload's* dump and those arms differ in *entry* switches. This arm sets a **payload** switch,
so its dump reads `#define STAGE90_HW_WATCHDOG_SELFTEST 1` at line 16 where all three read `0u`, and the
file's hash and size change with it (682 → 681 bytes).

**Gate: exit 0 / 546 lines / 0 bytes stderr**, with the invocation the run will use —
`preflight_boot_check.sh --allow-xnu-entry --allow-hw-watchdog-selftest`. Without the second flag the gate
refuses this arm at its own dedicated clause, which is the clause to read before the press, because it
names the three outcomes and the third one:

> `~33s` the hardware watchdog fired — it works · `~90s` it did not, and the bounded spin's PS_HOLD reset
> brought the device back · *"never both failed. Note this third case is reachable: `platform_reboot()`
> falls back to the same watchdog, so a dead watchdog plus a PS_HOLD that does not land means a manual
> power press. That is a real finding, not a lost run, but it is the one outcome with no log."*

## 5. The payload build does reproduce — measured, and it corrects 408

408 is this project's most-referenced build claim: *the payload link does not reproduce*, because a
same-tree rebuild once came out 48 bytes apart (533's note localises it to 44 bytes inside two
`*_selftest` functions and 4 immediately before the embedded Mach-O size). The arm was parked on the
strength of it, the readiness tool's header repeats it, and `run_and_capture.sh`'s design leans on
"`out/` holds the only copy".

**It reproduces, and the 48 bytes are a switch.** Three builds of the payload, all host-side:

| build | `stage90.elf` | `stage90.bin` | `stage90-qcdt.img` |
| --- | --- | --- | --- |
| plain, twice (no flags) | `40a847d4…` | `cc286b79…` | `2ae69c9f…` — **identical both times** |
| `-DSTAGE90_XNU_ENTRY=1`, pre-fix sources | `936c2741…` | `a47bc89f…` | `a5997bae…` |
| the parked acting arm `armed-seam-poc-a43304f2` | `936c2741…` | `a47bc89f…` | `a5997bae…` |

The third row is not a coincidence and not a rebuild of a park: it is a **rebuild from the tree**, with the
acting arm's own switches, compared file by file against the park. **All seven members tested came out
identical** — `stage90.elf`, `stage90.bin`, `stage90.img`, `stage90-qcdt.img`, `stage90_fixture.macho`,
`SHA256SUMS.txt` and `stage90-build-config.txt` — and the entry image built alongside them is
`a43304f2…`, byte-for-byte the arm's own entry. The size difference that started 408 is exactly the
switch: `kernel_size` is **6,015,356** with `STAGE90_XNU_ENTRY=1` and **6,015,308** without it — 48 bytes,
the `bl stage90_xnu_entry_run` path and nothing else.

**The scope, stated as narrowly as it was measured.** This refutes "the payload link consumes something
beyond the committed sources" for **this arm, in this tree, on this host**, with the switch set that arm
was built with. It does not explain 526's `7819cddb…`/`76bf4ea7…` pair, which this step did not rebuild —
and a payload embeds `xnu_arm_entry.bin`, so a rebuild of an *older* arm should not be expected to
reproduce at all once the entry image has moved. What the measurement does establish is the thing the
project needs: **the bytes of an arm are a function of the tree and its switch set**, so a park's
provenance can be tested by rebuilding rather than asserted, and the two plain builds agreeing is the
control that makes the flag experiment meaningful.

**The fix is the cost of this correction, and it is real:** `timebase.c`, `gic.c` and `stage90_main.c`
have changed, so the acting arm's payload **can no longer be rebuilt from the tree**. Its park is
unaffected — bytes on disk, verified against the record — and reverting to it is a `cp`, which is what the
park is for. But a *fixed* acting arm would be a **new** arm with a new name, which is why the repeat of
the acting arm and this self-test arm are now different arms rather than a rebuild and a variant.

## 6. What the arm asks, and what its log will say

The pre-registered reading, in the arm's own terms — and it is a reading of **content**, not of timing,
because §3 of `experiment-665` showed a run cannot be *attributed* from outside:

* the arm publishes `MI4IOS6_STAGE90 hw_watchdog SELFTEST: spinning; the hardware countdown should reboot
  us at ~…; if it does not, the bounded spin reboots us at ~…` **before** it spins, so a returning run's
  log names which arm it was;
* **the bite fired** ⇒ the device returns at ~28 s and the log carries the spin line **and no deadline
  line**;
* **the bite did not fire** ⇒ the device returns at ~90 s and the log carries the spin line **and**
  `hw_watchdog SELFTEST: deadline reached - the hardware watchdog did NOT fire`, plus
  `selftest_deadline_us=0x055d4a80` and `selftest_elapsed_us`;
* **both failed** ⇒ no return and no log — the gate's third case, and the one outcome that costs a manual
  power press. It is a finding about the bite *and* about PS_HOLD, not a lost run.

Both informative directions print, which is what 665 §5 required of this arm and what a timing-only
reading would not have given.

## 7. What blocks the press, and what this step does not do

Three of `tools/verify_press_ready.sh`'s five rows fail against this arm, and **two of the three are the
tool's own staleness rather than the arm's state** — recorded here as a finding, repaired in the next step
rather than in this one:

1. **`live arm is the recorded arm` — FAIL, the tool's default, not the arm.** `tools/verify_press_ready.sh:86-87`
   defaults `PARK`/`SET` to the **sleeper** arm (`armed-sleepless-696a0f39`), so the row compares the live
   bytes against an arm from two steps ago. Both are overridable (`--park`/`--set`) and the live bytes
   verify 11/11 against their own recorded set. **A default that names a specific historical arm is a
   stale pin**, and it is the same shape as the arm-row defect 653 repaired.
2. **`the gate accepts this tree` — FAIL, the tool's invocation.** The tool runs
   `"$GATE" --allow-xnu-entry` with no selftest flag, so the gate refuses at the clause that wants
   `--allow-hw-watchdog-selftest` and the row reports the refusal as a verdict about the tree. The gate
   itself is green (exit 0, 546 lines) under the run's own invocation. A tool that cannot pass the flag an
   arm needs must not let that read as a failed check.
3. **`the arm is named by a reading` — the WRONG arm, named confidently.** The row reads
   `xnu_arm_entry.elf` and the entry record's seam pair, and answers *"the ACTING arm (653 …)"* with the
   full seam reading — correct about the **entry**, and wrong about the **press**, because this arm never
   reaches the handoff. That is 653's own repaired defect recurring one arm later.
4. **`the press would be caught` — FAIL, and this one is physical.** Neither list names `4a2fe00b`, and
   the row's own text is the warning: if `33e80afe` is in fastboot when the phone arrives, the run is
   refused and the press is spent. The neighbour must be off the bus, and **no catcher is armed**
   (652 ended the cover), so an arming must precede the press.

**What this does not do.**

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; the frontier is
  where 665 left it — XNU reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}`,
  `poll_seq` stops at 2. The press remains the only step that can move it, and the press is blocked on the
  items in §7.
* **It does not spend a press, and it does not arm anything.** No watcher is armed. Nothing was sent:
  no `fastboot`, no `adb`, no device action of any kind.
* **It does not claim the watchdog bite is broken.** It claims the bite is *unmeasured* and that this arm
  is now built, recorded and gated to measure it — which is exactly what 664 §4 asked for.
* **It does not re-derive 408 beyond one arm.** §5's scope is the one arm that was rebuilt. What it
  corrects is the standing inference, not the historical observation.
* **It does not touch `preflight_boot_check.sh`**, which is the peer lane's file. The two clauses §4 and
  §7 cite are reads of it, not edits to it.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet, and an unobserved boot is not it.

## 8. Safety

No device action of any kind: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to
storage**. Two `fastboot`-capable processes are not running — no catcher is armed and none was armed,
replaced or read by this step. Every reading is host-side: builds (`./build.sh` in three switch
configurations), `arm-none-eabi-objdump`/`nm`/`strings`, a `gcc` host program on the exact integer types,
`tools/verify_revert_set.sh`, `tools/verify_press_ready.sh` (host-only by its own header, and measured so
in 634 with sudo/adb/fastboot stubbed) and `preflight_boot_check.sh` (host-only, and its only device-free
guarantee is the same one). `out/` currently holds the recorded set `armed-selftest-wdog-ef0361a2`, which
is the arm the next press sends; the superseded first build is parked beside it and named in the record as
not-to-be-sent. `fastboot boot` only, never `flash`; the device has been dark since the 17:24:03 park and
this step did not change that.
