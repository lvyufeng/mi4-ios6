# 686: the word the `pop` dies on is planted by this image's own wrapper, and the ending moves past the pop

685 went looking for a writer inside a five-instruction window and did not find one, because the writer is not
in that window at all: it is **this project's own `__wrap_platform_cache_idle_enter`**, one phase earlier, and
it stores the idle loop's own deadline (`cpu_data->rtcPop`) with the cache **on** at exactly the address the
exit's `pop {fp, pc}` will read as `pc`. The exit's `push` cannot correct it - `SCTLR.C` is clear through the
whole window - so the cache keeps the older copy and the `pop`, with `C` back on, is answered by it.

**Two consequences, and one of them is a repair that has never been run.** The seam's pair reads **DRAM** and
therefore says nothing about the copy the `pop` is answered from; and the operation arm - `FlushPoC_DcacheRegion`
to the Point of Coherency, then the two restore stores - is the *right* repair for this mechanism, which means
the one press that arm ever had (03:53:50, exit 2, no log) is 662 §4's first explanation rather than a mystery.
**The arm this step builds ends the run on the far side of the `pop`**, so `xnu_live_slot_post_calls` - the
project's own key for "the exit returned" - is the answer, and both of that arm's cells come back with a log.

**No device was touched in this step. Nothing was flashed, nothing was written to storage, no firer was armed.**
The arm is built, parked, recorded and gated; the press that spends it is the next step.

## 1. What 685 got wrong, and it is one sentence

685 §2 read the abort's four words correctly and 685 §3 read the value as a timer deadline correctly. Its §4-5
then asked where, in the idle exit's window, something *writes* a deadline into the frame, and answered: "not
in this window - the four instructions between `SCTLR.C` and the `pop` contain no store, so the clobber is a
write by something this arm has not touched". The window has no store **because the store is not in the exit at
all**. Both of the words the `pop` eats were written into the *cache* before the exit was even entered.

## 2. The plant, read out of this arm's own image

Three readings, all from `out/stage90/xnu_arm_entry.elf` and all already in the tree's own narration:

**(a) `cpu_idle` loads the deadline into `r4` and keeps it there.**

```
8000d944:  ldr  r5, [r1, #1484]      ; r1 = TPIDRPRW, +1484 = this config's ACT_CPUDATAP -> r5 = cpu_data
8000d984:  add  r6, r5, #224  ; 0xe0 ; r6 = cpu_data + 0xe0  = &cpu_data->rtcPop
8000d98c:  ldm  r6, {r4, r7}         ; r4 = cpu_data->rtcPop   <-- the deadline, in a callee-saved reg
```

`r4` is callee-saved, so it survives every call between that load and the enter wrapper: at the wrapper's
first instruction it still holds `rtcPop`.

**(b) The enter wrapper stores it - with the cache still on - at the address the exit's `pop` will read.**

```
8047c900 <__wrap_platform_cache_idle_enter>:
8047c904:  strd r4, [sp, #-12]!      ; [sp-12] = r4 = rtcPop, [sp-8] = r5; sp -= 12
8047c908:  str  lr, [sp, #8]
8047c90c:  sub  sp, sp, #12
...
8047c94c:  bl   CleanPoC_Dcache      ; Apple's whole-cache clean, still with C set
8047c950:  bl   platform_cache_idle_enter   ; <-- and this is where C is turned OFF
```

The wrapper is called from `cpu_idle` with `sp = X`. Its `strd` therefore writes `[X-12]`. The exit's own
wrapper is called at the same `X`, takes 8 bytes (`str r4, [sp, #-8]!`), and `platform_cache_idle_exit`'s
`push {fp, lr}` takes 8 more - so the exit's frame is at `S = X - 16` and **`[X-12]` is `[S+4]`, the `pc` slot
of `pop {fp, pc}` at `0x8004633c`**. This is not a deduction from the offsets alone: it is the sentence
`entry_slot_capture.h` has carried since 521 - *"`E = X - 8` is what puts the idle enter wrapper's
`strd r4, [sp, #-12]!` (the deadline) at the address the exit's `pop` reads as `pc`"* - and the reason that
header's own clause pins the exit wrapper's frame at **8 bytes** rather than tidiness.

