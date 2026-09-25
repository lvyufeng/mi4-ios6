# 665: the returning run entered XNU's reboot path, spun in it, and came back anyway — and the fault the frontier pre-registered as a data abort was a prefetch abort

664 §2 named the mechanism that ends a returning run: the frontier faults, a handler records it,
`entry_epilogue` writes `PSHOLD ← 0`, the phone reset. This step reads the **one returning run's own log**
(`out/stage90/captures/650-owed-run-park-574-2026-09-24-last_kmsg.txt`, the 17:24:03 park form) against that
account, and it corrects two things and leaves one question open with a named next reading.

Nothing was built, no device was touched, and no closure member was edited. Every reading below is a `grep`
over the archived log or a `sed` over the XNU copy in `external/`.

## 1. The fault is a **prefetch** abort, and its address is the word the frontier pre-registered

The log's own line:

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x5006e74
```

`0x05006e74` is `rtcpre_pop` — the stale word the whole frontier discussion has been about, and the value 653
pre-registered as the *expected* reading of the seam's pair. The idle exit does `pop {fp, pc}`; the popped word
went into `pc`; and the **fetch** at that address faulted. That is what the log says, and it is the same
conclusion 642's join reached from the other side.

**So 664 §2 named the wrong handler.** It said *"the run dies at the idle exit's `pop {fp, pc}` — a data abort —
and `fleh_dabort` ends: `entry_epilogue("exception: data abort")`"*. A `pop` whose **value** lands in `pc`
faults on the *instruction fetch at the loaded value*, which on ARMv7 is a **prefetch abort**, and the entry
image has a handler for exactly that:

| handler | address | source | ends in |
| --- | --- | --- | --- |
| `fleh_prefabt` | `0x8000ae34` | `entry_stubs.c:9295-9318` | `entry_epilogue("exception: prefetch abort")` |
| `fleh_dataabt` | `0x8000af00` | `entry_stubs.c:9321+` | `entry_epilogue("exception: data abort")` |

**Both** end in `entry_epilogue`, and both `why` strings ship in the image (one occurrence each in
`xnu_arm_entry.bin`), so the *conclusion* of 664 §2 survives: whichever handler took the fault, the path runs
the report and then `PSHOLD ← 0`. What was wrong was the name of the handler, and it is worth correcting rather
than glossing because the correction is what makes §3 possible: **the prefetch handler publishes its own
record** (`xnu_entry_prefetch_abort_ifar`, `_ifsr`, `_lr`, `_pc`, `_spsr`, `_ttbr0`, `_ttbr1`, `_ttbcr`,
`_sctlr`, `entry_stubs.c:9306-9315`), and that record is the instrument that would settle §3.

## 2. `MACH Reboot` is in the log — and the two print sites prove the path it names cannot reset

Two lines, eight lines apart:

```
4002: panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x5006e74
4009: Attempting system restart...MACH Reboot
```

Neither string is printed from anywhere else. `external/xnu-4570.1.46`:

```
osfmk/kern/debug.c:662-668
static void kdp_machine_reboot_type(unsigned int type)
{ printf("Attempting system restart..."); PEHaltRestart(type); halt_all_cpus(TRUE); }

osfmk/arm/machine_routines.c:344-355
__attribute__((noreturn)) void halt_all_cpus(boolean_t reboot)
{ if (reboot) { printf("MACH Reboot\n"); PEHaltRestart(kPERestartCPU); }
  else        { printf("CPU halted\n");  PEHaltRestart(kPEHaltCPU); }
  while (1); }
