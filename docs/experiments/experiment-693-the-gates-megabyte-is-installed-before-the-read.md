# 693: the gate's megabyte is installed before the read, and the probe's own order stops hiding a refusal

692 pressed the storage probe, and the probe died on **the third line it ever ran** — a load from
`0xfc4004c0`, in a megabyte that no table this image builds maps, on the strength of a sentence that
called it mapped. This step is the repair, and it is two changes rather than one, because the press
exposed two defects:

1. **the gate's megabyte (`0xFC4`) was never mapped**, and its load was the probe's third act;
2. **the probe's own order made a refusal unreadable** — the gate read came first and the controller's
   install came after it, so a run that ended gated out could not publish whether the section had been
   installed at all, which is exactly the distinction 692's record asked for and could not make.

So this arm installs the GATE's megabyte **first**, installs the storage block **before the gate is
read**, and publishes three new keys (`_gcc_map`, `_gcc_slot_before`, `_gcc_desc`) for the install 692
did not have. The measurement it buys is the same one 692 was armed for — does the block answer, and in
what state did the bootloader leave it — taken through a descriptor **this run wrote itself**.

**Nothing was sent to the device in this step.** The arm is built, its bytes are measured, it is parked,
the record is written by hand, the revert set verifies against both the park and the live tree, the gate
is green on the reverted tree and readiness is 5 of 5 — and the press is the next step's act, for the
reason 690 gave: 660's rule forbids editing the closure while a firer is armed, and this step had a tree
to edit.

## 1. The two defects, each one measured rather than argued

**The first is a falsified premise, and its dismissal is the whole of 692.** The record and readiness
both carried *"that megabyte is already mapped in the context the probe runs in, proved by
`entry_epilogue`'s own PS_HOLD store at `0xfc4ab000` on every returning run"*. The press answered it with
`xnu_live_sleh_far_frame=0xfc4004c0` and `fsr_frame=0x00000005` — a **section translation fault**, which
is the architecture's own way of saying *no descriptor covers this megabyte*. The class of the error is
`mi4-a-device-address-can-be-right-and-undereferenceable`: **a store inside a code path was read as
evidence that an address is mapped.** The code path proves the store is *reached*; it cannot prove the
descriptor exists, and on this device it did not.

**The second defect is the ordering, and it is a defect about what a run can say rather than about what
it does.** In 692's arm the probe ran: five identity keys → **read the gate** → install the controller's
section → *if the install refused, return* → *if the gate was closed, return*. So on a run whose gate was
read and found closed, `_map` was **never published**, and the log could not say whether the controller's
section had been installed. Two very different machines — *"the block is clocked off and was deliberately
not touched"* and *"the block could not be reached at all"* — would have printed the same three keys.
That is the defect: an arm whose cells are supposed to be told apart by `_map` published `_map` last of
all.

**And the fix for the second one is not a new key, it is an order.** An install is a **page-table write**
and not an access to the medium's controller, so moving it ahead of the gate read does not weaken the
interlock: the gate still guards **every one of the six register loads**, and the safety property 692
declared (`xnu_live_storage_writes=0`) is untouched. What the reorder buys is that the four install
numbers are published *before* any branch that can return.

## 2. The change

`entry_storage_probe` (`xnu_arm_boot/entry_storage.c`) now runs, in this order:

| # | act | keys published |
| --- | --- | --- |
| 1 | five identity keys | `_calls`, `_live_state`, `_section`, `_hc_mem_section`, `_windows_share_section` |
| 2 | **`entry_mmio_section(0xfc400000, 0xfc400000, …)` — the GATE's megabyte** | `_gcc_map`, `_gcc_slot_before`, `_gcc_desc` |
| 3 | **`entry_mmio_section(0xf9824000, 0xf9824000, …)` — the controller's section** | `_map`, `_slot_before`, `_desc`, `_l1`, `_l1_moved`, `_ttbr0`, `_ttbr1` |
| 4 | two loads off the GATE (read-only) | `_gate_read`, `_bcr`, `_cbcr`, `_gate` |
| 5 | the two refusals (`_gcc_map == 0`; `_map == 0`) then the gate interlock | `_gated_out`, `_loads`, `_writes` |
| 6 | six loads off the controller, all reads | `_core_power`, `_mci_data_ctrl`, `_mci_version`, `_hc_mode`, `_hci_version`, `_capabilities`, `_sw_rst`, `_mode_bit` |

Three things about that table are deliberate and each is a repair of something the press found.

