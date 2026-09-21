# Experiment 517 — the step was two changes, and its first run did not come back

**One line.** 516's doc named two things owed: measure the interrupt frame slot, and repair the exit the
way 516 repaired the enter. Both were built into one image, that image was booted non-persistently, and
**the device did not return** — no `MACH Reboot` as in 516, no `panic()`, no watchdog bite, no log, just
the USB disconnect that is the handoff and then nothing, for ~15 minutes. It needed a power-button press,
and the cold boot took the ram_console with it. So there is **no reading**: the run cannot say which of
the two changes did it, and one of them (`FlushPoC_Dcache()` on the idle exit) is the only thing in this
image that can change what the kernel *does* rather than only what it reports. The step is therefore
split, the state change now defaults **off** (`STAGE90_XNU_EXIT_POC_FLUSH=0`, asserted against the image
in the build), and the measurement — the frame read from inside the handler that owns it — is the arm
that has not yet had its run. What this step did land is the finding: **the recovery net's record is not
"it always brings the phone back"** (it recovered 506–515 and 516 returned on XNU's own path; 517 is the
first run it did not), and `preflight_boot_check.sh` and `docs/reference/recovery-and-rollback.md` §4a
now say so.

## What the step was for

516 ended with two sentences that are the shape of this step:

> **517 is owed the frame-slot measurement** — `fleh_irq_handler` stores the frame pointer at
> `[cpu_data + 0xb0]` (`locore.s:1403`) and `return_from_irq` clears it (`:1433`), so a wrapper on
> `ml_get_timebase`, which the handler calls at `0x8001aae4`, can publish that frame's own
> `SS_R0..SS_VADDR` beside the timebase it is returning — **and the exit-side mirror repair**:
> `FlushPoC_Dcache()` in `__wrap_platform_cache_idle_exit` **before** `__real_platform_cache_idle_exit()`,
> so the L1-only `FlushPoU_Dcache` at `caches.c:460` is no longer the only flush on the way out.

The first answers 516's own open number: `pc = far` in both of its runs was **the CPU's own timebase**
where a code address belonged, so `return_from_irq` → `load_and_go_sys` (`ldr lr, [sp, #60]` /
`ldm sp, {r0-r12}` / `movs pc, lr`) returned to whatever the interrupt frame's `SS_PC` slot held. The one
C caller that runs between the handler's dispatch and that return is the handler's own `ml_get_timebase`
call, and the handler's `locore.s:1421-1423` is `add r0, ...` / `bl ml_get_timebase` / `str` — an entropy
stir that stores `timebase` at `EntropyData.buffer[index_ptr]`. So the hypothesis the frame reading
exists to settle is narrow and checkable: **if the frame's `SS_PC` slot already holds the timebase at
that call, the writer is the stir, and the index arithmetic is the bug; if it is still an address, the
stir is excluded and the writer is something else in the handler.**

The second is 515's measurement read backwards: `platform_cache_idle_exit`'s own first act is an
**L1-only** clean-and-invalidate, the L2 keeps whatever the enter's window left it, and `caches.c:490`
sets `SCTLR.C` again at the end of that same function — the moment those copies become live.

## What the step changed

Two blocks, in two files, and the split between them is now a build flag.

**The frame reader — `__wrap_ml_get_timebase` → `entry_note_timebase_call(ret_lo, sctlr)`.** The wrapper
calls the real function once and passes its 64-bit return through unchanged, then records: the frame
pointer read out of `cpu_data->cpu_int_state` (`+176`) reached through `TPIDRPRW` → `[+1484]`
(`ACT_CPUDATAP`); the six frame words at `SS_SP/SS_LR/SS_PC/SS_CPSR/SS_STATUS/SS_VADDR`
(52/56/60/64/68/72); the entropy stir's own store address (`EntropyData.index_ptr + 4`, wrapped to
`EntropyData.buffer` at `+68`); and `_hit`, which is 1 exactly when that address lands inside the frame
just read — the one number that decides whether the stir can be the writer at all. Seventeen keys,
`xnu_live_tb_calls/_off/_seq/_frame/_pc/_lr/_sp/_cpsr/_status/_vaddr/_index/_target/_hit/_datap/_ret_lo/
_prev_lo/_sctlr`, and `_datap` is published beside `_frame` so 516's `_pcx_datap` is a comparison rather
than an inference.

