# 696: the storage arm's first write — the vendor's mode sequence, pre-registered before it is built

694 pressed the storage probe and the probe **answered**, all twenty-eight of its keys, none of them
through a fault: the eMMC controller's windows are reachable through a descriptor this arm writes
itself, SDCC1's branch clock is enabled, the block answers with non-zero and non-saturated words, and
**nothing was written to it** (`_writes = 0`). The one bit that press was bought for came back
**clear**:

```
xnu_live_storage_hc_mode     0x00002000     CORE_HC_MODE, as the bootloader left it
xnu_live_storage_mode_bit    0x00000000     HC_MODE_EN is CLEAR
xnu_live_storage_sw_rst      0x00000000     the core is not being held in reset
xnu_live_storage_core_power  0x00000441     bus power on, CORE_SW_RST clear
xnu_live_storage_mci_version 0x10000011     the block answers
```

So 531 §6's open question — whether the vendor's mode sequence is a *prerequisite* or a *re-do* — is
answered, and it is answered **in the direction that costs a write**: the block the payload inherited
is **not** in SDHCI mode, and the sequence the vendor driver runs at probe has not run. This step is
that sequence, and it is the first arm in this project that **writes to a device block** rather than
reading one.

**Nothing is sent to the device in this step.** This record is written *before* the arm is built, which
is what the 694 record requires of it ("must be pre-registered as a write before it is built, must name
its polling bound and its failure path"); the build, the park, the record entry, the gate and the press
follow, and the press is the next step's act.

## 1. What "the sequence" is, quoted rather than retyped

`external/android_kernel_xiaomi_cancro/drivers/mmc/host/sdhci-msm.c:2841-2868` is the vendor's own
bring-up, and every offset in it is a `#define` in the same file (`:55-60`):

```c
	/* Unset HC_MODE_EN bit in HC_MODE register */
	writel_relaxed(0, (msm_host->core_mem + CORE_HC_MODE));

	/* Set SW_RST bit in POWER register (Offset 0x0) */
	writel_relaxed(readl_relaxed(msm_host->core_mem + CORE_POWER) |
			CORE_SW_RST, msm_host->core_mem + CORE_POWER);
	/*
	 * SW reset can take upto 10HCLK + 15MCLK cycles.
	 * Calculating based on min clk rates (hclk = 27MHz,
	 * mclk = 400KHz) it comes to ~40us. Let's poll for
	 * max. 1ms for reset completion.
	 */
	ret = readl_poll_timeout(msm_host->core_mem + CORE_POWER,
			pwr, !(pwr & CORE_SW_RST), 10, 1000);
	if (ret) { ... "reset failed" ... }
	/* Set HC_MODE_EN bit in HC_MODE register */
	writel_relaxed(HC_MODE_EN, (msm_host->core_mem + CORE_HC_MODE));

	/* Set FF_CLK_SW_RST_DIS bit in HC_MODE register */
	writel_relaxed(readl_relaxed(msm_host->core_mem + CORE_HC_MODE) |
			FF_CLK_SW_RST_DIS, msm_host->core_mem + CORE_HC_MODE);
```

with `CORE_HC_MODE 0x78`, `HC_MODE_EN 0x1`, `FF_CLK_SW_RST_DIS (1 << 13)`, `CORE_POWER 0x0`,
`CORE_SW_RST (1 << 7)`. Four stores, one poll, and the arm performs exactly those four in that order,
reading the register back after each store and publishing the readback.

**And the handed-over word is the vendor's destination minus exactly one bit.** `_hc_mode` reads
`0x00002000`, which is `FF_CLK_SW_RST_DIS` — the last store's field — with `HC_MODE_EN` **clear**. So
the block is not in some unknown state this arm has to discover: it is in the state the vendor's
sequence leaves it in, one bit short, with `CORE_SW_RST` already clear. That is a second confirmation
of 694's reading, from the other side, and it is why the sequence's first and last stores both matter:
store 1 writes `0` and therefore **clears `FF_CLK_SW_RST_DIS`**, so an arm that did only the last two
stores would leave the block worse than it found it (FF clock reset enabled, where the bootloader had
it disabled) unless it re-set bit 13 — which is what store 4 does.

## 2. The two registers this arm deliberately does not touch, and the one it reads without clearing

* **`POWER_CONTROL 0x29`** — the SDHCI standard's own power register, at `hc_mem + 0x29`. 531 §8 read
  the vendor driver's avoidance of it out of the driver's own text: on this SoC **writing 0 to it *is*
  a bus-off request**. The mode sequence does not need it — the bus is already powered (`_core_power`
  bit 0 is set) — and the arm does not write it. It is not read either: a load from an address whose
  meaning this arm is not going to act on is a load that can only fault.
* **`CORE_PWRCTL_CLEAR 0xE4`** — the vendor writes the pending power-IRQ status back there to
  acknowledge it, and the acknowledge is for the benefit of a Linux driver about to register a handler.
  This arm registers none, so the acknowledge would be a write with no purpose. **It is not written**,
  and the *status* is read instead: `CORE_PWRCTL_STATUS 0xDC` is one of the two ways this arm can say
  whether `CORE_SW_RST` latched anything. The register is a status register — the vendor's own sequence
  reads it and then clears it in a separate store — so reading it changes nothing.
* **Nothing else in the block is touched.** No command is issued, no data is transferred, no partition
  table is read and no filesystem is mounted: the eMMC device itself is not addressed by this arm at
  all. `fastboot boot` only, never `flash`.

## 3. The bounds, and the one thing a bound cannot bound

The vendor polls with a **1 ms** ceiling (`readl_poll_timeout(..., 10, 1000)` — a 10 µs interval,
1000 µs total) against a reset the hardware documentation says takes ~40 µs. This arm's ceiling is
**100 ms**, which is 100× the vendor's, one twentieth of the fixture's own 2000 ms park, and 1/60 of
the arm's own 6 s ending:

| bound | value | why |
| --- | --- | --- |
| tick budget | **1,920,000** CNTVCT ticks = **100 ms** at 19,200,000 Hz | a *time*, not a count — the rate is measured (`xnu_live_post_cntfrq`, 691 and 694, and it agrees with the device tree's 19200 ticks/ms) |
| outer steps | **4096** | the backstop, sized so it cannot fire before the tick budget does (~126 ms of reads at 30 ns each), and iterated only when the bit is still set |
| reads per step | **1024** | one `CNTVCT` read per 1024 device reads, so the clock is not over-sampled |
| stores | **4** | the vendor's four, counted and published as `xnu_live_storage_writes` |

**And the honest half: the bound bounds the poll, not a read that never returns.** A load from a block
whose clock is off is not a fault on this SoC's fabrics — 531 §8 and this file's own header call it a
bus wait nothing ends — so if this arm's gate reading were wrong, the *first* `CORE_POWER` read would
hang inside the poll and no bound in the image would end it. The bound exists for the case it can
address (a reset that never completes) and the gate exists for the case it cannot: the sequence runs
**only** with `gate == 1`, and the gate's own two words were measured on hardware in 694 (`_bcr = 0`,
`_cbcr = 0x00004ff1`, so the branch enable is set) rather than inferred from the offset's arithmetic.

**One new hazard, named here rather than discovered with the device on the bench.** The vendor's own
comment says the reset "may trigger power irq if previous status of PWRCTL was either BUS_ON or
IO_HIGH_V", which is why it acknowledges the status afterwards. This arm leaves the status latched, and
whether that shows up anywhere depends on state this arm cannot read: the SDCC's interrupt line, the
GIC distributor's enable for its SPI, and XNU's own IRQ path for an SPI it has no handler for. It is
recorded as an open question, not as a managed risk the arm has mitigated — the reading that would bear
on it is `xnu_live_storage_mode_pwrctl_status` below, and it is published for exactly that reason.

## 4. The guards: how many there are now, and which one is new

The gate's interlock was written for **reads** — a load from an unclocked block is the one failure this
project cannot read a log out of — and this arm makes it guard **four stores**, which is a strictly
stronger reason to keep it. On top of it the arm adds **one** refusal, and the choice of it is the
step's one safety decision:

* **the gate** (`cbcr & 1`): unchanged, and now guarding writes as well as loads;
* **the readback that must precede any store** — `_mci_version` (`0x10000011` on 694's run) is neither
  `0x00000000` nor `0xffffffff`. A block that answered a read with either value is a block that is not
  answering, and a sequence that writes `CORE_POWER` to a block that is not answering is the one thing
  this arm could do that a reboot might not undo. `xnu_live_storage_mode_refused = 1` with
  `_mode_stage = 0` and `_writes = 0` is that cell, and it is reached without a single store.

## 5. Pre-registered: the cells this arm will be read against

Stated as *the reading implies*, so a run that produces something else is a finding and not a silence.
Every one of them is a key the arm publishes, and the sequence's keys are all written **before** 690's
clock block runs (the probe's call site is unchanged and precedes it), so a run that ends at the ending
still carries them.

| cell | reading | what it means |
| --- | --- | --- |
| **`_mode_refused = 1`, `_writes = 0`, `_mode_stage = 0`** | no store was made | the block did not answer a read (`_mci_version` 0 or saturated), so the arm refused itself — the safe cell, and the only one in which this arm writes nothing |
| **`_mode_w0_read = 0`** | `HC_MODE` reads back `0` after store 1 | the store landed, and `FF_CLK_SW_RST_DIS` is now **clear** on this block |
| **`_mode_rst_cleared = 1` with `_mode_rst_ticks` inside the budget** | `CORE_SW_RST` cleared | the reset completed; `_mode_rst_ticks` is the time it took, and the vendor's own claim (~40 µs) is the number to compare it against |
| **`_mode_rst_cleared = 0`, `_mode_timeout = 1`, `_writes = 2`, `_mode_stage = 4`** | the poll expired | **the failure path**: the arm makes **no further store**, and `_mode_w0_read` and `_mode_power_wr` say exactly what state it left. A reset that does not complete is a reading about the block, and this cell is why the poll is bounded |
| **`_mode_bit_after = 1`** | `HC_MODE & HC_MODE_EN` after the sequence | **the arm's question, answered**: the block is in SDHCI mode where it was not, and 531 §6's "prerequisite" is now the state rather than the prediction |
| **`_mode_w2_read = 0x00002001`** | the final `HC_MODE` | the block is in the state `sdhci-msm.c` leaves it in, bit for bit |
| **`_mode_hci_version` / `_mode_capabilities` against `_hci_version` / `_capabilities`** | the two standard registers re-read **in SDHCI mode** | the *reading* the arm exists for. 694's pair (`0x10`, `0x742dc8b2`) was taken through a block **not** in SDHCI mode and is therefore not a spec-valid pair (694 §3). A post-sequence pair that differs is the mode change made visible; a post-sequence pair that is **identical** is the finding that the mode bit alone does not change these two words, and the honest reading of the previous sentence then applies again |
| **`_mode_pwrctl_status`** | `CORE_PWRCTL_STATUS` after the reset | the hazard of §3, measured: non-zero says the reset latched a power-IRQ status (the vendor's own comment predicts it when the previous state was BUS_ON, which `_core_power = 0x441` says it was) |
| **`_writes = 4` with `_mode_stage = 8`** | the whole sequence ran | the arm's complete form; anything less is localised by the two keys together |
| **a non-return (exit 2)** | no capture | this is the first arm that can spend a press with nothing to show. It means the *gate* was wrong or the block did not tolerate the reset, and it refutes nothing about the cells above — which is why the gate read is taken first and published before any store |

**And the arm's own instrument, declared rather than assumed.** The probe's first call is guarded by
`g_storage_probed`, so the sequence runs **once**, on the wrapper's first call, like the six loads. The
value of `_writes` is published at the end of the path taken, so a run that returns from any of the
three early exits still says `0` — the count is a reading of what this arm *did*, not of what it
contains.

## 6. The switch: why the existing one's domain widened instead of a new one being added

The arm needs to be tellable apart from the read-only probe of 693/694, and the switch that names it is
`STAGE90_XNU_STORAGE_PROBE`. Three facts decide the shape, and each was measured rather than assumed:

* **a new switch cannot be added by this lane.** `preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` (the
  gate's key list) refuses any `STAGE90_*` key the entry record carries and the list does not print
  (`:620-623`), and that file is the peer lane's — the same boundary that has this project reporting
  gate defects by message rather than editing them (692 §4).
* **a switch that is not recorded is worse than no switch**, which is the reason the refusal exists:
  "a switch recorded on one side and inert on the other" is this project's oldest defect.
* **688 solved the same problem by widening a domain and not the name** (`STAGE90_XNU_POST_END_RUN` went
  from a flag to a count 0..4, `#error`-guarded at both ends, and *no key list moved* — "the first step
  in this sequence with no cross-lane half").

So `STAGE90_XNU_STORAGE_PROBE` becomes a **rung**, `#error`-guarded above 2:

| value | the arm |
| --- | --- |
| `0` | the object links and its body compiles to nothing — 690's clock arm with a passenger |
| `1` | the read-only probe: two installs, the gate, six loads — **the arm 694 pressed** |
| `2` | that probe **and** the vendor's mode sequence — the first arm that writes to the medium's controller |

**The name is now the weaker half of the key, and that is recorded rather than papered over.** A switch
called `PROBE` whose value 2 performs four stores is a name that understates its value; the alternative
was an arm a reader could not tell from a read-only one without a hash lookup, and this project has
already paid once for two arms that answered a reachability sentence identically (653). The file's own
prose names the key as historical and the value as a rung, and the gate's narration paragraph for this
key — the peer lane's text — says "the probe ... reads six of its registers" and is now **one value
short**; it is reported by message and not edited here.

## 7. The build's own refusals, and the three things they made visible

### 7.1 The first refusal, and the constant it moved

The entry build refused the first attempt, and it was right to:

```
FAIL: the exit's call to FlushPoU_Dcache is at 2147775192 and returns to 2147775196,
      while entry_trace.c's STAGE90_XNU_SEAM_LR is 0x800462dc: ...
```

`0x800472dc` is what this arm's image carries, `0x800462dc` is what the previous arm's did. **So the
seam's identification constant had to move, and the clause that compares it against the linked image is
the reason it was noticed rather than the reason it was avoided** — the mechanism 556 built for exactly
this, working. Three things about the measurement are worth keeping:

* **The shift is not the size of the addition.** `entry_storage_probe` grew by `0x314` (the mode
  sequence), and the kernel text that follows it moved by **two different amounts**: `cpu_idle`
  `0x8000dce4 → 0x8000dff8` (**+0x314**) and `platform_cache_idle_enter` `0x80046238 → 0x80047238`
  (**+0x1000**). So the entry group's growth is passed through literally until some alignment point and
  rounded up past it — which is [[mi4-linker-fill-term]]'s `Σ(inputs) + Σ(aligned fills)` measured on a
  real arm, and the reason a reader cannot compute the new address from the old one plus a delta.
* **`.text` grew `0x1200`** (`5,308,680 → 5,313,288`) and the entry **bin** grew `0x4000`
  (`5,519,996 → 5,536,380`) — the bin is the image the payload embeds, and its extra step is the boot
  image's own alignment, not code.
* **And the constant is a kernel address pinned in an entry source**, which is the class
  [[mi4-one-value-two-definitions]] and 520 are about: right until the kernel's text moves, silent
  afterwards. The clause makes it *loud* rather than silent, and that is the whole of its value — but the
  pin is still a hand-copied number, and the repair (a link order that puts the entry objects after
  Apple's, or a symbol the linker resolves) is **owed and not done here**, because this step's question
  is the mode sequence and a link-order change would move every address in the arm at once.

### 7.2 The store census: the four writes as a reading of the artifact, not a sentence in a comment

**This is the first arm in this project that stores to a device block, so "the writes are safe" cannot be
a sentence in a comment** ([[mi4-a-claim-in-a-comment-is-not-a-check]]). What replaced the sentence is a
new clause in `build_entry.sh`, run under the same `-ge 2` rung that turns the sequence on: it resolves
`entry_storage_probe` out of the **linked** `xnu_arm_entry.elf`, disassembles it up to the next global,
takes every store whose operand's base register is not `sp` as `<base>:<offset>` in program order, and
refuses the build unless the offsets on the **modal** base are exactly `120 0 120 120` in that order and
every store on any other base is an offset-0 pointer write (the body's own first-call guard). The
specific hazard it makes unreachable rather than reviewed is `POWER_CONTROL 0x29`, which lives in the
**other** window (`hc_mem`): on this SoC writing 0 to it **is** a bus-off request (531 §8), and nothing in
`entry_storage_probe` stores through `hc_mem` at all - which is now a property of the image the gate
boots.

**It was read in both directions before the arm was parked**, because a check that has only ever passed
cannot be told from one that never ran ([[mi4-silence-is-a-reading-only-if-success-is-silent]]):

```
rung 2   (out/stage90, the arm this record names)
         census: base=r5 off=[120 0 120 120 ] other=[r2:0 ] bad=[]      VERDICT: ACCEPT
rung 1   (the 693/694 park, armed-storage-gcc-1fc30bfe, hand-run through the clause's own text)
         census: base=r2 off=[0 ] other=[] bad=[]                       VERDICT: REFUSE
```

The four, read out of the disassembly by hand as a second derivation: `str r4,[r5,#120]` at
`0x8000d344`, `str r7,[r5]` at `0x8000d380`, `str r3,[r5,#120]` at `0x8000d470` and `str r4,[r5,#120]`
at `0x8000d4b0` - the vendor's order, `0x78`/`0x00`/`0x78`/`0x78` - with the `.bss` guard `str r6,[r2]` at
`0x8000d068` as the only other store in the body. **Two derivations, one answer.**

**The honest wart, recorded rather than smoothed over**: the base the offsets are counted on is **modal**,
so on a one-store body the modal base is the guard's register and the refusal above is a *base* mismatch
rather than a *sequence* mismatch. The clause never runs on a rung-1 arm (it is inside `-ge 2`), so this is
a property of the control and not of the arm - but a reader who feeds the clause another image should read
the census line and not the verdict alone.

**And adding the clause changed no byte of the arm.** The clause is a check and not code, and the
measurement is the strongest available form of that statement: the arm was parked, the clause written, the
entry image rebuilt with the same seventeen switches, and `xnu_arm_entry.bin` came back **`57d55fc9…`**,
`xnu_arm_entry.elf` **`5008170d…`**, `stage90.bin` **`33f38925…`**, `stage90-qcdt.img` **`9a4a8695…`** -
byte for byte the parked arm, with the payload build reporting "nothing to install". The one member that
moved is the source manifest's `build_entry.sh` line: **one arm, two manifests**, and the manifest that
stands is the one whose `build_entry.sh` line is the live file's - the rule that refused 688 once for
naming a `build_entry.sh` the tree no longer had.

## 8. What this does not do

* **It does not put the OS anywhere new, and it does not make storage usable.** Four stores to a
  controller's mode register put the block in SDHCI mode; they do not register a block device, do not
  read a sector, do not mount anything, and do not touch the flash medium at all.
* **It does not prove the boot survives it.** The frontier is unchanged: XNU reaches pid 1, runs the
  userland phase, and idles past the exit's `pop` until the arm's own clock ends the run (687-691).
* **It does not touch `POWER_CONTROL 0x29`**, and nothing in it asks the bus to change state: the bus is
  already powered and the arm leaves it that way.
* **TWRP-to-storage stays withheld.** The user's condition for it is that the OS can already be entered;
  the OS is not observed booting and staying, so the condition is unmet and no write to storage is made.

## 9. Owed

* **The press itself** — one readiness chain, one gate, exactly one runner, one non-persistent
  `fastboot boot`, with the flags and the arm name readiness prints, and the neighbour `33e80afe` off
  the bus. The arm is parked and recorded (`armed-storage-mode-57d55fc9`, 11 files, verified in `out/`,
  in the in-tree park and in the export park), so the press is the next step's act and nothing else's.
  **The whole of §5 is unread until then**: every cell in it is a pre-registration.
* **A second copy of the seam's address, and readiness caught it with nothing sent** (§7). The entry
  side's `STAGE90_XNU_SEAM_LR` was not the only pin: `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` (the
  fallback the reader uses when the ELF cannot be read) carried `0x800462dc` - the value every arm from
  678 to 694 had - while this arm's `platform_cache_idle_exit` returns from its `bl FlushPoU_Dcache` to
  **`0x800472dc`**. `preflight_boot_check.sh:2579` compares that literal against the live ELF and
  refuses the disagreement, and **readiness row 3 ran the gate before any press**: `FAIL the gate accepts
  this tree`, readiness exit 1, **the press unspent** - the mechanism working, on an arm whose whole
  point is that a spent press cannot be re-taken. The direction was decided by the disassembly and not
  by the record the gate names both halves of ("the runner's literal is the one owed a change"), and the
  change is in the runner beside a comment recording the class. **The structural repair is owed**: a
  fallback that must be edited per arm is a pin wearing the name of a fallback, and removing it needs
  the gate's clause to accept a labelled absence - the peer lane's file, so a step and not an edit here.
  Readiness's own row-4 narration also gained the rung-2 branch, so the press log now names the arm that
  writes rather than describing a read-only one.
* **What the census does not cover, and why that exclusion is a reading and not a convenience** (§7.2):
  the clause skips stores whose base register is literally `sp`, so "every store this body can make to a
  device register" is only a claim about the *included* class if nothing ever points `sp` at a device.
  Measured on this image: the three instructions in the whole body that write `sp` are `sub sp, sp, #28`,
  `add sp, sp, #28` and `add sp, sp, #32` - constants, and no `mov`- or `ldr`-into-`sp` anywhere - so the
  excluded class is the body's own frame and nothing else. **A store through a derived address is not
  excluded**: it appears as `<that register>:<offset>` and is refused by the `bad=[]` test, which is why
  that second test exists beside the four-offset one.
* **The gate's narration for `STAGE90_XNU_STORAGE_PROBE`** is one value short after this step (§6) —
  the peer lane's text, reported by message, not edited here. The gate's *key list* needs nothing:
  the switch's name did not change, which is the whole reason the rung was built this way.
* **A kernel address pinned in an entry source** (§7): `STAGE90_XNU_SEAM_LR` is a hand-copied `0x8004…`
  that this step had to move once. The clause catches a wrong value; nothing prevents one. The repair is
  a link order or a linker-resolved symbol, and it moves every address in the arm, so it wants a step of
  its own.
* **Carried, unchanged**: the 691 §5 one-store `entry_note_wfi` readback; `entry_reset.h`'s false IMEM
  claim; the `RESTART_REASON` decision; 676 §6 / 677 §6; the 684-owed runner clause for the 678 arm;
  `tools/xnu_dt_requirements.py` not encoding the `"master"` value; the two peer-lane tripwire repairs;
  and **what actually returns a run** (8 s / 17 s / 24 s), which 691 attributed to the ending's own
  PS_HOLD and which no arm has yet measured from the other side.
* **The step after this one, named so it is not re-derived**: with the block in SDHCI mode, the next
  readings are the standard register file (which this arm's post-sequence pair is a first look at) and
  then the *card*: `CORE_PWRCTL_CTL`'s bus-power/IO-voltage state before any command is issued, since a
  `CMD0`/`CMD8` sequence against a bus whose power state the OS has not set is the first act that the
  medium itself would see.