* **`_gcc_section` and `_gcc_share_section` are published with the five identity keys, not with the
  install**, because they are arithmetic on constants and not readings of a device: the gate's megabyte
  is L1 index `0xFC4` (4036) and the controller's is `0xF98` (3992) — different indices in the same
  table, both inside `0..4095`, which is why the two installs cannot collide (see §3).
* **The gate's own install has its own three keys and not a share of `_map`'s.** 532 §6 step 2's rule —
  the arm must be able to tell two installs apart — is why; a log with one set of install numbers and
  two calls could not say which call failed.
* **`_gate_read` is still the key that separates "not read" from "read and closed"**, and on this arm
  `_gate_read = 0` has exactly one cause: the GATE's install refused. That is a narrower statement than
  it was on 692, where `_gate_read = 0` could also mean the storage install refused *after* the gate had
  already been read — the ordering defect above, restated as a key.

## 3. Why the two installs cannot collide, and why the second one is the reset path's too

`entry_section_install` refuses an **occupied** slot rather than clobbering it (532 §3.2), and 532 §6
step 2 is the warning that a *two-call* arm can trip it: a second call for a window inside the same
megabyte would find the slot already holding a block descriptor and return 0 *after* having mapped the
controller correctly, which reads like a mapping failure and is not one. This arm makes two calls and
they are at **two different L1 indices** — `0xFC4` and `0xF98` — so that refusal cannot be how either
call ends, and `xnu_live_storage_gcc_share_section` publishes the comparison as a reading (`0`) rather
than leaving it to a comment.

**And the GATE's megabyte is also `entry_epilogue`'s.** The entry image's `noreturn` reset tail writes
`RESTART_REASON (0x0fa0065c) ← 0x78665501` and then **`PSHOLD (0xfc4ab000) ← 0`** and spins
(`entry_stubs.c:4227-4234`). Those are **two different unmapped megabytes**: `0x0FA` and `0xFC4`. 684
measured the first one faulting — 690's arm died at `entry_seam_end_run+0xc`, `str r3,[r2,#0x65c]`, with
`far_frame=0x0fa0065c`, so the forced ending never reached its own `dsb`, let alone the PS_HOLD store —
and 692's own ninth abort is the same store faulting again. `0xfc4ab000` is inside this arm's `0xFC4`
megabyte, so **`_gcc_map = 1` beside a readable `_bcr` is also the reading that the PS_HOLD half of the
reset path becomes reachable** — the repair 684 named, taken here as a side effect of mapping the gate
the probe needed anyway, and not as a second experiment.

## 4. The arm, measured in the ELF

Read out of `out/stage90/xnu_arm_entry.elf` (`d187d494…`), not inferred from the source:

