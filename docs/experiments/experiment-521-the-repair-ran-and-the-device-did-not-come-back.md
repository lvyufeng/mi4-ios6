# 521: the repair, run - and the device did not come back

Stage90. Built host-side with `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
**`STAGE90_XNU_EXIT_POC_FLUSH=1`** - the one state change 520's section 8.2 names - plus the four
instrument repairs 520's own run exposed. Gate green, then **one non-persistent `fastboot boot`**,
nothing flashed and nothing written to storage. **The device did not come back**: it needs a power press,
so the run left no log and this step's readings do not exist. What it does have is the non-return, and
that is not nothing: this is the **second** image in this project's history that did not come back, and
the **second** one with the exit-side Point-of-Coherency flush on.

The image is `out/stage90/stage90-qcdt.img`, sha256
`00c630e9639bc18bff547ce08a2d1dbe1fe45ef3a50e0061b4d9a56cdc76aaba`, 8,540,160 bytes. The copied entry
image is `xnu_arm_entry.bin`, 5,519,996 bytes, sha256
`9ab1f61c32e2eb491587fca81d5e99eefc26c847be503f8dc342de86b66d2a79` - the same size as 520's and 519's,
because the copied span ends at a pinned `__bss_start`. `.text` is **5,306,408** (+4,840 against 520's
5,301,568: the capture macro, the second publisher and the widened window), `.data` 206,804, `.bss`
**379,696** - byte-for-byte 519's and 520's, because the five key tables are initialised and live in
`.data`.

## 1. What this arm is

Two things, and the second was already 520's section 11 verbatim:

1. **`FlushPoC_Dcache` in the exit wrapper** - 517's exit-side clean-and-invalidate, which was added at
   this site behind `STAGE90_XNU_EXIT_POC_FLUSH` and had never been run on its own. The prediction,
   written before the run in 520's section 8.2: with the line invalidated, the exit's own
   `push {fp, lr}` writes the only copy of `{fp, lr}` there is - at DRAM, with no valid line to hit -
   and the `pop {fp, pc}` reads DRAM and returns to `0x8047c964`. The verdict was to be the panic's
   *absence*.
2. **The instrument repairs**: the four slot words captured by the caller into the table's own words
   (`entry_slot_capture.h`), the abort site publishing every abort up to `STAGE90_SLOT_AB_MAX` (64)
   instead of a geometric subsequence, and the read window widened to the kernel map's own bounds with
   the rtc note's inner refusals counted.

## 2. The build

`build_entry.sh`'s `xnu_entry_520` clause ran green and printed the wrapper's shape, which is the part of
the instrument that had to be exactly right:

* `__wrap_platform_cache_idle_exit` at `0x8047c958`, **172 bytes** (520's was 100), taking `sp` off the
  register once at `0x8047c964` after its **only** stack movement - one decrement of 8 bytes and one
  restore. The frame size is load-bearing and not tidiness: the slot's address is `E - 4` with `E = X - 8`,
  which is what puts the idle *enter* wrapper's `strd r4, [sp, #-12]!` (the deadline) at the word the
  exit's `pop` reads as `pc`; a 16-byte frame would move the slot out from under that store and stop
  testing 520's mechanism while every surface stayed green. The clause asserts 8, and the first attempt
  at the capture **did** produce a 16-byte frame (a probe compiled ahead of the build showed it), which is
  why the capture is staged in the table rather than passed in registers.
* **8 loads at `[sp-16]`, `[sp-12]`, `[sp-8]`, `[sp-4]`** - four per site, the first group ending at
  `0x8047c98c` *before* the first publication and the second starting at `0x8047c9ac` *after* the real
  exit's call at `0x8047c9a8`. This is the assertion 520's defect 395 needs: gcc is free to hoist, and
  only addresses say what it did.
* **8 stores into a table at word offsets 32/36/40/44**, twice - the header's `pend_*` layout read out of
  the image, so a field reordered in the header without the clause's number moving stops the build.
* `FlushPoC_Dcache` called **1** time, at `0x8047c960`, before the real call: 517's clause compares that
  count against the flag, so the flag and the image cannot disagree.

Symbols: `entry_slot_note` `0x800075d4` (0x98), `entry_slot_ab_note` `0x8000766c` (0x110),
`entry_slot_rtc_note` `0x8000777c` (0x188), `entry_slot_tb_note` `0x80007ba0` (0x94),
`entry_note_sleh` `0x80007904`, `FlushPoC_Dcache` `0x80045828`. Tables (`nm -S`, and the clause asserts
each against its key list): `g_slot_pre` `0x80510114` 48, `g_slot_post` `0x805100e4` 48, `g_slot_ab`
`0x80510038` 40, `g_slot_rtcpre` `0x805100b0` 52, `g_slot_rtcab` `0x80510064` 52, `g_slot_tb`
`0x80510098` 24 - 41 distinct keys, 6/6/7/9/9/4.

**The build found one defect, and it was in the check rather than the image.** The clause's first version
asserted the wrapper holds **4** stores into a table; it holds 8, because the capture macro runs at both
sites. The awk counts the whole body and the assertion was written from one site's shape - so the build
refused an image that was right, with a message about a field reordering. Fixed to 8 with the two sites
named; the second build was green. (A measurement defect of the ordinary kind: the shape asserted was
"what one capture writes" and the body holds two captures.)

## 3. The run

Device `4a2fe00b`. Gate green - it printed `00c630e9639bc18b...`, computed from the file itself. One
non-persistent `fastboot boot` through `run_and_capture.sh --allow-xnu-entry`: `Sending 'boot.img' (8340
KB) OKAY`, `Booting OKAY`. Nothing was flashed and nothing was written to storage.

**The device did not come back within the 180 s window, and has not come back since** (roughly nine
minutes at the time of writing). `adb devices` lists nothing, `lsusb` shows the bootloader's own fastboot
gadget (`18d1:d001`) still configured, `fastboot devices` lists nothing and `fastboot getvar product`
does not answer. So the SoC is not running the bootloader's command loop either: the phone needs a
**power press** (hold Power ~10-15 s, release, press Power normally), and because the payload's log lives
in the top of DRAM, that power cycle loses whatever the run produced. There is no `xnu_live_slot_*`
reading from this arm, and the instrument repairs are unverified on hardware.

## 4. What the non-return says, and what it does not

It is the second one. The ledger, from the runs rather than from the gate's prose:

* **Every flag-off run since 506 has come back**, including runs parked in the kernel's own idle `WFI`,
  516's two runs on XNU's own `MACH Reboot`, and 518's, 519's and 520's - each of the last three inside
  the 180 s window, each ending in `Attempting system restart...MACH Reboot`.
* **Both non-returns are flag-on runs.** 517's first run carried this flush and the frame reader and did
  not come back either, and it is the one run since 506 whose recovery needed a power press.
* The hardware watchdog recovered 506-515 and did not recover either flag-on run.

So the exit-side flush is now the prime suspect, and the difference between this arm and 520's is exactly
one *state* change plus a *measurement* change. The measurement change is very unlikely to be the cause:
520's image also captured, also published and also ran with the cache off inside the window, and it came
back; the repaired capture does the same work in more places (8 loads and 8 stores per pass instead of 4
loads in the callee) and writes the same kind of `.data`. What is different in kind is **a full L1 *and*
L2 clean-and-invalidate executed inside the idle window, with `SCTLR.C = 0`**.

And that is the piece 516 never exercised. 516 established that `CleanPoC_Dcache`'s two-loop form (L1 and
L2) is what reaches the Point of Coherency, and it read that function's body to prove it - but the call
it verified is on the **enter** side, where the cache is *on* and the core is fully powered. On the exit
side the same loops run in the window, with the caches disabled, in the state the idle path enters. A
set/way clean-and-invalidate of the L2 in that state is the one operation in this image that has never
run on this hardware before, and it is the only thing in the image that two non-returning runs share.

