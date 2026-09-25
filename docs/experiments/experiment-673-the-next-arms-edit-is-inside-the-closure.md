# 673: the arm after the press is one edit away, and that edit is inside the closure

The press is owed and the window is armed (672). This step asked the question that decides whether the
**next** arm can be built the moment the reading lands: *is 663 §2's arm — publish the pair, force the
reset, never return — a build-only step, or does it need source that does not exist yet?*

It needs source that does not exist yet, it is a small edit, and **the edit is inside the manifest the
gate recomputes** — so it cannot be made while a firer is armed, and the press is the thing that releases
that constraint. Nothing was built, nothing was sent, no device was addressed, no byte was written.

## 1. The seam always returns — measured, and with a control

`entry_seam_flush` (`stages/stage90/xnu_arm_boot/entry_trace.c:2250`) is the one place the intercepted
`FlushPoU_Dcache` reaches. Its body reads the slot, runs the flush, optionally runs 535's operation, reads
the slot again, publishes, and then **falls off the end** at `:2316`:

```
2296    sctlr = entry_sctlr();
2298    g_seam_calls++;
2299    if (entry_seam_publish(g_seam_calls) != 0u && entry_live_ready() != 0u) {
...         entry_live_write("xnu_live_seam_*", …);       ← the pair, and which arm read it
2315    }
2316 }                                                       ← return
```

and the wrapper above it makes the return go back to the call site:

```
2324  __attribute__((naked)) void __wrap_FlushPoU_Dcache(void)
2326      "mov r0, sp\n\t mov r1, lr\n\t b entry_seam_flush\n"     ← `b`, so no frame of its own
```

**There is no write to the watchdog, no loop, and no call to any reboot path in that body.** The three
arms of the seam are `SEAM_POC=1` (535's operation), `SEAM_MEASURE=1` (the same interception with the
operation removed) and neither — plus a build-time `#error` (`:437`) asserting the first two are
exclusive. **All three return to the idle exit, and the idle exit is what dies at the `pop`.** So no
existing switch can end the run at the seam, and 663 §2's step 4 has nothing to switch on.

The payload side has nothing either: the only deliberately-ending primitives are
`STAGE90_HW_WATCHDOG_SELFTEST` (the spin the armed arm uses) and `STAGE90_DEADMAN_SELFTEST`, and both end
the run by *waiting* rather than at the seam. `platform_reboot()` (`stage90_main.c:1042`) does own both
reset writes, and its bite call sits under `#if STAGE90_HW_WATCHDOG == ARMED` — but `platform_reboot` is
the **failure** path, reached from the payload's own abort handling, not from the seam.

**The control matters here, and §4 records why:** the same conclusion was first drawn from a `grep`
against a path that does not exist, whose silence proved nothing. The redo against the real path returned
the seam's switch block, the `#error`, the publish calls and the wrapper — so the extractor could see this
file, and the absence is a measured absence.

## 2. What the edit is, and why it is small

The resource 663 §3 chose for the forcing is the watchdog bite at `0xf9017014`, and the reason it is
reachable from *this* side is already established: the entry image maps the `0xf9000000` megabyte (the GIC
and the timer at `0xf9020000` are its own mappings, 658), so the write needs no new mapping and no
payload call. So the arm is a block in `entry_seam_flush`, after the publish:

```
#if STAGE90_XNU_SEAM_END_RUN            /* a new switch, and deliberately a third arm of one seam */
    hw_wdt_write(MSM8974_WDT_REG_RST, 1u);
    for (;;) { __asm__ volatile ("wfe"); }
#endif
```

— plus the mutuality assertion next to `:437` extended to three arms, because a switch that can be set
against `SEAM_POC` silently produces an arm nobody named. Its verdict is the one 663 §3.1 registered: the
phone comes back (the net works, and the design is sound) or it does not (the net is broken here, and the
design would have been a wasted press) — read as a **time**, which is what 669 built the runner to
measure.

## 3. The constraint that makes this a sequencing fact and not a preference

`xnu_arm_entry-sources.txt` is *"every regular file in `xnu_arm_boot/`, by content"*, and the gate
recomputes that list and **refuses when the two differ** (`preflight_boot_check.sh:1041`, `:1063`, `:1171`
— the manifest is written *by* `build_entry.sh` at the moment of the build, so it agrees with the image
and not with the tree). So an edit to `entry_trace.c` makes the gate refuse the arm currently in `out/` —
which is the arm the armed launcher would send. The two facts therefore compose:

* the edit **cannot** be made now: it would make the gate refuse the owed press (a press spent on nothing),
  and 660's closure rule forbids moving the entry sources while a firer is armed;
* the edit **does not need to be made now**: it is only needed to build the arm *after* the press;
* and the press **releases the constraint by itself**, because the launcher fires exactly one runner and
  then exits.

So the order is not a schedule chosen by taste: **press → the window closes on its own → edit, build,
park, record, gate → re-arm.** If the operator does not press, the window closes at its deadline and the
edit is safe then too. Either way, nothing in this step is on the press's critical path — which is exactly
what the question was asked to establish, and the answer is that the next arm's build can start the
minute the reading lands, from a tree nobody has to wait on.

## 4. The false absence, recorded — it is m671's shape, forty minutes after the rule

The first probe of §1 was:

```
$ grep -rn 'STAGE90_XNU_SEAM' --include='*.c' xnu_arm_boot/
```

against the **top-level** `xnu_arm_boot/`, which does not exist — the entry sources are under
`stages/stage90/xnu_arm_boot/`. The command printed nothing, and *"there is no seam switch that ends the
run"* was two sentences away from being written down as a measurement on that silence. What caught it was
the next probe: looking for the seam's implementation returned `ugrep: warning: xnu_arm_boot/: No such
file or directory`, i.e. the extractor had never looked at anything.

This is m671 exactly — *a whole-tree absence claim is a claim about the extractor's scope* — recurring in
the hour after the rule was written, in a lane whose own memory file says to put a known-present control
through the same command. The rule was right and was not applied. The redo did apply it: the same pattern
against the real path printed the switch block, so the silence is now a reading.

## 5. Safety, and what this does not do

Read-only throughout: `grep`, `sed`, `Read` over sources in the tree, and the two device-list reads the
readiness tool has always made. **No build, no byte written under `out/`, no edit to
`stages/stage90/xnu_arm_boot/**` (the very thing §3 says must wait), no gate or runner invocation other
than none at all, no `fastboot`, no boot, nothing written to storage, the neighbour `33e80afe` untouched.**
The armed launcher (pid 426955, deadline 13:21:30 UTC) was left alone: a `tail` of its log and a
`ps` of its pid, nothing else.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no
boot and no reading. It names what the *next* arm's build will need, which shortens the interval between
the press and the frontier's answer but is not the answer. **TWRP-to-storage stays withheld**, because
「如果os已经能进去了的话」 is unmet.
