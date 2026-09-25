# 692: the press — the storage probe dies on the gate it read first, in a megabyte the record said was mapped

The press 692 armed was fired. **The device returned and the log came back** (exit 0, **17 s**), and the arm
gave a reading on the second key it publishes and then stopped: the log carries
**`xnu_live_storage_calls=0x00000001`**, `_live_state=0x00000001`, `_section=0x00000f98`,
`_hc_mem_section=0x00000f98`, `_windows_share_section=0x00000001` — and **nothing after them**. No `_map`,
no `_bcr`, no `_gate`, no `_loads`. The probe's **first dereference of the new device block took a data
abort**:

```
panic(cpu 0 caller 0x80454668): kernel abort type 4: fault_type=0x1, fault_addr=0xfc4004c0
r0:   0x80487fc8  r1: 0x0000000a  r2: 0xde500000  r3: 0xfc400000
r4:   0xf9824000  r5: 0x8051a000  r6: 0x8051a0e0  r7: 0x00000000
r8:   0x00000001  r9: 0xc0492b10 r10: 0x800ba588 r11: 0x800b2648
r12:  0xde58bdbf  sp: 0x8054fea8  lr: 0x001ffff3  pc: 0x8000d0c4
cpsr: 0x80000093 fsr: 0x00000005 far: 0xfc4004c0
```

**And the register dump names the site by itself.** `0x8000d0c4` is `entry_storage_probe+0xcc`, and the
built image's disassembly there is unambiguous:

```
8000d0b8:	movw	r0, #32712	; 0x7fc8
8000d0bc:	movt	r3, #64576	; 0xfc40        -> r3 = 0xfc400000
8000d0c0:	movt	r0, #32840	; 0x8048
8000d0c4:	ldr	r1, [r3, #1216]	; 0x4c0          <-- the fault
8000d0c8:	ldr	r5, [r3, #1220]	; 0x4c4
```

`r3` matches the dump, `r3 + 0x4c0 == 0xfc4004c0 == far` to the byte, and the key string the fault was
about to be published under is at `0x80487fc8` = `"xnu_live_storage_bcr"` — **the value of `r0` in the panic
dump**. So the panic's own registers say *which load died and what it was measuring* rather than leaving it
to be inferred from `pc`.

**The premise the arm was built on is falsified by the arm's own log.** 692's `revert-set.txt` block and the
readiness narration both carried the sentence *"that megabyte is already mapped in the context the probe
runs in, proved by `entry_epilogue`'s own PS_HOLD store at `0xfc4ab000` on every returning run"* — and
`fsr_frame=0x5` is a **section translation fault**, i.e. the live table does not map the `0xFC4` megabyte at
all. §3 shows the same class of failure *already measured* in the previous arm's parked capture, which is
where the sentence should have been checked. §4 shows the address half of the gate's own storage tripwire
printed `none` for the image that carries this address.

**One press spent**: one readiness chain (**5/5**), one gate (**exit 0**, 568 stdout lines), exactly one
runner, one non-persistent `fastboot boot`. Nothing flashed (25 × `persistent_write_attempted=0x00000000`,
none nonzero), nothing written to any storage controller (`xnu_live_storage_writes` is not even published,
because the run died before that line), and no device state changed but the boot. The firer was
`press-on-clear.v5.sh` (sha `7f23cae5…`), armed by this step for `armed-storage-0da7313b` with the flags it
derived from the arm's own switches (`--allow-xnu-entry`).

## 1. The press

