# 694: the storage arm pressed — the gate is OPEN, the controller answers, and it is not yet in SDHCI mode

693's arm was pressed. **The device returned and the log came back** (exit 0, **24 s**), and for the first
time in this sequence the probe ran to its end: **all twenty-eight of its keys are in the log**, there is
no abort at any of its loads, and the three cells it was armed to separate came out as the run it existed
for —

* **the GATE's megabyte was unmapped and this arm mapped it** (`_gcc_map=1`, `_gcc_slot_before=0`,
  `_gcc_desc=0xfc41040e`);
* **the gate reads, and it is OPEN** (`_gate_read=1`, `_bcr=0`, `_cbcr=0x4ff1`, `_gate=1`);
* **the controller answers, and nothing was written to it** (`_map=1`, `_loads=6`, `_writes=0`).

And its two pre-registered bits are the first reading this project has of the state the bootloader handed
over: **`HC_MODE_EN` is CLEAR** — the block the payload inherits is **not** in SDHCI mode, so the vendor's
mode sequence is a **prerequisite** and not a re-do (531 §6's open question, answered in the direction
that costs a write).

**One press spent**: one readiness chain (**5/5**), one gate (**exit 0**, 569 stdout lines), exactly one
runner, one non-persistent `fastboot boot`. Nothing flashed, nothing written to any storage controller,
and no device state changed but the boot. The flags and the arm name are the ones readiness printed
(`--allow-xnu-entry`, `--expect-arm=armed-storage-gcc-1fc30bfe`), and the neighbour `33e80afe` was off the
bus throughout.

## 1. The press

