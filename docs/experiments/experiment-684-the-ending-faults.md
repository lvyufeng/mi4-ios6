# 684: the press was spent, the run came back — and the ending it was built on faults

The press 683 unblocked was given. **The device returned and the log came back**, so the arm's own question is
answered — but not with the answer the arm was designed to give. The seam published its pair exactly as
designed, and then **the first instruction of the deliberate ending faulted**: a section translation fault on
`0x0fa0065c`, the restart-reason word. The run did not end on purpose; it was ended by the fault, and the
entry image's own abort path — the net that has always worked — is what brought the phone back.

**One press spent, exactly one gate, exactly one runner, one non-persistent `fastboot boot`, nothing flashed,
nothing written to storage.** The device is up and in adb.

## 1. The press

| time (UTC) | event |
| --- | --- |
| 13:02:54 | `GATE EXIT=0`, **550 stdout lines** — the number the arm's own flags print |
| 13:02:54 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-seam-endrun-88972ba9` |
| 13:03:54 | **`RUNNER EXIT=0`** — returned and captured |

The runner's own measurement of the interval is **18 s** (`the device came back 18s after this run called
fastboot boot`, seen via the host log and the serial). The bytes sent were checked against the gate's reading
of them and were unchanged across the send: `94c95342821abe70…`. The capture is **612,441 bytes**, sha256
`647255967f15c3abfa310dcfa1f4ee85ef61fced8b72e51d340f8019baf5e122`, archived as
`out/stage90/captures/684-endrun-pressed-2026-09-25-last_kmsg.txt`. There was **no firer armed** for this run
— readiness, the gate and the runner were invoked directly, in that order, once each.

## 2. What the arm published, and the operation did nothing

Complete and clean, every key the arm's two writers publish, with `xnu_live_seam_end_run` selecting the
reading exactly as 680 built it to:

```
xnu_live_seam_calls=0x00000001   xnu_live_seam_op=0x00000001   xnu_live_seam_end_run=0x00000001
xnu_live_seam_lr=0x800462dc      xnu_live_seam_sp=0x8054fec8   xnu_live_seam_sctlr=0x30c57879
xnu_live_seam_b0=0x800b2648      xnu_live_seam_b1=0x8047c9c0
xnu_live_seam_a0=0x800b2648      xnu_live_seam_a1=0x8047c9c0
```

* **The seam ran exactly once**, at the exit's own call site (`lr=0x800462dc`, the exit's `bl`), so the window
  was reached and the hook was not entered from the routine's three other callers.
* **The pair came back unchanged** ⇒ the runner's cell is **`CLEAN LINE`**, and on this arm that cell has one
  reading: **the line was not dirty and the clean had nothing to write out.** So 546 §3's mechanism — *the
  pop is answered by a stale line that the clean must write out* — **was not exercised at all**. The arm was
  built to produce that reading or its opposite, and it produced neither: it produced *there was nothing here
  to fix*.
* **`b1` is the right address.** The runner derives the real exit's return site from the image, and
  `b1=0x8047c9c0` **is** it. So the frame the exit's `push` wrote was already correct in memory, read with
  `SCTLR.C=0` out of DRAM (`seam_sctlr=0x30c57879` has C clear).

**So the measured state of the hypothesis is: the seam's slot was not stale.** Whatever kills the boot at the
`pop` is not a dirty line at this address — which is 652's `b1 == a1` arrived at from the other direction, and
now with the operation actually enabled.

## 3. The ending, and the fault

The arm's ending is `entry_seam_end_run`, and the log states exactly where it died:

```
panic(cpu 0 caller 0x80454668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
r0: 0xfffffffc  r1: 0x0000000a  r2: 0x0fa00000  r3: 0x78665501
sp: 0x8054fea8  lr: 0x8047cb08  pc: 0x8047b488
cpsr: 0x80000093  fsr: 0x00000805  far: 0x0fa0065c
```

Every register is the designed ending's own operands, and the disassembly of the arm's image confirms the
instruction:

```
8047b47c <entry_seam_end_run>:
8047b47c:  movw r3, #0x5501
8047b480:  mov  r2, #0xfa00000          ; the restart-reason base, the project's MSM_IMEM_BASE_PHYS
8047b484:  movt r3, #0x7866
8047b488:  str  r3, [r2, #0x65c]        ; <-- THE FAULT:  r2=0x0fa00000, r3=0x78665501
8047b48c:  dsb  sy
8047b490:  movw r3, #0xbfff
8047b494:  mov  r2, #0
8047b498:  movt r3, #0xfc4a
8047b49c:  str  r2, [r3, #-4095]        ; PS_HOLD <- 0, never reached
```

and the closure, from the seam's own body:

```
8047caf8:  bl   800026c8 <entry_live_ready>
8047cb04:  bl   8047b47c <entry_seam_end_run>   ; the seam's call
8047cb08:  ...                                  ; lr = 0x8047cb08, the value in the dump
```

`far = 0x0fa0065c` is the store's own address and `fsr` status `5` is **a section translation fault**. So the
word this project has been calling `RESTART_REASON` **is not mapped in the context the seam runs in**, and
the store could not happen at all.

## 4. That falsifies 674 §1 — measured, not argued

674 §1 corrected 663 §3 with a claim that both 678's design and 681's whole step rested on: *both registers
are already in the page table XNU runs under — the payload maps `0x0fa00000` and `0xfc400000` into
`stage90_candidate_l1`, the table `xnu_handoff.c:318` installs into TTBR0 and keeps live across the
no-return handoff — so step 4 needs no new mapping.* 681 read that off the source, found the Phase-5 line
unconditional, checked two other statements of the same fact, and reported **the claim holds**.

**It does not hold, and this run measured it.** The seam runs while XNU is running, and by then the live
TTBR0 is not the payload's table: `0x0fa00000` is below XNU's kernel window (`[0x80000000, 0xFFFEFFFF]`), so
XNU's pmap has no section for it and the walk finds nothing. A source reading can see *which table the payload
installs*; only a run can see *which table is installed by the time the code that matters executes*, and 681
read the first and concluded the second.

**The same argument condemns the ending's second store.** `str r2, [r3, #-4095]` writes `0xfc4ab000`, which
is in the payload's Phase-4 megabyte `0xfc400000` — equally the payload's table, equally not XNU's. The run
never reached it, so this is **implied and not measured**, and it is named as implied.

**And it means 663 §2 step 4 has the requirement 674 removed.** The forced reset must be reachable from the
context the seam actually runs in. The entry image already owns the tool — `entry_mmio_section`
(`entry_stubs.c:2230`) installs one section into the live table and is used for exactly this purpose for the
GIC (`entry_gic.c:377`) — so the repair is to install `0x0fa00000` and `0xfc400000` the same way before
storing, or to end the run through a path that does not need them.

## 5. Why the run came back anyway, and what that says about the net

**The designed ending failed and the fallback worked**, and the log names the fallback:

```
xnu_live_sleh_pc=0x8047b488  xnu_live_sleh_lr=0x8047cb08  xnu_live_sleh_far_frame=0x0fa0065c
xnu_live_sleh_storm=0x00000009
xnu_entry_sleh_pc=0x8047b488  xnu_entry_sleh_lr=0x8047cb08  xnu_entry_sleh_far_frame=0x0fa0065c
xnu_entry_panic_entered=0x00000001
```

The entry image's own abort path took the fault, recorded it, and its `entry_epilogue` reset the phone — the
same store sequence, from the exception context, which is where every returning run has always got its reset
from. **That is 664's structure working**, and it is worth saying plainly that this press did **not** test
`PS_HOLD` as the frontier's first net: it tested the entry image's epilogue, which is what 653's arm and
every other arm already exercised. The arm 663 designed — *end the run on purpose at the seam* — has still
never actually ended one.

## 6. What the runner read, and one sentence it got right for the wrong reason

The pair cell, the arm's name and the `seam_end_run` selection were all correct, which is 680's repair
reading a real log for the first time. One block was not:

> **DIED IN THE EXIT**  `pre_calls=0x00000001` and `rtcpre_calls=0x00000001` both published and
> `slot_post_calls` did not … so the death is inside `platform_cache_idle_exit`, which is 520's `pop {fp, pc}`
> at the same pc.

The localization happens to be right here — the fault *is* in the exit's call tree — but the reason is not:
the pc is `entry_seam_end_run`'s store, not the pop, and on an ending arm a non-return through the seam is
**by design**. `slot_post_calls` absent means the seam did not return, and on this arm that is expected
whether the ending works or faults. So this block needs the same treatment 680 gave the pair cells — *the
absence is the arm's design, and the fault is what says the ending did not take* — and it is **owed** rather
than invented at the end of a run report.

## 7. What this does to the frontier, and to the goal

**The frontier is unmoved.** The run never reached the `pop`: the seam was entered from the exit's call,
published, and faulted before returning. The boot got as far as it did in 520 — three user-mode fault
records, `storm=9` — and the goal block passes (`open`/`read`/`getpid`/`exit`/`wait` all read, the fixture's
`MH_MAGIC` moved into a user page), but the runner says what that is worth: **it is the floor, not this run's
progress**, because the whole userland phase happens before the death. 「起码要能进入操作系统，把基础驱动跑起来」
is **not** advanced, and **TWRP-to-storage stays withheld**.

What the press *did* buy is three facts no source reading had:

1. **The seam's slot was not stale** — the operation enabled, the pair unchanged, `b1` the correct return
   address. The L2-copy hypothesis is not supported at this address.
2. **`RESTART_REASON` is unreachable from the seam's context**, on a store, by section translation fault.
3. **The ending has never run**, because it faults on its first instruction — so `PS_HOLD` as the frontier's
   first net is still unexercised, exactly as 664 §2 suspected.

## 8. Safety

One press, and the device is not hung, altered or bricked. `preflight_boot_check.sh` ran once (`EXIT=0`, 550
lines) and `run_and_capture.sh` ran exactly once (`EXIT=0`), both under the arm's own derived flags
(`--allow-xnu-entry`) and with `--expect-arm=armed-seam-endrun-88972ba9` declared. One `fastboot boot`, and
**nothing flashed**: nothing this project sent was ever written to storage, so the brick half of the standing
constraint holds by construction. The neighbour `33e80afe` was off the bus throughout (`fastboot` empty
before the send) and untouched. The device came back and `adb` lists `4a2fe00b device`.