| time (UTC) | event |
| --- | --- |
| 15:55:16 | the launcher armed for `armed-storage-0da7313b`, flags `--allow-xnu-entry`, sha `7f23cae5…`, budget 600 s |
| 15:55:17 | the phone is on the bus (fastboot `[]`, adb `[4a2fe00b device]`); neighbour `33e80afe` absent |
| 15:55:51 | **readiness exit=0** — 5 of 5, including `the press would be caught` |
| 15:55:51 | `preflight_boot_check.sh --allow-xnu-entry` |
| 15:56:18 | **`GATE EXIT=0`, 568 stdout lines** |
| 15:56:18 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-0da7313b` |
| 15:57:19 | **`RUNNER EXIT=0`** — returned and captured; *"the device came back 17s after this run called `fastboot boot` (seen via: host log, port)"* |

The capture is **598,621 bytes**, sha256
`8783abd0b6a4742f6e4ebc10794de45eb4b69f5270e0e7a6c11e63109a881e19`, archived as
`out/stage90/captures/692-storage-pressed-2026-09-25-last_kmsg.txt`. The phone was back on Android when the
run ended, and the neighbour never appeared.

**The arm itself was built, parked and verified before any of it**: entry `xnu_arm_entry.bin`
`0da7313b…` (5,519,996 bytes), payload `stage90-qcdt.img` `8943ff86…` (8,540,160 bytes), the payload's own
switch record `6c2b6038…` **byte-identical to the 690 arm's** (only the embedded entry blob and the entry
bss-size immediates differ), park `out/stage90/frozen/armed-storage-0da7313b/` at 11/11 files, and
`xnu_arm_entry-config.txt` the one record that names this arm by its last line
`STAGE90_XNU_STORAGE_PROBE=1`.

## 2. The arm's answer

Five keys, and then the machine stops:

```
xnu_live_storage_calls=0x00000001            the probe ran exactly once
xnu_live_storage_live_state=0x00000001       and the live channel was up
xnu_live_storage_section=0x00000f98          0xf9824000 >> 20 - the one install's index
xnu_live_storage_hc_mem_section=0x00000f98   0xf9824900 >> 20 - the same index (532 section 3.1)
xnu_live_storage_windows_share_section=0x00000001
```

Everything after that line in the probe is absent: `_bcr`, `_cbcr`, `_gate`, `_map`, `_slot_before`,
`_desc`, `_l1`, `_l1_moved`, `_ttbr0`, `_ttbr1`, `_loads`, `_writes`, and the six register words. **The
first two readings are the arm's own labels for what it was about to do** — the addresses before they are
dereferenced, which is 532 section 3.1's arithmetic published as numbers — and they are correct: both
windows are in the `0xF98` section, so 532 section 6's step 2 really is **one** install and not two. The
third line of the probe, the BCR read, is where the log stops.

**The instrument's own join is on the same cell.** The runner's summary prints
`this log's abort: xnu_live_sleh_lr=0x001ffff3 pc=0x8000d0c4` with `storm=0x00000009
episodes_seen=0x00000009`, and the storm record in the payload's dump repeats it:

```
xnu_live_sleh_storm=0x00000009
xnu_live_sleh_seen=0x00000009
xnu_live_sleh_pc=0x8000d0c4
xnu_live_sleh_lr=0x001ffff3
xnu_live_sleh_sp=0x8054fea8
xnu_live_sleh_cpsr=0x80000093
xnu_live_sleh_fsr_frame=0x00000005
xnu_live_sleh_far_frame=0xfc4004c0
xnu_live_sleh_frame_ok=0x00000001
xnu_live_sleh_user=0x00000000
xnu_live_slot_ab_m4=0x8000d0b4     (pc - 0x10)
xnu_live_slot_ab_calls=0x00000009
```

**Nine episodes is the stage's standing count, and it is this step's control.** The 690 arm's capture — still
on this host as `/tmp/cancro-last_kmsg.txt.prev.8`, sha `4cfa68c0…`, the file the gate fingerprinted before
the boot — ends at `storm=9`, `seen=9`, `slot_ab_calls=9` too, and its eight traced episodes carry **the same
`lr` values** as this run's (0x8029231c, 0x80293cd0, 0x800454a0, 0x8029368c, 0, 0, 0, 0x80296114) and the same
`far_frame`s (0x1000, 0xc8…, 0x101f28, 0x102000, 0x11a4). Those eight are the payload's own deliberate
abort tests and are **arm-independent**. Their `pc`s differ between the two logs by the entry image's own
growth — `0x8000d82c → 0x8000db00` and `0x8000d4f8 → 0x8000d7cc` (+0x2d4), `0x80017820 → 0x80017ae0` and
`0x80017840 → 0x80017b00` (+0x2c0) — which is an independent statement that the 692 entry image differs from
the 690 one **only** in the region the probe and its call were added to.

So the fatal episode is the ninth in both arms, and in each arm the ninth is the arm's own new one:

| | 690 (the clock arm) | 692 (the storage arm) |
| --- | --- | --- |
| fatal `pc` | `0x8047b488` | `0x8000d0c4` |
| symbol there | `entry_seam_end_run+0xc` | `entry_storage_probe+0xcc` |
| instruction | `str r3, [r2, #0x65c]` with `r2 = 0x0fa00000` | `ldr r1, [r3, #1216]` with `r3 = 0xfc400000` |
| `far_frame` | `0x0fa0065c` | `0xfc4004c0` |
| `fsr_frame` | (see §3) | `0x5` — section translation fault, read |

**The frontier itself is unmoved and the log says so in the runner's own words.** The runner's seam block
prints `FAIL seam_sp=0x8054fec8 is not sleh_sp-8 (the abort's sp is 0x8054fea8, so the slot is
0x8054fea0): the arm's address and the pop's are different words, and nothing below this line joins up` —
which is the correct reading and not a defect: **this arm's death is not the pop's death**, so the seam
pair's cells are not this press's. The keys that are present agree: `xnu_live_seam_calls=0x00000001` (the
seam hook was entered once, on the way in to the wrapper's first return),
`xnu_live_slot_post_calls=0x00000001` (the exit returned **once**, which 687 also measured), `xnu_live_pcx_seq=1`
and `xnu_live_storage_calls=1` (all three say the probe ran on the first and only pass through the wrapper).
The goal's own floor is intact and unmoved: 2 `open`s with errors `0x0` and `0x2`, a `read` that moved the
fixture's `0xfeedface` into a user page, `getpid` = 1, `exit`/`wait`, `ast` 2 — and, as in every capture
since 520, the arm's own safety reading is a wall of zeros: **25 × `persistent_write_attempted=0x00000000`**,
none nonzero, and **7 × `no_platform_driver_exec`** (the payload's standing answer that the OS asked for no
platform driver, measured on every arm of this stage).

## 3. The sentence that was wrong, and where it was already contradicted by a measurement

The arm's record and the readiness narration both justified the gate read with this claim:

> That megabyte is already mapped in the context the wrapper runs in, proved by `entry_epilogue`'s own
> PS_HOLD store at `0xfc4ab000` on every returning run.

**It is wrong, and 692's `far` is the disproof**: `far=0xfc4004c0` with `fsr=0x5` is a section translation
fault on a *read* of that megabyte, so the live table does not map `0xFC4` in the context the probe runs in.
The claim's shape is the defect: it took a **store in a code path** as proof that an **address is mapped**.
The class is recorded in this project's own memory as
`mi4-a-device-address-can-be-right-and-undereferenceable`, whose rule is `addr >> 20` before dereferencing,
and whose citation is 532 — the very experiment the arm's plan came from.

**And the previous arm's parked capture already said so, in a register this project had read and not
joined.** 690's fatal abort is `pc=0x8047b488`, `far_frame=0x0fa0065c`. `entry_seam_end_run` is at
`0x8047b47c` in **both** images (the probe's insertion is below it in `.text`), and its body is:

```
8047b47c:	movw	r3, #21761	; 0x5501
8047b480:	mov	r2, #262144000	; 0xfa00000
8047b484:	movt	r3, #30822	; 0x7866      -> r3 = 0x78665501
8047b488:	str	r3, [r2, #1628]	; 0x65c        <-- 0x0fa0065c = RESTART_REASON
8047b48c:	dsb	sy
8047b490:	movw	r3, #49151	; 0xbfff
8047b494:	mov	r2, #0
8047b498:	movt	r3, #64586	; 0xfc4a      -> r3 = 0xfc4abfff
8047b49c:	str	r2, [r3, #-4095]	; 0xfffff001   -> 0xfc4ab000 = PS_HOLD
8047b4a0:	dsb	sy
8047b4a4:	wfe
8047b4a8:	b	8047b4a4
```

So **690's forced ending faulted on its own first store**, to `RESTART_REASON`; it never reached the `dsb`,
the PS_HOLD store, or the `wfe`. That is the same failure class one arm earlier, in an address nobody was
looking at, and it was printed in the 691 document as an adjacent number (`sleh_pc` / `sleh_lr` /
`far_frame`) without the store being joined to the `far`.

**Two consequences, both measured and neither comfortable:**

* **No arm has ever been observed to complete the PS_HOLD store.** `entry_epilogue` (`0x80004948`) carries
  the same tail at `0x8000577c-0x80005794`, storing to the same two addresses, and it is reached on the
  abort path — so the "PS_HOLD is proven by every returning run" line, which has been in this project's
  notes since 664, is an inference from *code* and not a measurement. The store's address appears in a fault
  record on the one run where the path was reached. **This also falsifies the explanation 691's own README
  row gives for its return time** — "the ending's own PS_HOLD (~9.2 s + 689's measured ~19.6 s of Android
  boot = 28.8 s)" — because the ending's first store is the abort, so the ending cannot have reset the
  machine through PS_HOLD. The decomposition's *shape* (a reset close to the ending, then ~19 s of Android)
  may still be right; the mechanism it names is not.
* **What returns a run is therefore not established.** Three nets exist and none of them matches this
  press's own timing: the payload's `platform_reboot` was never entered (no `platform_reboot entered` line,
  though the payload's own log lines are in the capture), the software dead-man is armed for 60 s
  (`deadman_samples=0x258` × `deadman_interval_us=0x186a0`) and left no dump, and the hardware watchdog was
  armed at payload start for `TIMEOUT_S 25u + BITE_GAP_S 3u` = 28 s. **This run's device came back 17 s
  after the send** — earlier than the countdown's own deadline — so if that number is right, the reset was
  not the countdown finishing. The 28 s in the gate's own paragraph is about 526's port and was never a
  per-run measurement. **Item for the next step: re-read the return interval against the criterion the
  runner prints** (`seen via:`), now that 669 made the interval a first-class reading — 690's 28 s and this
  run's 17 s are not comparable unless the criterion was the same, and 690's log has been overwritten.

## 4. The gate's storage tripwire said `none` about this image

The gate prints, before every press:

```
== storage tripwire ==
no storage symbols in the payload or in the entry image it jumps into
storage-controller references in /mnt/data/mi4-ios6/out/stage90/stage90.elf
  movt high halves checked: 0xf980-0xf98f
  literal words checked:    0xf9800000-0xf98fffff
  none
```

**Both halves are quiet about an image that materialises `0xf9824000` twice, and this is measured rather
than argued.** `grep`-by-word on the very file the address half names —
`out/stage90/stage90.elf` — finds `movt r4, #0xf982` at file offset 580376 (`VA 0x8db18`, inside `.text`,
symbol `stage90_xnu_entry_blob+0xcfe4`) and `movt r3, #0xfc40` at 580440. The 690 arm's `stage90.elf`
carries **neither**, which is what makes them this arm's own bytes.

Why each half misses it:

* **The symbol half** (`preflight_boot_check.sh:1280`) reads `sdcc|emmc|nand|mmc|ufs|flash|partition` with
  a delimiter anchor, over both `stage90.elf` and `xnu_arm_entry.elf`. This arm's new symbols are
  `entry_storage_probe`, `st_read32` and `g_storage_probed` — a probe named after the **medium** rather than
  after one of the controller's family names, so it matches nothing. The check's own comment already names a
  neighbouring hole (a camelCase name); this is the same hole entered from the other side, and the printed
  sentence promises more than the word list delivers.
* **The address half** (`tools/check_storage_refs.py`) disassembles `stage90.elf` with `objdump -d` and
  scans `.text`/`.rodata` for literal words. The entry image is embedded in the payload's `.text` **as
  data**, and `objdump -d` prints that region as bytes — measured: the row at `8db10` reads
  `48 00 48 e3 00 10 93 e5 | 82 49 4f e3 d9 d6 ff eb`, eight raw bytes, not instructions. Every address in
  the entry image is materialised as a `movw`/`movt` pair (the probe's own `r4` is built that way at
  `8000d07c`), so the movt half is *structurally* blind to it and the word half finds no full 32-bit
  constant to match. **Pointed at `xnu_arm_entry.elf` the tool does fire** —
  `movt 0xf9820000  8000d07c: e34f4982 movt r4, #63874 ; 0xf982`, exit **1** — and at the 690 arm's entry
  image it prints `none`, exit 0. So the fix is one more file in a loop the gate already has for the symbol
  half, and the peer lane owns both files; it is reported rather than edited here.

This is the reason 692's record keeps the arm's own `xnu_live_storage_writes` discipline **and** the reason
the arm is the right shape anyway: the probe writes nothing, so what the tripwire's absence cost this press
is a *warning*, not a write. But the same hole in front of an arm that *does* store to a controller register
would spend a press on the strength of a `none`.

## 5. What the arm did and did not establish

* **Established**: the two windows really do share one 1 MB section, on the device and not only in the
  device tree (`_section` == `_hc_mem_section` == `0xf98`), so 532 section 6's step 2 is one install.
* **Established**: **`0xFC4` is not mapped in the context the exit wrapper runs in.** This is the first
  measurement of that megabyte on this frontier, and it is a *negative* one. It also re-reads the reset
  path: `entry_epilogue`'s stores to `0x0fa0065c` and `0xfc4ab000` are in the same position, and one of
  them has now been observed to fault.
* **Not established**: nothing about the controller itself. `_bcr`, `_gate`, the version word, both
  pre-registered bits (`_mode_bit`, `_sw_rst`) and every one of the six loads are **UNREAD** — the arm
  never reached the install, so 532 section 6's steps 2 and 3 remain exactly where they were. 531
  section 6's mode-bit question is still open.
* **Not established**: whether the six loads would have returned. The gate read was placed first *because*
  a load through a stopped branch is a bus wait with no log — and the press shows the gate read was the
  riskier of the two, which is the opposite of the assumption it was designed on.
* **The negative cell in the record was written about the wrong address.** It said a mapping failure with a
  live gate "comes back with `xnu_live_sleh_far = 0xf9824000`". The run came back with a far, and the far is
  `0xfc4004c0`: the probe's first dereference is the **gate**, not the controller block, and the gate's own
  megabyte is the one that had never been measured. The cell was not wrong about the shape (a translation
  fault, logged, coming back) — it named the wrong site.

## 6. What the next arm looks like, from this measurement

The probe needs **one more section installed for `0xfc400000`** before it reads the gate —
`entry_mmio_section(0xfc400000, 0xfc400000, &slot_before, &desc)` — and the arithmetic is friendly: `0xFC4`
is a different L1 index from the `0xF98` install, so 532 section 3.2's occupied-slot refusal cannot fire and
the two calls cannot collide. The alternative is to keep the install-only variant and drop the gate read to
`0xf9824000`'s section, taking the bus-wait hazard the gate existed to remove — **not** recommended: the
gate is the safer read *if* it is mapped, and the measurement says the mapping is one call away.

And the same map now has to be considered for the **reset path**: `0x0fa0065c` (RESTART_REASON) and
`0xfc4ab000` (PS_HOLD) are in two different unmapped megabytes, and the one run that reached the prologue of
that path faulted on the first store. An arm that intends to end a run by resetting the machine has to map
what it stores to, or it ends the run by faulting and leaves the return to whatever net actually fires — the
open item in §3.

## 7. What this does not do

**It does not enter the OS in the sense the goal needs.** The boot is still the fixture's own path, and this
run's frontier is *earlier* than 690's: pid 1 runs, one character device answers, `open`/`read`/`getpid`/
`exit`/`wait`/`ast` — the same floor 520 and 533 met — and then the probe's first device read faults. No
storage device was brought up, no filesystem, no driver beyond the fixture's, and the arm was read-only by
construction: it publishes no `_writes` line because it died before reaching it, and its only `st_read32`
executed is the one that faulted.

**The storage condition is still unmet and TWRP-to-storage stays withheld**: 「如果os已经能进去了的话」 asks
for a boot *observed* entering the OS and staying there, and this run's OS never got past its second idle
exit. The probe's read-only device work is not the withheld write — but it is also not evidence for it.

## 8. Owed

* The two repairs §4 names belong to the peer lane (`preflight_boot_check.sh`, `tools/check_storage_refs.py`)
  and go by message: the address half should read the same two images the symbol half does.
* The `0xFC4` / `0x0fa0` mapping question, and the return-interval question, are the next arm's work.
* 690's parked capture should be re-read with §3's join in mind; the 691 document's presentation of
  `sleh_pc=0x8047b488` / `far_frame=0x0fa0065c` as adjacent numbers is what a reader would carry forward,
  and this document is where the join lives.
* Still owed from earlier steps and unchanged: the 691 section 5 one-store `entry_note_wfi` readback,
  `entry_reset.h`'s false IMEM claim, the `RESTART_REASON` decision, the 676/677 section 6 items, and the
  684-owed runner clause for the 678 arm.