| time (UTC) | event |
| --- | --- |
| 17:23:07 | **readiness exit=0** — 5 of 5, including `the press would be caught` (adb lists `4a2fe00b` as `device`; fastboot empty, so the neighbour is absent) |
| 17:23:45 | `preflight_boot_check.sh --allow-xnu-entry` |
| 17:24:11 | **`GATE EXIT=0`, 569 stdout lines, 0 stderr** |
| 17:24:13 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-gcc-1fc30bfe` |
| 17:25:22 | **`RUNNER EXIT=0`** — returned and captured; *"the device came back 24s after this run called `fastboot boot` (seen via: host log, serial)"* |

The capture is **611,558 bytes**, sha256
`d5c42bf9ea86d3b8d72084676f372adcb8607384e4ab8c841c5a69d6dfb9ab91`, archived with the run and gate logs as
`out/stage90/captures/694-storage-gcc-pressed-2026-09-25-{last_kmsg.txt,run.log,gate.log}`. The run's own
gate line reports the bytes it sent were the bytes the gate read
(`6974403896cb3671d39bfedf8e05d9108e11732174e6b68897c2dd86b156d772`, unchanged across the send), the live
channel is **not full** (4,837 records of 8,192, and no `xnu_live_capped`), so an absent key in this log is
an event that did not happen rather than a record that was dropped. The phone was back on Android when the
run ended.

**Where the arm's own reading lands in the log.** The ordered sequence of the run's live records is one
exit-wrapper return (**seam pair published, `xnu_live_slot_post_calls=1`**), then the probe's twenty-eight
keys, then the clock's baseline (`xnu_live_post_t0=0x075e4f57`), then six further passes, then the ending.
That order is the source's own: `__wrap_platform_cache_idle_exit` calls `entry_storage_probe()` on its
first return, *before* the clock block.

## 2. The arm's answer, key by key

**Identity and the arithmetic that says the two installs cannot collide** (published before any device is
touched):

```
xnu_live_storage_calls                 0x00000001     the guard: this probe ran once
xnu_live_storage_live_state            0x00000001     the live channel existed, so the mapper was available
xnu_live_storage_section               0x00000f98     the controller's L1 index (3992)
xnu_live_storage_hc_mem_section        0x00000f98     its second window is in the same megabyte
xnu_live_storage_windows_share_section 0x00000001
xnu_live_storage_gcc_section           0x00000fc4     the GATE's L1 index (4036)
xnu_live_storage_gcc_share_section     0x00000000     different indices: 532 section 3.2 cannot fire
```

**The two installs, and the megabyte 692 died in**:

```
xnu_live_storage_gcc_map          0x00000001     the GATE's megabyte is now mapped
xnu_live_storage_gcc_slot_before  0x00000000     the slot was EMPTY - nothing had ever mapped it
xnu_live_storage_gcc_desc         0xfc41040e     section, PA base 0xfc400000 (see section 4)
xnu_live_storage_map              0x00000001
xnu_live_storage_slot_before      0x00000000     also empty
xnu_live_storage_desc             0xf981040e     section, PA base 0xf9800000
xnu_live_storage_l1               0x80704000     the table the mapper read, this time
xnu_live_storage_l1_moved         0x00000001     and it is NOT the table the console latched (0x80700000)
xnu_live_storage_ttbr0            0x8070404a     XNU has switched tables since the console's latch
xnu_live_storage_ttbr1            0x8070404a
```

`_slot_before = 0` on **both** calls is the reading that makes 692's premise explicit: the megabyte that
the record called "already mapped" had an **empty** L1 slot, and so did the controller's own megabyte —
neither had a descriptor before this arm wrote one. And `_l1_moved=1` beside `xnu_live_ttbr0=0x8070004a`
(the channel's own reading at its init) versus `_ttbr0=0x8070404a` (the mapper's, taken here) is 484's
finding reproduced on a returning run.

**The gate, and the `+4` pair**:

```
xnu_live_storage_gate_read  0x00000001
xnu_live_storage_bcr        0x00000000     SDCC1_BCR  at 0xfc4004c0
xnu_live_storage_cbcr       0x00004ff1     SDCC1_CBCR at 0xfc4004c4
xnu_live_storage_gate       0x00000001     BIT(0) of the CBCR: the branch is ENABLED
```

**This is the reading 692 could not take**, and it is the one that grounds the whole probe: the GCC block
answers, the CBCR's bit 0 is set, and the branch's two registers are at `0x4C0` and `0x4C4` — the `+4`
offset that 528 §8 read off the UART pair and 531 §8 asserted from `clock-8974.c` is now confirmed on
hardware. A gate of 1 is also what makes the six loads safe to make, which is the interlock's other half:

```
xnu_live_storage_gated_out  0x00000000
xnu_live_storage_loads      0x00000006
xnu_live_storage_writes     0x00000000     the arm's own safety reading
```

**The six words, and the two bits this arm exists for**:

```
xnu_live_storage_core_power     0x00000441     CORE_POWER 0x00  - bit 7 (CORE_SW_RST) CLEAR
xnu_live_storage_mci_data_ctrl  0x00008000     CORE_MCI_DATA_CTRL 0x2C
xnu_live_storage_mci_version    0x10000011     CORE_MCI_VERSION 0x50 - the block ANSWERS
xnu_live_storage_hc_mode        0x00002000     CORE_HC_MODE 0x78  - bit 13 set, bit 0 CLEAR
xnu_live_storage_hci_version    0x00000010     hc_mem + 0x00 (SDHCI standard)
xnu_live_storage_capabilities   0x742dc8b2     hc_mem + 0x40 (SDHCI standard)
xnu_live_storage_sw_rst         0x00000000     CORE_POWER & CORE_SW_RST: nothing is holding the core
xnu_live_storage_mode_bit       0x00000000     CORE_HC_MODE & HC_MODE_EN
```

## 3. What the two bits mean, and the direction 531 §6 was asking about

**`_mode_bit = 0` is the answer 531 §6 wanted**: the mode sequence the vendor's driver opens with is a
**prerequisite**, not a re-do. On this handed-over state the arm's own summary of what is ahead is
concrete — `CORE_HC_MODE` reads `0x2000` (bit 13 set, **bit 0 clear**), and the vendor's probe
(`sdhci-msm.c:2841-2868`) is: write 0 to `CORE_HC_MODE`, set `CORE_SW_RST`, poll it clear, then set
`HC_MODE_EN`. So the first **two** of those writes are still ahead of this boot, and `_sw_rst = 0` says the
core is *not* currently held in reset, so the version word below is a live answer rather than a
reset-suppressed one.

**`_mci_version = 0x10000011` is not 0 and not `0xffffffff`** — the "the block did not answer" cell is
falsified, which is what makes the other five words readings at all.

**And the two standard windows must be read with `_mode_bit = 0` beside them**, which is exactly the
caveat 531 §6 raised: the mode bit has to say SDHCI before a single standard register means anything.
`hci_version = 0x10` and `capabilities = 0x742dc8b2` are plausible-looking words, and the honest reading
is that **they were taken through a block that is not (yet) in SDHCI mode** — so they are the pre-mode
state of that window and not a spec-valid HCI_VERSION/CAPABILITIES pair. The arm publishes them because
531 §6's step 3 is to read three words before writing anything; it does not claim they are spec-valid, and
this step does not either. What *is* claimed is narrower and measured: the window at `hc_mem` answers at
all, and it answers with non-zero, non-saturated words.

## 4. The attribute, measured three ways — and a decoder that cannot see TRE

`_gcc_desc = 0xfc41040e` and `_desc = 0xf981040e` share the low twenty bits `0x1040e`, so both sections
were installed with one recipe (the mapper takes the attribute as an argument and writes no key of its
own). The bits are `TEX[2:0] = 000`, `C = 1`, `B = 1`, `S = 1`, `AP = 0b01` (PL1 RW, PL0 no access),
executable. **On this machine `SCTLR.TRE` is set** (`xnu_live_sctlr = 0x30c5787d`, bit 28), so those C/B
bits are not a memory type at all: they select the encoding index in `PRRR`, and `PRRR[3]` is what says
what the index means. Read out of the log rather than argued:

* `xnu_live_prrr = 0x1f08022a` — its index fields, two bits each, are `2, 2, 2, 0, …`, so **the first
  index whose field is 0 is 3**, which is what the mapper's own recipe searches for;
* the recipe encodes that index as `B = i[0] = 1`, `C = i[1] = 1`, `TEX[0]` — the bit
  `ARM_TTE_BLOCK_ATTRINDX` writes `i[2]` into, `ARM_TTE_BLOCK_TEX0SHIFT = 12` (`proc_reg.h:789`) — `= 0`,
  exactly the `0x040e` observed;
* and the attribute word the mapper used is published by the channel itself: **`xnu_live_attr = 0xc`**
  (`= (1<<3) | (1<<2)`), the value both `entry_stubs.c`'s comment and this press's log carry.

`PRRR` field 0 is the Strongly-ordered encoding, so **both sections are Strongly-ordered and shareable**
— the project's own device attribute, the one `docs/reference/pmap-attribute-map.md` maps MMIO with, and
not a Normal/cacheable mapping. That matters for the claim §3 of the 693 record makes about the reset
path: `0xfc4ab000` is in the megabyte this arm installed, and it is installed with **device semantics**,
so a store through it would reach the pin rather than sit in a cache line.

**`tools/decode_armv7_descriptor.py` printed `Reserved` for this combination**, and that is a defect in the
tool and not in the mapping: with `SCTLR.TRE` clear, `TEX=000, C=1, B=1` is the reserved row, but with TRE
set those two bits are the `PRRR` index and the tool had no input for TRE — it decoded the *descriptor*
correctly and the *attribute* by a rule that this device does not use. The tool's own header names the
problem in the shape of an assumption it does not state: it decodes against Apple's `ARM_PTE_*`, where the
low twenty bits are one opaque `ATTR_MASK`. **This step repaired it** (`--tre`, `--prrr=`, `--nmrr=`), and
the repair turned up two things the repair's own first draft got wrong, both recorded here because a
tool's help text is a claim like any other:

* **The rule that names a descriptor is a property of the regime it runs in, not of the descriptor.**
  `0x0001140e` is the *payload's* `STAGE90_PMAP_DESC_SECTION_NORMAL_WB` (`stage90.h:4423`), and the
  payload's Phase-1 tables run with TRE **clear** — so for it the `TEX/C/B` table *is* the rule and the
  reference's "Normal, Write-back, write-allocate" row is right; the sentence that used to stand here (that
  its index's `PRRR` field was 3, the "use TEX/C/B" row) was **wrong arithmetic**: `0x0001140e` is
  `TEX[0] = 1, C = 1, B = 1`, so its index is **7**, whose field on this machine is `0b00`. The
  counterfactual is the interesting half: read under the regime the *entry image's* installs run in, the
  payload's three section descriptors would be "Strongly-ordered" (index 7, field 0) for `NORMAL_WB`,
  "Write-Back" (index 4, field `0b10`, `NMRR` IR4 = 1) for `NORMAL_NC`, and "Write-Back" (index 0) for
  `SO_ONLY` — i.e. **inverted**, which is why the two regimes must never be decoded with one rule.
* **`PRRR` field `0b00` is now evidenced rather than asserted, and `0b10` is not a type at all.** The
  machine installs `PRRR_SETUP = 0x1F08022A` (`TR0..TR7 = 2,2,2,0,2,0,0,0`) beside `NMRR_SETUP =
  0x01210121` (`IR0..IR7 = 1,0,2,0,1,0,0,0`, `proc_reg.h:530`/`:550`), and those `NMRR` fields are
  **exactly** Apple's own names for the first six `CACHE_ATTRINDX_*` (`:630-636`): 1 = `WRITEBACK` at
  index 0, 0 = `DISABLED` at index 1 (`WRITECOMB`, "no cache, buffered writes"), 2 = `WRITETHRU` at 2,
  0 = `DISABLED` at 3 (`DISABLE`), 1 = `WRITEBACK` at 4 (`INNERWRITEBACK`), 0 at 5 (`POSTED`). So the
  field that sits on the two indices Apple calls `DISABLE` (`0b00`, indices 3 and 5) is the uncached
  one — which is the field `entry_stubs.c` searches `PRRR` for — and `0b10`, which sits on a write-back
  index *and* on a write-combining one, cannot name a type by itself: it hands the attribute to `NMRR`.
  **That settles 548 §5's first residual by evidence rather than by choosing a reading**: XNU's kernel
  mapping is `ATTRINDX(0)` (descriptor `TEX=000, C=0, B=0`), its `PRRR` field is `0b10`, and `NMRR` `IR0
  = NMRR_WRITEBACK` — **write-back, write-allocate**, not the "fixed normal type" alternative.
* **And the misnaming that started this is in the entry image's own source, not only in this document.**
  `entry_stubs.c:2322` reads `/* ARM_TTE_BLOCK_ATTRINDX(i): B = i[0], C = i[1], TEX[2] = i[2]. */` beside
  the code that computes `(((i >> 2) & 1u) << 12)` — and bit 12 is `ARM_TTE_BLOCK_TEX0SHIFT`, i.e. the
  descriptor's **TEX[0]**, exactly where Apple's macro (`proc_reg.h:803`) puts it. The arithmetic is right
  and the name is wrong, and a wrong name is what a reader copies: this document's first draft said
  `TEX[2]` for the same reason, and so did the tool's. **Owed, and it must ride along with the next edit
  inside `xnu_arm_boot/`** — that directory is content-hashed into `xnu_arm_entry-sources.txt` (`673`), so
  a comment-only touch now would change the parked arm's own identity and the gate would refuse it.

## 5. The ending fired — and it did NOT end the run

The arm's ending is still 690's clock, and it fired:

```
xnu_live_post_t0          0x075e4f57     the baseline (123,621,207)
xnu_live_post_cntfrq      0x0124f800     19,200,000 Hz - the machine's OWN statement of the tick rate
xnu_live_post_elapsed     0x001daeeb     pass 2:   1,945,323 ticks
                          0x010dbb07     pass 4:  17,677,063
                          0x06e109da     pass 7: 115,411,418