```

So the returning run **reached `halt_all_cpus(TRUE)`**: it printed `MACH Reboot`, called `PEHaltRestart`, and
entered `while (1)`. And 663 §3.2 measured what those calls do — `PE_halt_restart` is an unfilled `.bss` slot
(`0x805858d8`) and **both** `haltRestart` overrides return `-1` — which the disassembly of `halt_all_cpus`
agrees with (`80011ea0 bl printf` / `80011eb0 bl PEHaltRestart` / `80011eb4 b 80011eb4`).

**And the phone came back anyway.** That is the reading, and it is worth stating in this direction: *not* "XNU
reset the machine", but **a returning run passed through a reboot path that provably does nothing and reached a
one-instruction spin, and the reset that actually returned it came from somewhere else.** 663 §3.2's argument
was disassembly-only; this is it confirmed on hardware, in the one log that exists.

**It also retires a sentence in the gate — which is the peer lane's file, so it goes by message and not by
edit.** `preflight_boot_check.sh:1319` reads:

> *"the last three **returns** (518, 519, 520) came back with XNU's own panic and its own `Attempting system
> restart...MACH Reboot` in their logs, **so XNU reset the machine itself**."*

The premise is the same two lines §1-§2 just placed, and the inference is the one this log refutes: the print
is produced by a path whose only remaining act is an infinite spin. The gate also uses that inference to
dismiss its own watchdog row (*"a `MACH Reboot` is a return path that does not need the net"*), which is the
direction that costs something — it is a reason not to look at the net. §4 records the message; **no edit to
the gate was made.**

## 3. So exactly two candidates reset the returning run, and the field that would name one read NULL

With XNU's own path removed by §2, the candidates for the reset in the 574-park run are:

1. **the entry side's `entry_epilogue`** — `PSHOLD ← 0` at `entry_stubs.c:4229` (664 §2), and
2. **the payload's armed watchdog bite** — `0xf9017014`, due 28 s after an arming the log confirms
   (`hw_watchdog_enabled=1`, `counter_running=1`, `countdown_plausible=1`, `disarm_hw_watchdog_en=1` at the
   jump).

**And the log does not say which, because the instrument that would name it read zero.** `entry_epilogue`
prints its caller's reason string as the report's first line — `entry_write(g_why)` at `:3840` — and the run's
own record of that pointer is:

```
xnu_entry_why=0x00000000
xnu_entry_why_byte=0x00000034
```

The header in the log reads `MI4IOS6_STAGE90_XNU real XNU entry: 47`. `g_why` is NULL, so the two characters
after the colon are not a string at all — they are what the reader found at physical address 0, which is why
the header is two bytes of garbage. **This is the third time this exact field has come out wrong, and the
file says so about the other two**: `entry_stubs.c:1870-1880` records the *empty* case and *"in the storm run
the same line read `47`"*, and `:2835` records `g_why` reading `0x800e3d00` where the caller passed
`0x800ce9f4`. So the field is known-bad and 664 §2 leaned on neither; the point here is narrower and it is the
point of this step: **the one field that names the reset path is the one field that does not arrive.**

**A second reading, and it is the one that decides §3's open question — with a caveat that must be stated with
it.** `fleh_prefabt` publishes `xnu_entry_prefetch_abort_*`, and the fault *is* a prefetch abort, so if
`fleh_prefabt` took it those keys should be in the report. They are **absent** from this log:

```
grep -oE 'xnu_entry_prefetch_abort_[a-z0-9_]*=' <log>   ->  (nothing)
```

Two explanations, and this log cannot separate them:

* **the ring dropped them.** The same report carries `xnu_entry_kv_dropped=0x000092b3` — 37 555 records refused
  for want of room — and `xnu_entry_kv_in_dram=0x00000000`. A dropped key and a handler that never ran produce
  the identical absence, which is the defect `mi4-silence-is-a-reading-only-if-success-is-silent` is about.
* **a different handler took the fault**, and the entry report in this log has another origin — which is the
  reading that would leave candidate 1 above unsupported.

**So the honest status of 664 §2 is: the *code* is proven (`entry_epilogue` does `PSHOLD ← 0` and both abort
handlers reach it), and *that it ended this run* is not.** The three keys that would say so are the nulled
`why`, the absent `xnu_entry_prefetch_abort_*`, and `xnu_entry_kv_in_dram=0`.

## 4. The one finding that belongs to the peer lane

`stages/stage90/preflight_boot_check.sh:1319`'s inference (§2) is a claim in a file this lane must not edit —
`preflight_boot_check.sh` is `run-experiment-526`'s file. The rule is the one this project has settled twice:
**a finding you can measure in the other's file is a message plus a document in your own lane, never an edit.**
The message is sent; this section is the record of what was in it and why it matters to that file specifically:
the sentence is used there to dismiss the file's own watchdog paragraph, so a wrong inference in it removes a
reading — `mi4-a-claim-in-a-comment-is-not-a-check`'s most obstructive form, a claim that supplies a reason not
to look somewhere.

## 5. What this changes about the next arm — and it is a requirement, not a preference

663 §3.1's rehearsal arm asks one bit: *force the bite alone; does the phone come back?* §3 above shows that
bit is **not readable from the outside** — a returning run cannot be attributed to the bite, because the
*other* net may have been what returned it, and the run's own report cannot name its caller. So the arm needs a
third thing beyond 663 §3.1's design:

> **It must make the reset path's identity a value that survives.** Two cheap forms, either sufficient:
> publish a distinguishable marker *before* forcing the bite (so the report's own tail says which net the arm
> chose), or write the reason through a field that is not `g_why` — which §3 measured to be NULL in the run
> that needed it.

That is the same shape as R2 (`revert-set.txt` being the one record the build does not write) and as 653's
`SLOT_NULL` rule: **the arm's identity has to be produced by something that cannot be overwritten by the thing
under test.** It is added here rather than left to the arm's build step because it is the difference between a
press that answers the question and a press that answers *a* question.

## 6. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent. The phone is dark and needs a power press.
* **It does not establish which net reset the returning run.** It removes one candidate (XNU's own path) by
  source and disassembly, and it leaves the other two open with the field that would decide them named.
* **It does not build anything, and changes no closure member.** The gate, the runner, the readiness tool,
  `out/` and `xnu_arm_boot/` are all untouched; no watcher was armed or replaced. The armed watcher (pid
  3454466) is still the only thing that can press, and it has no second fire.
* **It does not re-read the log's own ordering as a timeline.** The RAM console is a ring written by more than
  one writer with no timestamps in it, so §3's "later in the buffer" is not claimed as "later in time"; the
  two facts §2 uses — *these two strings have these two print sites* — do not depend on the ordering.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 7. Safety

No device action of any kind: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**.
Every reading is host-side and read-only — `grep`/`sed` over the archived capture
`out/stage90/captures/650-owed-run-park-574-2026-09-24-last_kmsg.txt`, `sed` over the three XNU sources that
print the two strings and define the two handlers, `nm`/`grep -a` over `out/stage90/xnu_arm_entry.elf`/`.bin`,
and one `SendMessage` to the peer lane. No build, no edit to any file in 660 §5's closure, and **no edit to
`preflight_boot_check.sh`**, which is the peer lane's file. `fastboot boot` only, never `flash`.