**(c) Nothing after the plant can move the cache's copy back.** The exit's `push {fp, lr}` runs with
`SCTLR.C` clear (measured on every arm: `xnu_live_seam_sctlr=0x30c57879`, bit 2 clear), so its two stores
reach memory and do not update the line. Apple's own `FlushPoU_Dcache`, called from inside the window, cleans
and invalidates to the **Point of Unification** - which is the L1 - and the stale copy that survives it is one
level below. Then `0x80046324` sets `SCTLR.C` and the `pop` at `0x8004633c` reads `[S]` and `[S+4]` from the
cache.

## 3. Which is why the seam's pair has always been a DRAM reading, and a vacuous one

With `SCTLR.C` clear, a load to Normal memory is non-cacheable - it does not consult the L1 or the L2. So both
of the seam's reads (`b0`/`b1` before Apple's flush, `a0`/`a1` after it) are answered by **DRAM**, where the
exit's `push` is correct *by construction*. `b1 == a1` is therefore what this arm's shape produces, not a
verdict about the line: 652 read the equal pair as "the flush did not write DRAM ⇒ candidate (A)", and the
stronger reading is that the measurement cannot see the cache at all.

**This is 653 §3's own hedge, and it settles it.** That section asked whether "the pair says whether Apple's own
L1 flush writes the slot's line back" and answered that the control arm was the precondition for asking. The
answer is that the pair reads DRAM; the L1 flush's write-back, if any, lands at the **PoU** and never shows up
in a `C`-clear load.

**And it corrects 685 §4 from the other side.** 685 wrote that "the operands the operation protects are not the
operands that are wrong". The frame *is* what is wrong, and the operation protects exactly it - the operand it
cannot see is the stale copy the `pop` is answered from, which is the thing the operation is for.

## 4. So the operation arm's one press has a first explanation rather than a mystery