xnu_live_post_end_calls   0x00000007     PRESENT => the ending FIRED, on the seventh return
```

115,411,418 against the armed 115,200,000 is **211,418 ticks (11.011 ms) past the deadline**, on pass 7 —
6,011.0 ms of live kernel, which is what a deadline checked at a pass boundary looks like, and the
pre-registered cell (`_post_end_calls` present ⇒ the boot stayed up for the whole deadline and was then
ended on purpose) is met. `_post_cntfrq` is also the first time this project has put the tick rate on the
record from the hardware rather than from the device tree: **19,200,000 Hz**, which confirms 690's
device-tree claim exactly.

**And then the ending's own first store faulted**, at the ninth abort episode and at exactly the address
the 693 record pre-registered:

```
xnu_live_sleh_storm      0x00000009
xnu_live_sleh_seen       0x00000009
xnu_live_sleh_pc         0x8047b488     entry_seam_end_run+0xc: str r3,[r2,#0x65c]
xnu_live_sleh_lr         0x8047b5bc     the instruction after the bl inside entry_post_clock
xnu_live_sleh_far_frame  0x0fa0065c     RESTART_REASON - still in an unmapped megabyte
xnu_live_sleh_frame_ok   0x00000001     the handler took the frame
```

**`lr = 0x8047b5bc` is the value 690's record pre-registered for this arm** (688's counted ending read
`0x8047ca0c`), so the ending is identified by the reading that predicted it and not by its address alone.
`far = 0x0fa0065c` is `RESTART_REASON` — **the `0x0FA` megabyte, which this arm did not map and 684 did
not repair** — and the fault is recovered rather than fatal (`frame_ok=1`, no `panic … sleh_abort` in the
log).

**The part of this that is new, and it is a correction to how the ending has been described**: the boot
**kept running after its own ending**. The evidence is the channel's own counters, and it is not a reading
off "records appear later in the file" — **the log carries exactly 4,837 `xnu_live_` lines and the channel
reports exactly 4,837 records of 8,192**, so every record the boot made is here, once, in write order. Three
of them are *after* the ninth abort and are monotone continuations rather than repeats:

| key | last value before episode 9 | value after it |
| --- | --- | --- |
| `xnu_live_cls_calls` | `0x3d` (62nd class walk, line 7581) | **`0x3e`** (line 8792) |
| `xnu_live_walk_seq` | `0x07` (line 6437) | **`0x08`** (line 8801) |
| `xnu_live_path_ret` | `0xc05f0e18` (line 6451) | **`0xc05f17b8`** (line 8815) |

So the ending's fault was not merely survived: execution left the wrapper, and the boot went on to do at
least one more device-tree class walk and one more path walk. **A forced ending whose first store faults
does not end the run**, and every "this arm ends the run on purpose" sentence about 678/686/690 needs that
qualifier: what those arms measured is that the ending was *reached*, never that it took effect. The 24 s
return is therefore **still unattributed** — the third in a row (8 s on 690, 17 s on 692, 24 s here) — and
the candidates in this log are the armed hardware watchdog (`hw_watchdog_enabled=1`, counter running,
countdown plausible, readback ok) and the payload's dead-man, neither of which leaves a line.

## 6. The abort control table is confirmed, again, by an arm that moved

The log carries nine episodes and the eight that are not this arm's are **the same eight as 692's**:

| episode | `lr` | 692 `pc` | 694 `pc` |
| --- | --- | --- | --- |
| 1 | `0x8029231c` | `0x80017ae0` | `0x80017bc0` |
| 2 | `0x80293cd0` | `0x8000db00` | `0x8000dbdc` |
| 3 | `0x800454a0` | `0x8000d7cc` | `0x8000d8a8` |
| 4 | `0x8029368c` | `0x80017ae0` | `0x80017bc0` |
| 5 | `0x00000000` | `0x00001118` | `0x00001118` |
| 6 | `0x00000000` | `0x00001124` | `0x00001124` |
| 7 | `0x00000000` | `0x000011a4` | `0x000011a4` |
| 8 | `0x80296114` | `0x80017b00` | `0x80017be0` |
| **9** | **`0x001ffff3` / `0x8047b5bc`** | **`0x8000d0c4` (the probe's gate read)** | **`0x8047b488` (the ending's store)** |

Every `lr` is identical between the two captures, the three user-address episodes (`0x1118`, `0x1124`,
`0x11a4`) have not moved at all, and the `pc`s inside the entry image have moved by `+0xE0` (the
`0x8001_7…` pair) and `+0xDC` (the `0x8000_d…` pair) — **which is the entry image growing**, exactly the
shift a rebuild should produce, and the reason a fault site is identified by `pc`-minus-base and not by
`pc`. Episode 9 is each arm's own: 692's died in the probe, this one in the ending.

## 7. What this establishes, and what it does not

* **Established, on hardware, for the first time**: the eMMC controller's two windows are reachable at
  `0xf9824000`/`0xf9824900`; SDCC1's branch enable is on; the controller answers with a version word; it is
  **not** in SDHCI mode; its core is not held in reset; and every one of those readings was taken with
  **zero writes to the block**. 531's §6 question is answered, and 532's §6 steps 2 and 3 are done.
* **The goal's own floor is still met and no further**: the run's criterion block reads user mode reached
  and a driver answering — `open` answered twice (error `0x0` then the control `0x2`), a `read` that
  returned the fixture's `0xfeedface` into a user page, `getpid` 1, `exit` pid 2, `wait` reaping,
  `ast` records. That is 520's and 533's reading too; it says the OS still boots to pid 1's syscalls, not
  that this arm advanced the boot.
* **Not established**: that XNU drives this controller. Nothing here registers a block device, mounts a
  filesystem, or writes a partition table; the probe is read-only by design and `_writes=0` is its own
  reading. The **frontier is unmoved** — XNU reaches pid 1 and dies at the idle exit's `pop {fp, pc}`, and
  this arm's ending is 690's clock at the same site.
* **The `0x0FA` megabyte is still unmapped**, so the reset path's `RESTART_REASON` store still faults on
  its first store, and the arm this step pressed is the one that made its *other* store's megabyte
  reachable. The reset path is not repaired by this press and cannot be by a mapping alone — 663 §3.2's
  point stands: there is no software reset here, and the ending has now been measured not to end a run.
* **`xnu_live_storage_writes = 0` is the arm's safety reading, not a comment**, and the ELF was read to
  confirm it: no store anywhere in `entry_storage_probe` is based on either base register. Nothing was
  written to the medium in either direction.
* **TWRP-to-storage stays withheld.** The user's condition for it is that the OS can already be entered;
  the OS is not observed booting, so the condition is unmet and no write has been made.

## 8. Owed, and what the next step is

* **What actually returns a run** is now three measurements (8 s / 17 s / 24 s) and no answer. Every
  returning arm has ended in `entry_seam_end_run`'s fault or in the pop's, and every reset has come from
  something that leaves no line. The two candidates this log narrows to are the payload's armed hardware
  watchdog and its dead-man. A **rehearsal arm that ends a run on purpose through `platform_reboot`'s
  shape** (store, `dsb sy`, then `for (;;) wfe`, 663 §2 step 4) is the arm that would attribute it, and
  673 already bounds the work: it needs ~ten lines behind a new switch **inside the closure**
  (`xnu_arm_entry-sources.txt` is every file in `xnu_arm_boot/` by content), so it can only be edited
  between an armed firer and the next one.
* **The mode sequence is the next question, and it is a WRITE.** With `_mode_bit = 0`, the vendor's
  sequence is a prerequisite: `CORE_HC_MODE ← 0`, `CORE_SW_RST` (bit 7 of `CORE_POWER`) set, poll it
  clear, then `HC_MODE_EN`. That arm must be pre-registered **as a write before it is built**, with the
  polling bound and the failure path named, and it must not touch `POWER_CONTROL 0x29` (531 §8: writing 0
  to it *is* a bus-off request). Until then the read-only path has this much left: `_hci_version`,
  `_capabilities` and `_mci_data_ctrl` re-read **after** the mode sequence, which is the only way to say
  whether they were pre-mode words.
* **The gate's storage tripwire still does not see these addresses** (692 §4, reported to the peer lane by
  message and not edited here): `STORAGE_SYM_RE` in `preflight_boot_check.sh:1280` has no `storage`, and
  `tools/check_storage_refs.py` disassembles the payload where the entry image sits as **data**. This
  press's payload carries `movt #0xf982` at file offset 580376 and `movt #0xfc40` in the entry blob, and
  the fixed `out/stage90` was gated green with it.
* **A tool defect, in this lane — repaired in this step**: `tools/decode_armv7_descriptor.py` decoded
  `TEX/C/B` as a memory type with no `SCTLR.TRE` input, so it printed `Reserved` for the Strongly-ordered
  descriptors this project's own MMIO mapper installs (§4). It now takes the regime (`--tre`, `--prrr=`,
  `--nmrr=`); what it got wrong in the first draft is written up in §4, because a help text that names a
  `PRRR` field is a claim like any other. **The reference doc's `TEX/C/B` table stays as it is** and is now
  labelled with the regime it belongs to (`docs/reference/pmap-attribute-map.md`): every descriptor on
  that page is the payload's, and the payload's tables run with TRE clear.
* **A misnamed bit in the entry image's own comment** (§4): `entry_stubs.c:2322` says `TEX[2] = i[2]`
  where the `<< 12` it sits beside is `TEX[0]`. A comment-only fix, and it waits for the next edit inside
  `xnu_arm_boot/` because that directory is content-hashed into the parked arm's identity.
* **Carried, unchanged**: the 691 §5 one-store `entry_note_wfi` readback; `entry_reset.h`'s false IMEM
  claim; the `RESTART_REASON` decision; 676 §6 / 677 §6; the 684-owed runner clause for the 678 arm;
  `tools/xnu_dt_requirements.py` not encoding the `"master"` value.
