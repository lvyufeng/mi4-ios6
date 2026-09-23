# 572: 535 ran and did not come back - the arm moved a returning death into a non-return

535's one boot was fired on 2026-09-23 at ~01:16 and the device did not return: `run_and_capture.sh`
exited **2**, and both halves of that criterion are in the run's output (adb *and* the host log, which is
551's whole point). **No log exists for this run and none can be recovered** - the payload's ram console
lives in DRAM, and a cold boot clears it - so what this step has is a *state* and not a reading. That
state is decisive on its own, though, because 533's image **did** return from the same cell (568 section
1: `exit 0`, and 568 section 4's death at the `pop`), and the only functional difference between the two
runs is the arm.

This is 571 section 4's outcome table's *first* row it cannot cover: the run never produced the log that
would have separated "the boot survived the idle pass" from "it died at the `pop` again" from "it died
somewhere new".

## 1. The run

```
gate:   ./preflight_boot_check.sh --allow-xnu-entry          exit 0   (one UNREAD - section 5)
run:    ./run_and_capture.sh --allow-xnu-entry               exit 2
boot:   Sending 'boot.img' (8340 KB)   OKAY [  0.263s]
        Booting                        OKAY [  0.011s]
        Finished. Total time: 0.290s
wait:   bounded wait expired after 180s
        adb:      serial 4a2fe00b not listed
        host log: 1530 -> 1530 enumeration(s) of SerialNumber: 4a2fe00b
```

The download succeeded, so the SoC received the image and ran it. The host's own USB log has the shape of
a boot that started and never finished:

```
[2194582.646] usb 3-10: new high-speed USB device 125   18d1:d00d  serial 4a2fe00b   (fastboot)
[2194585.396] usb 3-10: USB disconnect, device number 125        (fastboot boot took over)
              - nothing else. No enumeration, of any vendor or product, for the next 180 s and since.
```

**So the SoC did not come back in any form, and it was not reset by its own watchdog.** A watchdog reset
would have re-enumerated within seconds (520's runs carried `hw_watchdog_counter_running=0x00000001` and
the net is armed in this image too); none happened. The device is hung and needs a power press, which is
the one cell of this phase that costs a hand rather than a boot (517's).

## 2. The log, and the two that survived

| file | state after the run |
| --- | --- |
| `/tmp/cancro-last_kmsg.txt` | **absent** - the capture never happened, and per the runner's own rule the *name existing at all* was the test |
| `/tmp/cancro-last_kmsg.txt.prev` | 596665 B, `f0285b0f…` - the 2026-09-22 death, untouched |
| `/tmp/cancro-last_kmsg.txt.prev.2` | 597641 B, `734062592d…` - **568's run**, the one the gate fingerprinted before this boot, parked by step 2b and still intact |

Nothing was lost: 568's log was parked rather than overwritten, and 520's `.prev` is where it was. The
*cost* is that this run's own readings (`xnu_live_seam_*`, and the `pop`'s shape) were never written to
anything a host can read.

## 3. Why this is the arm, and not the payload

The payload was re-embedded for this build (`stage90.bin` `5867d4ed…`, `01:06:05`) because it carries the
entry image - so the two runs' payload files differ. Their *sources* do not: nothing outside
`xnu_arm_boot/` and `run_and_capture.sh` changed between 568's run and this one, and the entry image is
the only artifact whose content moved (`12684433…` for 535 against `f202f246…` for 533, and 571 section 3
decomposes that difference to 484 bytes of new code plus addresses into the relaid-out entry objects). So
the reading stands: **the arm moved the boot from "dies at the `pop` and returns on XNU's own `MACH
Reboot`" to "does not return at all".**

**And this is not 547 section 4's row (ii).** That row's *shape* - "dies at the `pop`, no log, no return"
- is this run's shape, but its *cell* is 533's image, and 533's image returned (568). Reading this run as
that row would be one value with two definitions, in the place where it costs a boot: what changed here
is not the capture's shape, it is the *image*. 565 section 3's table is about 533's result and is closed;
535's outcome is a **new cell** and the table for the next arm starts from it.

## 4. The pattern this makes three of

| operation in the idle window | scope and site | device |
| --- | --- | --- |
| 517's flag, `FlushPoC_Dcache()` | whole cache, **before** the exit's `push` | **no return** - twice, which is why 569's gate narration warns about it |
| 521's arm | the window, `FlushPoC_Dcache`-class | **no return** (the reader's own text: "521's non-return said the flush is not the answer") |
| **535's arm** | **the slot's 64-byte line, at the seam, inside the exit** | **no return** |
| 533's image (no operation) | - | **returns, with a log** (568) |

Not a mechanism, a *pattern*: every arm this phase has put a **clean to the Point of Coherency** into
that window has cost the return, at three different scopes and two different sites. The common factor is
the clean, and the one cell without it is the one that comes back.

## 5. Candidates, named and *not* measured

Nothing below is established; the log that would have separated them does not exist. They are written
down because the next arm is shaped to bisect them rather than to guess between them.

1. **The clean's granularity is a line, not the eight bytes the arm reasoned about.**
   `FlushPoC_DcacheRegion(slot, 8)` walks whole lines - its body is `and r2, r0, #63` / `lsr r1, r1, #6`
   over a `mcr 15,0,r0,cr7,cr14,{1}` loop, i.e. 64 bytes at a time (measured in the linked image). So the
   clean writes the cache's copies of **all sixteen words** of that line into memory, and the arm restores
   only the two it read. Every *other* word of the line that the window wrote - and 546 section 3's whole
   premise is that this line is the idle thread's own frame, so its neighbours are live - is left holding
   the pre-window value. My reasoning in 571 said the clean is *right* for words the window did not write;
   the words to worry about are the ones it did, and the arm only knows about two of them.