`FlushPoC_DcacheRegion(slot, 8)` walks whole 64-byte lines and issues **`mcr p15, 0, r0, c7, c14, 1`** -
`DCCIMVAC`, clean-and-invalidate **to the Point of Coherency**. On this mechanism that does three things in
order: it writes the stale deadline out to DRAM (over the `push`'s correct words), it discards the line at
every level up to the PoC, and then the arm's step 4 **restores** `[S]` and `[S+4]` from `b0`/`b1` with the
cache still off, followed by a `dsb`. After that the `pop`, with `C` back on, misses everything and is answered
by DRAM with the frame the exit pushed - **the `pop` should return.**

The operation arm was pressed exactly once, at **03:53:50**, and did not come back: exit 2, no log, a power
press. 662 §4 listed three explanations and refused to claim the first one ("the repair worked, the `pop`
returned, and the boot hung somewhere later"), because n = 1 on each side and a hang produces no reading.
**Nothing in this step falsifies that caution, and this step's arm is how the explanation is turned into a
reading** - without letting the machine run on into whatever hung it.

## 5. The arm: the same ending, moved to the far end of the same exit

`STAGE90_XNU_POST_END_RUN=1` calls the **same** `entry_seam_end_run` - one definition, unchanged, `noreturn`,
the two stores and the `wfe` loop - from the end of `__wrap_platform_cache_idle_exit`'s body, after the exit
has returned and after the post site has published, instead of from inside the seam. The interception, the two
reads, the operation, the restore and both publishes are byte-for-byte the operation arm's.

**The reading is one key, and the two cells are opposite:**

| cell | `xnu_live_slot_post_calls` | what it says |
| --- | --- | --- |
| the `pop` **returned** | **present** (>= 1) | the operation's clean-and-invalidate plus the restore carried a `pop` every earlier run died on. **The frontier is not the `pop` on this arm**, and 662 §4's first explanation is measured. The run then ends at the ending, deliberately. |
| the `pop` **died anyway** | absent | the operation was in the image and did not carry it - 574-park repeated with the operation enabled, a cell the tree has never filled. |

**Both cells come back with a log**, and that is the design's whole point: the ending's first store is the one
684 measured faulting (a section translation fault on `0x0fa0065c`, taken by the entry image's own abort path,
which then resets the phone through `entry_epilogue`), and a run that dies in the `pop` reaches the same
epilogue. There is no cell of this press that needs the boot to survive, and no cell in which the machine is
left to continue into an unknown hang.

**Why the ending could not simply be left at the seam.** 678's arm ends the run *before* the `pop`, so it can
never say whether the operation repaired it; this one ends it *after*, so it never says what the boot does
next. The two are refused together by the build and by `entry_trace.c`'s `#error`s, and both are refused
without `SEAM_POC=1`: an ending behind an operation the image does not have would be read as a verdict about a
window nothing repaired.

## 6. The build, and what the clauses caught

The entry image was built with the operation arm's switches exactly (`ENTRY_TRACE=1 REAL_ARM_INIT=1 SLOT_NULL=1
SEAM_POC=1 SEAM_MEASURE=0 IDLE_STACK=1 ISTACK_SEPARATE=0 IDLE_NO_CACHE/EXIT_POC_FLUSH/IDLE_NO_SLEEP=0`) with
`SEAM_END_RUN=0 POST_END_RUN=1` in place of `SEAM_END_RUN=1 POST_END_RUN=0`.

**Two defects of this step's own making were caught by the clauses rather than by a run**, and both are the
project's standard defects:

* **A clause that read a variable another region defines.** The new wrapper clause was first placed beside the
  seam's copy of `entry_seam_end_run`'s call count - and `sxw_body` is defined four hundred lines below that,
  so the build stopped on `sxw_body: unbound variable` under `set -u`. The repair is placement, not a default:
  the clause now sits with the 520 clauses it borrows their disassembly from.
* **Unescaped backticks in a double-quoted `layout_fail` string** - `\`noreturn\`` written raw - which the
  shell answered with `noreturn: command not found` *inside the message*. This is the third time this project
  has paid for that specific character (679, 683, here), and the rule is already in the file's own comments.

**And one clause was relaxed, with its reason written into the clause.** `sxw_inc == 1` ("the wrapper gives the
8 bytes back exactly once") failed: with the ending called last, gcc emitted `bl entry_seam_end_run` and **no**
`add sp, sp, #8`, because it knows the callee cannot return and restoring `sp` would be dead code. That clause's
own stated reason is *"the wrapper returns to its caller"*, and on this arm it does not - so the assertion is
conditional, and what replaces it is the pair of properties the neighbouring clauses assert: the frame
decrement is still once by 8 (the slot the *pre* reading is taken at, which this arm still takes), nothing
follows the ending's call, and the ending's own body is a loop it cannot fall through. The arm's disassembly
reads exactly that:

```
8047c9c8:  bl   8000766c <entry_slot_null_note>   ; the post site, published first
8047c9e4:  bl   80006f2c <entry_note_pcx>
8047c9e8:  bl   8047b47c <entry_seam_end_run>     ; and the body's last call
```

### 6.1 The identity key, because the log has to say where the ending is

This arm's log differs from 653's by **one key** (`xnu_live_slot_post_calls`, present only if the `pop`
returned) - a difference in what *happened*, not in which arm *ran*. So the seam publishes
`xnu_live_seam_post_end_run` beside `xnu_live_seam_end_run`, unconditionally (both endings are reached after the
seam), and the build asserts the string is in the image the way it already asserts `xnu_live_seam_op`'s.

## 7. What else moved, and it is the runner's owed sentence

684 §6 recorded that the runner's `DIED IN THE EXIT` block attributes a non-return on an **ending** arm to 520's
`pop`, "and it is owed rather than invented at the end of a run report". It is paid here, for both endings, and
the reason it matters now is that this arm produces logs of *both* shapes:

* `run_and_capture.sh` reads the log's own `xnu_live_seam_post_end_run` and, when it is 1: the `PASS` branch
  (`slot_post_calls >= 1`) says the death this log also reports is **the ending's own store and not the pop**;
  the `DIED IN THE EXIT` branch says the localization is the press's **negative** answer (the pop ran and died
  anyway); and clause (5)'s `seam_end_run == 0` branch says the ending is on the far side.
* `tools/verify_press_ready.sh` row 4 gains a third arm name: the entry reading plus `POST_END_RUN=1` and the
  pop's own consequence, so the pre-press narration cannot print 653's or 678's cells over this arm - the
  defect 683 repaired there for 678's arm, one key over.

## 8. The lane crossing, and it is the second one of its kind

The arm's switch is a **key in a record**, and the record's key set is compared for equality against a list in
`stages/stage90/preflight_boot_check.sh` - the other lane's file (675 §1 measured the two clauses). The gate's
converse clause refuses any record key it does not print, so without the edit **every press of this arm would be
refused before the device was touched**, exactly as 678's was.

The edit is the same class as 683's, made under the same authorisation (the operator chose "I make it now" for
that change one step earlier, and the peer session's owner has been unreachable for days with the work waiting)
and recorded the same way: **one more name this gate prints**, plus the two prose counts that name the
`ENTRY_CFG_KEYS` variant subset moving from nine to ten. No safety clause moves - the entry-sources manifest
comparison, the storage tripwire, the arm-identity comparison and the exit census are byte-unchanged. The gate's
census is measured after the edit and printed in §9.

**One thing this step did not do, named rather than taken:** the gate refuses a record carrying both of the
*same switch's* two arms (`SEAM_POC` and `SEAM_MEASURE`) and has no equivalent clause for the two *endings*
(`SEAM_END_RUN` and `POST_END_RUN`), because the build refuses that pair before a record can exist. Adding a
second refusal to the peer's file would be a widening of this crossing, and it belongs to that lane's owner.

## 9. State

**The entry image**, built with the operation arm's switches exactly (`ENTRY_TRACE=1 REAL_ARM_INIT=1
SLOT_NULL=1 SEAM_POC=1 SEAM_MEASURE=0 IDLE_STACK=1`, and `SEAM_END_RUN=0 POST_END_RUN=1` in place of
`SEAM_END_RUN=1`), two independent invocations minutes apart and **bit-identical**:

```
xnu_arm_entry.bin            ee7aca87c0140afde826d1586c67e10566003355a39a3c80d90f61879c320e98  5519996
xnu_arm_entry.elf            bec07bdbacab75ada140fe8dd256419c5a7e99cd02c9f716b1d26d7932cec11a  6698696
xnu_arm_entry-config.txt     b1099c85f0ced65bc43c248a55f4ebb137544e25e360e25d60f18ee2160dc11f      810
xnu_arm_entry-sources.txt    e1af91e0423510473577511b0b35a9485c5efd54288f1913e61fa8d80731c6ae     2184
```

The build's own census is clean: `--selftest: all 31 mutations were refused`, the record carries the 15
arm keys `ENTRY_ARM_KEYS` names plus the two artifact keys, and `xnu_entry_535` prints this arm's name
rather than 653's or 678's. The config's record names the arm in its last two lines
(`SEAM_END_RUN=0` / `POST_END_RUN=1`) and is bound to the bin by `SHA256` and `BYTES`, both of which
agree with the live artifact.

**The payload**, `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh` — the invocation the gate
names — and the point of the build is what did *not* move:

* `stage90-build-config.txt` is **byte-identical** to the 678 arm's and to the 653 arm's
  (`6c2b6038…`), and so is `stage90_fixture.macho` (`52bc9c35…`). Those are the two members that do
  not contain the entry image.
* Of the 544,077 bytes that differ between this payload and the 678 park's, **every single one lies
  inside the embedded entry blob**: the two difference lists are *equal* after a uniform shift of
  494,236 bytes, which is `stage90_xnu_entry_blob`'s VMA (`0x80a9c`) minus the payload's base
  (`0x8000`) — so the blob occupies `.bin` `[494236, 6014232)` and not one byte of the payload's own
  code differs. That is the 678 record's claim, now measured rather than asserted.
* Every payload artifact keeps its size (`6015492 / 6077564 / 6019072 / 8540160`), the entry image's
  size included: the switch changes no code size anywhere.

```
stage90-qcdt.img             1e2abec44e50ffd768b3c175a9b4603566d9e8cff037b8ad0829db43a04a8d87  8540160
```

**The park**: `out/stage90/frozen/armed-post-endrun-ee7aca87` — eleven files, plain `cp`, hashed in
place, `tools/verify_revert_set.sh out/stage90 --set=armed-post-endrun-ee7aca87` **exit 0** with all
eleven `ok` and all five manifest members agreeing. The record is eleven lines appended to
`stages/stage90/revert-set.txt` by hand; **nine of the eleven differ from the 678 park's and the two
that agree are the two that do not carry the entry image** — which is why the count is nine rather than
one, and the record says so.

**The gate and the readiness chain are GREEN**, which is the one thing the 678 record could not say:

```
tools/verify_press_ready.sh   →  ok: 5 check(s), exit 0
  the arm is named by a reading     686's POST-END arm (STAGE90_XNU_POST_END_RUN=1) ...
  the gate accepts this tree        exit 0 under '--allow-xnu-entry' (derived from the arm's own switches)
  the press would be caught         adb lists 4a2fe00b as 'device'
gate flags  --allow-xnu-entry
the run     ./preflight_boot_check.sh --allow-xnu-entry
            ./run_and_capture.sh --allow-xnu-entry --expect-arm=armed-post-endrun-ee7aca87
```

The flag set carries **no** `--allow-hw-watchdog-selftest`: the flags are a property of the arm's own
switches, and the self-test switch is 0 on this arm. The park was found by hashing the live
`stage90-qcdt.img`, and exactly one set in the record records those bytes.

**One correction was made to readiness in this step, because 684's log falsified a sentence it was
printing.** Row 4's shared text said "the EXPECTED row is STALE LINE, WRITTEN OUT, i.e. `a1 ==` this
log's own `rtcpre_pop`". 684's log measured the *other* cell — `b1 == a1 == 0x8047c9c0`, an equal pair,
on a run whose `pop` then died — and §3 above is why: the pair is read with `SCTLR.C` clear and is
therefore a DRAM reading in which the exit's `push` is correct by construction. The sentence and the
`CLEAN LINE` clause beside it now carry that, in the file that speaks before a press.

**The device**: `adb` lists `4a2fe00b device`, `fastboot devices` is empty, and no firer is alive (no
`press-on-clear`, `heartbeat` or `press-watcher` process). The 678 arm's window closed on its own at
13:21:30 UTC. **Nothing has been armed for this arm** — a re-arming is the next step, and it is owed.

## 10. What this does not do

**It does not enter the OS and it does not move the frontier.** No device was touched: no `fastboot boot`, no
flash, nothing written to storage, no firer armed. 「起码要能进入操作系统，把基础驱动跑起来」 is **not advanced**
by this step - what it advances is that the next press cannot come back unreadable, whichever cell it lands in.
**TWRP-to-storage stays withheld** until a boot is observed entering the OS.