* **`entry_storage_probe` is at 0x8000cff8 and its one caller is unchanged at 0x8047cb2c**
  (`__wrap_platform_cache_idle_exit`, the same `bl` 692's arm had) — the call site did not move, only the
  body did. It is still the only `bl` to the probe anywhere in the image.
* **The gate's base is `movt r6, #0xfc40` at 0x8000d084**, with `mov r6, r3` at 0x8000d064 and `r3` the
  function's own zero, so `r6 = 0xfc400000` from that instruction on.
* **The two installs are at 0x8000d0ec and 0x8000d144**: the first with `r0 == r1 == r6` (`0xfc400000`),
  the second with `r0 == r1 == r4` where r4 is materialised by `movw r4,#0x4000` + `movt r4,#0xf982`
  (`0xf9824000`). The gate's install **precedes** the controller's in address order, which is the change.
* **The gate's two loads are `ldr r9,[r6,#1216]` at 0x8000d1dc and `ldr r6,[r6,#1220]` at 0x8000d1ec**,
  i.e. `0x4C0`/`0x4C4` off `0xfc400000` — the same pair 692's arm had, now reached through a descriptor
  this run writes.
* **The six loads are the same six and every one is a read**: `[r4]`, `[r4,#44]`, `[r4,#80]`, `[r4,#120]`,
  `[r4,#2304]` (= `hc_mem + 0x00`) and `[r4,#2368]` (= `hc_mem + 0x40`) at 0x8000d250-0x8000d268, with
  **no store to any address based on `r4` or `r6` anywhere in the function** — the only `str`s are the
  frame save/restore on `sp`. The two derived bits are `ubfx r1,r7,#7,#1` (0x8000d2e4, `CORE_SW_RST` out
  of `CORE_POWER`) and `and r1,r6,#1` (0x8000d2f4, where r6 has been reloaded with `hc_mode`).
* **`STAGE90_XNU_STORAGE_PROBE=1` is still the last line of `xnu_arm_entry-config.txt` and no switch was
  added**, so the gate's `ENTRY_CFG_KEYS` needed no change and `xnu_arm_entry-sources.txt` is 23 files /
  28 lines, as in the 692 park, with exactly two of them changed — `entry_storage.c` and
  `entry_storage.h`.

**And one reading of 692's abort must not be carried over, which is why it is written down here.** 692's
panic printed `r0 = 0x80487fc8`, and in **692's own image** that address is the string
`xnu_live_storage_bcr` (verified by reading both pools at the same address, rather than from 692's
record). In **this** image the same address is `xnu_live_storage_gcc_section`: the pool moved by three
keys. The two arms schedule the same faulting pair differently — 692's `_bcr` key immediates are placed
*between* the base's materialisation and the load, this arm's are placed *after* both loads — so a repeat
fault at 0x8000d1dc here would print a leftover `r0 = 0x80488168` (`_ttbr1`, the last key written before
the loads are scheduled). **The `pc` identifies a fault site; a data abort's `r0` is a leftover
register**, and 692's doc is right about 692 and would be wrong if read against this arm.

## 5. The payload half, and why `stage90-build-config.txt` cannot name this arm

The entry image is embedded in the payload, so both were rebuilt (exit 0 both times):

| file | this arm | bytes | vs the 692 park |
| --- | --- | --- | --- |
| `xnu_arm_entry.bin` | `1fc30bfe…` | 5,519,996 | the one binary this step changed |
| `xnu_arm_entry.elf` | `d187d494…` | 6,699,000 | same size as 692's |
| `stage90-qcdt.img` | `69744038…` | 8,540,160 | same size as 692's |
| `stage90.bin` | `f5425e0e…` | 6,015,492 | same size as 692's |
| `stage90.elf` | `3caacb44…` | 6,077,564 | same size as 692's |
| `stage90.img` | `68d240ee…` | 6,019,072 | same size as 692's |
| `stage90-build-config.txt` | `6c2b6038…` | 682 | **byte-identical** |
| `stage90_fixture.macho` | `52bc9c35…` | 1,744 | **byte-identical** |

**The payload's own switch record is byte-identical to the 692, 690, 688, 686, 678, 653 and sleeper arms'
and therefore cannot name this arm** — the entry-side switches are not in the payload's dump, and the
payload-side change is exactly one thing: the entry blob it embeds. The record that names this arm is
`xnu_arm_entry-config.txt`, whose last line is `STAGE90_XNU_STORAGE_PROBE=1` and which differs from 692's
in **exactly one line** — `STAGE90_XNU_ENTRY_SHA256=`, from `0da7313b…` to `1fc30bfe…` (measured by
`diff`, not inferred from the size, which is 875 bytes in both). Every payload artifact keeps 692's byte
count, so the diff is inside the embedded blob and the entry bss-size immediates, as it has been for
every arm since the blob was introduced.

## 6. Pre-registered before any press

No device was touched for any of this, and these are the cells the press will be read against. They are
stated as *the reading implies*, so a run that produces something else is a finding and not a silence.

* **`_gcc_map = 1` with `_gcc_slot_before = 0` and `_gcc_desc` a block descriptor whose PA field is
  `0xfc400000`** ⇒ the gate's megabyte was unmapped before this arm and this arm mapped it. `_gcc_map = 0`
  ⇒ the install refused, `_gate_read = 0`, and none of `_bcr`/`_cbcr`/`_gate` is published.
* **`_gate_read = 1` with `_bcr` and `_cbcr` both readable** ⇒ **the reading 692 could not take**: the GCC
  block answers, and the `+4` offset between the branch's BCR and CBCR is confirmed on hardware rather
  than inherited from `clock-8974.c` plus the UART pair's own `+4` (528 §8).
* **`_gate = 1`** ⇒ the six loads run. `_gate = 0` with `_gated_out = 1` and `_loads = 0` ⇒ installed,
  clocked off, deliberately not touched — and, on this arm, with `_map` **published anyway**, which is
  the whole reason for the reorder.
* **`_map = 0` with `_gate_read = 1`** ⇒ the gate's megabyte went in and the controller's own section did
  not, which is the third cell and was unreachable on 692.
* **`_loads = 6` with `_writes = 0`** ⇒ the controller answered and nothing was written to it. This is
  the run the arm exists for, and `_mode_bit` (`CORE_HC_MODE & HC_MODE_EN`) and `_sw_rst`
  (`CORE_POWER >> 7 & 1`) are the two bits 531 §6 left open, now read from the state the bootloader
  handed over.
* **`_mci_version` of 0 or `0xffffffff`** ⇒ *the block did not answer* — a reading of this arm and not a
  failed run.
* **The negative cell is a NON-RETURN (exit 2)**, and on this arm it means something narrower than it did
  on 692: *the gate was read, said the branch was on, and the block still did not answer*. A mapping
  failure with a live gate is a translation fault and comes back with a log — 692 proved that — and the
  two map-failure fault addresses are `0xfc400000` (the gate) and `0xf9824000` (the controller), which is
  what makes the two installs tellable apart in a fault record.
* **`_gcc_map = 1` beside a readable `_bcr` is also the reading that `entry_epilogue`'s PS_HOLD store at
  `0xfc4ab000` becomes reachable** (§3). It does **not** make the reset path work: `0x0fa0065c` is a
  different megabyte and is still unmapped, which is why a forced ending still faults on its own first
  store.
* **The ending is 690's clock and it did not move**, so `xnu_live_post_end_calls` is still this arm's own
  ending cell; the probe's records precede it because the probe's call is placed before the clock's
  block.

## 7. What this does not do

* **It does not put the OS anywhere new.** The probe is a **read-only** device probe behind a gate, and
  the boot's own frontier is unmoved: XNU reaches pid 1, runs the userland phase, and dies at the idle
  exit's `pop {fp, pc}`. This arm asks what the eMMC controller says; it does not make XNU drive it, it
  does not register a block device, and it does not mount anything.
* **It does not touch storage in any direction.** No write to the controller (`_writes` is published, and
  the ELF has no store through either base), no `flash`, no partition table, no filesystem — `fastboot
  boot` only.
* **`xnu_live_storage_writes = 0` is the arm's safety reading, not a comment.** The vendor's own probe
  opens with three writes to this block (`CORE_HC_MODE ← 0`, `CORE_SW_RST`, `HC_MODE_EN`) and 531 §8 adds
  a fourth reason not to make them: writing 0 to `POWER_CONTROL 0x29` **is** a bus-off request. The
  answer this arm buys is bought with loads alone.
* **The `0x0FA` megabyte is still unmapped and the PS_HOLD store is still unproven.** No arm has been
  observed to complete one; the only thing this step changes about that is that one of its two
  prerequisites is now reachable.
* **TWRP-to-storage stays withheld.** The user's condition for it is that the OS can already be entered;
  the OS is not observed booting, so the condition is unmet and the write has not been made.

## 8. Owed

* **The press itself** — one readiness chain, one gate, exactly one runner, one non-persistent
  `fastboot boot`, with the flags and the arm name readiness prints (`--allow-xnu-entry`,
  `--expect-arm=armed-storage-gcc-1fc30bfe`) and the neighbour `33e80afe` off the bus.
* **The gate's storage tripwire still does not see this address** (692 §4, reported to the peer lane by
  message and not edited here): `stage90.elf` carries `movt r4,#0xf982` at file offset 580376 and, in
  this arm, `movt r6,#0xfc40` too — and `STORAGE_SYM_RE` in `preflight_boot_check.sh:1280` has no
  `storage`, while `tools/check_storage_refs.py` disassembles the payload where the entry image sits as
  **data**. Pointed at `xnu_arm_entry.elf` it fires; at the payload it prints `none` about an image that
  carries the address twice.
* **Carried from earlier steps, unchanged**: the 691 §5 one-store `entry_note_wfi` readback;
  `entry_reset.h`'s false IMEM claim; the `RESTART_REASON` decision; 676 §6 / 677 §6; the 684-owed
  runner clause for the 678 arm; `tools/xnu_dt_requirements.py` not encoding the `"master"` value; and
  the open question of **what actually returns a run** (17 s on 692 against the armed 25 s + 3 s
  watchdog, `platform_reboot` never entered and leaving no line, and a 60 s dead-man that leaves no dump).
* **The step after this one, named so it is not re-derived**: if the gate says the branch is on and the
  block answers, the next question is the *mode sequence* — whether `CORE_HC_MODE`'s `HC_MODE_EN` reads
  set on the handed-over state (then the vendor's first two writes are a re-do) or clear (then they are a
  prerequisite) — and after that the first **write** to this block, which is a step that must be
  pre-registered as a write before it is built.