2. **The PoC clean reaches a level whose configuration 544/545 leave open.** Those two steps left "which
   level" and "which geometry" open on purpose (the operand's low bits carry the level, and the factors
   in `proc_reg.h` disagree with 520's capture). A clean to the Point of Coherency is defined by the
   architecture, but whether this SoC's L2 is inside that domain under this image's configuration is
   exactly what an MVA operation assumes.
3. **The interception itself** - the `--wrap`, the `naked` trampoline, the `sp`-as-slot read. Cheap to
   exclude and unlikely (the wrapper is three instructions and the build clause reads them out of the
   image), which is why it is the *first* thing the next arm tests rather than the last.

## 6. The next arm, shaped to be harmless and to bisect without a log

**The seam interception with no operation.** The same switch, the same `naked` trampoline, the same
`lr == STAGE90_XNU_SEAM_LR` filter, the same two reads and the same publications - and then `bl
__real_FlushPoU_Dcache` and return, with **no clean, no invalidate, and no store**. With `SCTLR.C` clear
the two reads are non-cacheable loads and have no side effect on the cache or on memory, so this arm
cannot change what the boot does; its whole content is the *measurement* of the seam.

It is worth one boot because the two cells it can land in are both decisive, and **neither needs a log to
be read**:

- **it returns** (and, if the pop still dies, the log carries `xnu_live_seam_*` at last) - the
  interception is proven harmless and 535's operation is what cost the return, which makes section 5's
  candidates the frontier and the operation's shape the next question;
- **it does not return** - the interception itself is the problem, before any more cache work, and the
  whole approach is refuted cheaply.

Cost: one non-persistent `fastboot boot`; a non-return costs a power press, which this run has already
shown is what the window's operations cost.

## 7. The gate's owed clause, and this run's reading of it

571 section 6 held the boot for the peer session's `_pop_lr_from_elf` pattern (the arm's `--wrap` made
both that derivation and the runner's match nothing, and the gate printed UNREAD **while exiting 0**). I
fired anyway, and the reason is a change in what that clause was protecting: the *runner* derives the
criterion itself again (571 section 5's fix, measured: `lr criterion: 0x800462dc (derived from
…/xnu_arm_entry.elf at run time)`), so this run's criterion was machine-derived and not the pinned
literal, and the address was verified by hand against the same ELF before the boot. What was lost was the
gate's *cross-check* of the literal, not the criterion. The gate's copy is still owed and now matters for
the next run, because the *next* arm reaches the same call through the same wrapper.

## 8. Safety

One non-persistent `fastboot boot`; `flash` is not used anywhere (the runner prints so in its own words);
nothing was written to storage; a brick is impossible by construction. The device is **hung and needs a
power press** (hold Power ~10-15 s, release, press Power normally) - that is the recoverable cost of
this cell, not a permanent death, and it is the reason the next arm (section 6) is built to have no
effect on the machine whatever it does. **TWRP stays withheld**: the goal clause 「如果os已经能进去了的话」
is unmet, and further from met than after 533's run rather than closer.

## 9. State

- 535's artifacts: entry `12684433…` (5519996 B), elf `4f7d8c28…`, qcdt `3d8720c4…` (8540160 B), config
  `97679b66…` - all parked under `out/stage90/captures/535-*`, beside 533's and 520's.
- Logs: `.prev` = 2026-09-22 (`f0285b0f…`), `.prev.2` = 568's run (`734062592d…`); the live name absent.
- Owed before the next boot: the gate's derivation pattern (section 7), and the in-source citation in
  `entry_trace.c` that names `experiment-569-*.md` for this block (571 section 9) - both are build- or
  gate-side and neither spends an artifact.