**What the non-return does not say** is which instruction wedged the core, and it cannot be known from
this run: there is no log. It also does not say the *cache* is the problem rather than the *flush's
placement* - a maintenance call that hangs, a maintenance call that leaves the L2 in a state the
bootloader cannot come up from, and a maintenance call that is fine but changes the timing of something
else are three different failures with the same signature.

## 5. What the next arm does

The mechanism 520 identified does not *need* maintenance in the idle window. The stale valid line is
created **before** the window: the enter wrapper's `strd r4, [sp, #-12]!` runs with the cache on, and
516's `CleanPoC_Dcache` (`DCCSW` - clean only) writes it to DRAM and **leaves it valid**. Invalidate it
there - in the same call, with the cache on, where 516 has already run the L2 half of a two-loop body -
and there is nothing left for the `pop` to hit:

> **522: the enter side cleans *and invalidates* (`FlushPoC_Dcache` where the enter wrapper calls
> `CleanPoC_Dcache` today), and the exit-side flush stays at 0.** One state change, none of it inside the
> window, and the slice of the mechanism 516 verified (the L2 half of that loop body, cache on) is the
> one it exercises.

Two further consequences worth writing down before the arm is built:

* **The window's own `SCTLR.C`-off store is not the problem and must not be changed.** The exit's `push`
  writing DRAM only is *correct*; what was wrong was a stale valid line surviving to the `pop`.
* **If 522 also fails to return, the suspect moves off the cache maintenance entirely** and onto the
  capture/publish path in the window (which 520's run survived, but with a different shape), and the arm
  after that is a *null instrument*: the same wrapper with the capture replaced by a counter, to separate
  "the readings cost something" from "the flush costs something".

> **Superseded, 2026-09-22:** the arm that was built as 522 is **not** the quote above. It keeps the
> exit-side flush at 0, as predicted here, but its one state change is turning the **D-cache back on at
> the window's near end** rather than making the enter side invalidate - because repairing the copy still
> leaves the `strd` and the window's cache-off store arguing about which one the `pop` reads, while
> re-enabling the cache removes the argument. Experiment 522 section 1 has the reasoning; nothing above
> is changed, and the second of the two consequences listed here stands as written.

## 6. Safety

Non-persistent `fastboot boot` only, one run, through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`; nothing flashed, nothing written to storage. The device is not
bricked and cannot be: the failure is a hang that needs a power press, which is the same failure 517's
first run produced and the same one the ledger has carried since 506. **A hang is the cost of a state
change that turns out to be wrong, and the ledger now has two of them - which is the reason 517's rule
("one state change per image, so a failed run attributes") is what this step was built to obey, and why
the non-return, unlike 517's, *does* attribute: the flush is the only state change in the image.**