**The dereference is guarded by the kernel's own bound, not by hope.** This function runs *inside* the
interrupt whose frame it reads, so a fault in it would be a fault inside the instrument that is reading
a fault (269). The candidate is dereferenced only when `cand ∈ (cpu_data->intstack_top −
INTSTACK_SIZE, cpu_data->intstack_top)` — `+8` and 16384 from this configuration's `assym.s` — and a
candidate that fails is published as `_frame = 0` with the six words zeroed. `_calls` and `_off` are
counted either way, so a run whose guard refused everything says that too.

**The exit flush — `FlushPoC_Dcache()` in `__wrap_platform_cache_idle_exit`.** `CleanPoC_Dcache` (516's,
`0x8004575c`) writes back and leaves the line valid; `FlushPoC_Dcache` (`0x80045828`) is the same two
loops with `cr7,cr14,{2}` — clean **and** invalidate, over the L1's geometry and then the L2's. On the
exit, with `SCTLR.C` still clear, the clean half is a no-op on lines the enter already cleaned and the
**invalidate** half is the whole of the work: it discards the L2's pre-window copies before `caches.c:490`
makes them live. It takes no reading — `_pcx`'s record is still taken after the real exit, with the cache
on — so 516's three-cache-state shape is unchanged.

**And it is off by default, because of this step's own first run.**

    STAGE90_XNU_EXIT_POC_FLUSH=0   (default)  the measurement alone; this wrapper is 516's
    STAGE90_XNU_EXIT_POC_FLUSH=1             restores exactly the image that ran on 2026-09-21

The `xnu_entry_517` clause asserts the wrapper's `FlushPoC_Dcache` **count against this same variable**,
so a flag-off build cannot publish a say line claiming a write-back it does not contain, and a flag-on
build cannot silently omit one. A build with neither, or with both, fails.

**A defect the review found before it could cost a run.** `ml_get_timebase` is the first wrapper in this
project on a **per-interrupt** path, and `entry_live_write` spends one of `ENTRY_LIVE_CAP` (8192) records
for the *whole* run — 516's log already carries **4258** `xnu_live_*` records. Two writes per interrupt
for the ~2500 interrupts of a 25 s run would have spent most of that budget inside a diagnostic that needs
eight records of its own, pushing the tail — the `wfi`, `pcx` and `pce` keys a run *stops* in — behind
it. That is 445's and 461's defect ("the report path's buffer was the tracer's and was full when the
report was written") arriving from the other direction. The instrument now writes the per-call keys for
the first `STAGE90_TB_LIVE_MAX` (8) recorded calls and refreshes the two counters once per
`STAGE90_TB_LIVE_EVERY` (256) calls: bounded at `8 × 17 + calls/256` ≈ 150 records for a 25 s run,
healthy boot or storm. The cost is stated rather than hidden — `_calls`/`_off` are current to the last
multiple of 256 — and the console line says so.

## What the build checks

`build_entry.sh`'s `xnu_entry_517` clause is green (0 failures, exit 0) and asserts, from the linked
image rather than from the source:

- `FlushPoC_Dcache` is the kernel's **own** function — one definition, not in pass 1's undefined set —
  and its body holds **2** clean-and-invalidate loops (`cr7,cr14,{2}`), against `CleanPoU_Dcache`'s 1 and
  `CleanPoC_Dcache`'s 2 `DCCSW` (`cr7,cr10,{2}`); the loop count *is* the difference between the L1-only
  form and the Point-of-Coherency one.
- `platform_cache_idle_exit` still calls `FlushPoU_Dcache` exactly once (`caches.c:460` — this step adds
  to Apple's instruction, it does not rewrite it), and the exit wrapper's flush count equals
  `STAGE90_XNU_EXIT_POC_FLUSH`.
- `ml_get_timebase` is the kernel's own and **every reference to it in the linked pool is a call**
  (`R_ARM_CALL R_ARM_JUMP24`) — a value-taken reference would let `--wrap` rewrite an address that is
  later branched through, and the record would then be missing the calls it exists to make.
- The compiled `entry_note_timebase_call` loads `[.., #176]`, `[.., #8]`, `[.., #1484]`, uses 16384 and
  68, and loads **all six** of the frame's offsets.
- This configuration's `assym.s` says `CPU_INT_STATE=176`, `CPU_INTSTACK_TOP=8`, `INTSTACK_SIZE=16384`,
  `ACT_CPUDATAP=1484` **and** `SS_SP/SS_LR/SS_PC/SS_CPSR/SS_STATUS/SS_VADDR/SS_SIZE = 52/56/60/64/68/72/80`
  — the second group against `entry_saved_state.h`, which is the *other* place those offsets are written
  down, i.e. this project's oldest defect class (one value, two definitions) closed for the six numbers
  this step's reading is made of.
- The seventeen keys are literals in the linked image's `.text`, and `entry_note_timebase_call` holds
  17–19 `entry_live_write` call sites — the cadence above is a property of the compiled body, so a later
  edit that hoists a write out of its guard stops the build.
- **The flag reaches the file that reads it, and the clause is what found out that it did not.**
  `STAGE90_XNU_EXIT_POC_FLUSH` is added to `entry_trace.c`'s own compile line as well as to
  `STUB_DEFINES` (which only reaches `entry_stubs.c` and `entry_timebase.c`). The first flag-on build
  after the split compiled `#if STAGE90_XNU_EXIT_POC_FLUSH` to its **default**, i.e. it would have
  produced the flag-off image while reporting the flag as on — and the build stopped, because this clause
  reads the *wrapper's* call count out of the linked image and compares it against the variable. Same
  defect class as `#define X 0` not being "off" to `#ifdef X`; shorter fuse, because here the check and
  the flag live in the same clause.
- **The four offsets are read from the two files that state them, not written here a third time.**
  `CPU_INT_STATE`/`CPU_INTSTACK_TOP`/`INTSTACK_SIZE`/`ACT_CPUDATAP` come from `entry_stubs.c`'s macros and
  from `assym.s` (genassym's answer), and the clause's only job is to require them to be **equal**; the
  six frame offsets come from `entry_saved_state.h` and are also used, at those values, to build the
  matcher for the compiled body — so the header↔binary tie is checked without this file restating
  52/56/60/64/68/72 anywhere. (The header↔genassym agreement is separately gated, by
  `tools/check_saved_state_offsets.py`, which this build runs a hundred lines earlier.) An earlier
  revision of the clause did hard-code all of those numbers, and that is precisely the defect class the
  checker exists to refuse — a third definition of one value, with nothing comparing it.

**Two of the three build failures this clause produced were the check, not the image**, and both are
recorded in `memory/mi4-measurement-defects.md` (175, 176):

- The operand class was written `r[0-9]+`. objdump spells r12 `ip`, r14 `lr`, r13 `sp`, r15 `pc` — **per
  instruction**, not per function — so five of the ladders read 0 on a body that loads every field
  (`80007574: e59c30b0 ldr r3, [ip, #176]`). It failed with "does not load `[#176]` from cpu_data", a
  true-sounding sentence about a false premise. **516's own two clauses had the same narrow matcher and
  passed only because gcc happened to pick `rN` there** — so this is a latent defect 516 shipped with.
- One read was written `<<<"$(awk '...' <<<"$twbod"))"`, and bash appends the stray `)` to the last field:
  `tfrom` became `2152187904)`. Every test used it in a *string* comparison, so the build was green and
  the say line published the wrong number. Found by diffing the say template's literal segments against
  the log line the build produced. Every extracted value now has its **shape** asserted
  (`^[0-9]+$` / `^0x[0-9a-f]+$`), skipped only where the value is legitimately absent.
- The third was 516's `xbod` extent using `sym_next` where the body carries *local* labels
  (`clean_dcacheline`), so the range ended four bytes in; fixed with `next_global`, whose boundary must be
  *strictly* greater than the start because `clean_mmu_dcache` and `CleanPoC_Dcache` share `0x8004575c`.

## The run

**One run, and it is a negative reading. Non-persistent `fastboot boot`, nothing flashed.**

    18:45  preflight_boot_check.sh --allow-xnu-entry          all checks passed, exit 0
    18:46  run_and_capture.sh --allow-xnu-entry
           Sending 'boot.img' (8340 KB) OKAY [0.263s] / Booting OKAY [0.011s]

The USB record for the phone's port (`usb 3-10`, serial `4a2fe00b`), read from the host kernel log:

    18:46:06  USB disconnect, device number 41        <- adb reboot bootloader
    18:46:13  new device 50: idVendor=18d1 idProduct=d00d  Product: Android  <- fastboot mode
    18:46:16  USB disconnect, device number 50        <- fastboot boot handed off to the payload

and then **nothing**: no further enumeration on that port in ~15 minutes, `adb devices` and
`fastboot devices` both empty throughout, and no other Xiaomi/Google device on the bus. (The
`05c6:9008` QDL device that appears in `lsusb` is **not** the phone — it is an unrelated Halium unit on
port 3-3 that entered QDL mode minutes earlier. Section 4a's own warning about unrelated devices is
there because of exactly this.)

No log was captured: `/tmp/cancro-last_kmsg.txt` still holds 516's run 3 (596731 bytes, 18:04). The
power press is the long hold, i.e. a PMIC reset, so DRAM was reinitialised and whatever the payload
wrote into the ram_console is gone rather than merely unread — §4a's step 5.

**What the run does and does not say.** It says the image is not one this device recovers from by itself,
which is new. It does **not** say which change did it, and it does **not** say the exit flush is
dangerous: a compound step whose failure is unattributable is the defect, whatever the cause turns out to
be. The two halves have different risk shapes, and that is the whole basis of the split:

- the **frame reader** adds one call and six bounded loads inside an interrupt that already exists, and
  publishes numbers into the tracer's own channel — the risk shape of every other instrument in this
  image, on a hotter path than most but a *reading*;
- the **exit flush** changes the state of the CPU's caches on a path whose last act is to switch the
  D-cache back on — the risk shape of 516's enter-side repair, which 516's two runs returned from, but
  this step's doc cannot pretend that is proof, because 516's runs did not also carry a new wrapper on
  the interrupt path.

## What is owed

1. **The measurement arm's run** — `STAGE90_XNU_EXIT_POC_FLUSH=0`, `stage90-qcdt.img` sha256
   `67075d64…`, image below. Its risk profile is 516's, whose two runs returned. It answers the question
   the frame reading exists to answer, and it is what 516's doc asked for first ("measure the frame slot,
   and repair the exit the way 516 repaired the enter").
2. **The exit flush's own run** (`=1`) once the measurement arm has returned, so a failure has one
   candidate.
3. **The watchdog's non-recovery, understood rather than noted.** The bone is worth stating precisely:
   the watchdog's *arming* is proved at arm time (`en_after`, `counter_running`, `countdown_plausible`),
   and a *bite* is proved by nothing but a device that came back. Every run from 506 to 515 came back on
   the 25 s bark / 28 s bite — including 512/513 with the OS parked in the kernel's own `WFI` idle path
   and 514/515 in abort storms — and 516 twice on the kernel's own `MACH Reboot`. So the net has fired on
   this device many times and this run is the counterexample. `hw_watchdog.c`'s own header already says
   the honest thing and the docs did not: the bite is a **secure-mode interrupt mediated by TrustZone**,
   so "does not depend on the *payload's* state" was always the claim, never "does not depend on any
   software". A run that can leave the device needing a power press is a cost this project should know
   before it spends it.
4. Still owed from 516: the `r4 = r11 = pc + 2` register-dump anomaly in both its runs.

## The image

`.text` 5,297,664 → **5,298,816** (+1152: the frame reader's body and the seventeen key literals), entry
image **5,519,996** bytes — unchanged and *not* an error, for 516's reason: the copied span is
`[0x80000000, __bss_start)` and `__bss_start` is pinned at `0x80543a80` in both images, so growth in
`.text` eats the fill below `.data` rather than moving the end. `.bss` `0x80543a80` … `0x8059c570` =
**363,248** bytes (516: 363,184). The seventeen new words are 4 bytes each — measured, `nm -S` — so 68
bytes of new `.bss` and a 64-byte section delta, a four-byte difference this doc does not explain by hand
and does not need to: the checkable claims are that the seventeen symbols exist at size 4 and that the
section grew 64 bytes. Entry point `0x80000074`, tree at `0x806e0000` + `0x744c`, boot args at
`0x8059e000`, `topOfKernelData` `0x80700000`, headroom 1,456,784 bytes. **Wrap census 76** (516: 75; the
new one is `--wrap=ml_get_timebase`). 129 fixture mutations refused, `xnu_entry_failures = 0`.

The arm that has not run: `stage90-qcdt.img` sha256
`67075d6439f75086345ffd9202df30874a41f3d7aedf8b24b8a32fde9b450182`, `stage90.bin` `d42968c7…`,
`stage90.elf` `d00e4e33…`, `xnu_arm_entry.bin` `09aa2dfb…`, `stage90_fixture.macho` `52bc9c35…`. **The
clause refactor above is proved to be check-only by that hash**: the image was rebuilt after the offsets
were changed from restated numbers to derived ones, and `67075d64…` is what it was before, byte for byte —
a check that changes the artifact would not be a check. The flag-on counterpart, built with the flag now
reaching the compiler, is `498dbde7…` (`stage90.bin` `1ea61459…`, `stage90.elf` `de62c4ee…`).

The arm that **did** run, and did not come back: `stage90-qcdt.img` sha256
`59618b026ff6978a384ce2d0d2ab8c0a96e30753eae3458df0eadb83c55e3664`, `stage90.bin` `3b0425aa…`,
`xnu_arm_entry.bin` `e4359cae…`. It was built before the split, so its `FlushPoC_Dcache` call was
unconditional — which is what "the image that ran carried both changes" means concretely. The symbols this
step reads: `FlushPoC_Dcache` `0x80045828`, `CleanPoC_Dcache` `0x8004575c`, `CleanPoU_Dcache` `0x800457a8`,
`FlushPoU_Dcache` `0x80045874`, `platform_cache_idle_exit` `0x800462d4`, `ml_get_timebase` `0x80017474`,
`entry_note_timebase_call` `0x80007390`, `__wrap_ml_get_timebase` `0x8047c7f8`, `EntropyData`
`0x805264fc`.

## Safety

One run, non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`, **nothing flashed, and
nothing in this project is ever written to storage** — so the brick half of the standing constraint
("一定要保证不要让设备彻底死机或者变砖") is satisfied by construction and no run can change that. The
hard-hang half was **not** satisfied by this run: the device needed a power-button press, the nets did
not recover it, and the log is lost with the cold boot. That is the finding this doc has to carry, and it
is why the gate's text and §4a of the recovery reference have both been corrected rather than left
claiming a net that holds always. A four-byte difference in `.bss` and a run with no reading are the
price of the step; the device is intact.

    python3 tools/check_experiment_index.py    # ok: 494 row(s) across 91 stage column(s)
