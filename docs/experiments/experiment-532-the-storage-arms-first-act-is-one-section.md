# 532: the storage arm's first act is one section, and both windows are in it

**A host-side reading: no device, no build, no switch.** It opens the storage line at the level *below*
531. 531 read the controller's register file — `hc_mem` at `0xf9824900`, `core_mem` at `0xf9824000`, the
mode bit in `CORE_HC_MODE 0x78`, the PIO data path. This reads the prerequisite that has to be satisfied
before a single one of those registers can be touched at all: **the address translation that answers for
`0xf9824900` in the context the arm will run in.** The source is the payload's own image — `mmu.c`,
`xnu_arm_vm_init_full_pmap.c`, `entry_stubs.c`, `entry_gic.c` — and the last captured log, for the values
the image cannot state.

In one paragraph: **XNU's page tables do not map the eMMC controller, and the mechanism that puts a device
section into them already exists and is already proved — but it is one 1 MB section per call, and both of
531's windows are inside the *same* one.** `0xf9824000 >> 20 == 0xf9824900 >> 20 == 0xF98` (index 3992), so
the arm's first act is **one** `entry_mmio_section(0xf9824000, 0xf9824000, …)`, not two. An arm that turns
531's two-window table into two calls writes one L1 slot twice and the second call is **refused** — the
installer skips an occupied slot rather than clobbering it — so the mistake is loud and cheap rather than
silent. The same reading says what else is *not* mapped, and it is not a short list: the console's own sink
at `0xF991E000` (528's address) is index `0xF99`, and 528's pinmux at `0xFD510000` is index `0xFD5` — a
*different* section each, so the two front-end arms do not share a mapping and each adds one. And it says
why "the GIC works, so device addresses work" is not evidence: the GIC's section `0xF90` is in the identity
table *and* installed by the GIC probe itself, which is the very precedent the storage arm has to follow.

## 1. Three tables, and the controller is in none of them

The payload builds two L1 tables of its own, and a handed-off kernel runs under a third.

**`mmu.c`'s identity table** — `build_identity_table()` (`mmu.c:5404`), installed by `enable_identity_mmu()`
(`mmu.c:5495`, `write_ttbr0((uint32_t)(uintptr_t)stage90_l1_table)`). Its device sections are the whole
list:

| line | call | section |
| --- | --- | --- |
| `mmu.c:5455` | `map_section_mmio(STAGE90_GIC_ALIAS_BASE, 0xf9000000u)` | `0xC40` → PA `0xF90` |
| `mmu.c:5458` | `map_section_mmio(0x0fa00000u, 0x0fa00000u)` | `0x0FA` |
| `mmu.c:5462-5463` | `map_section_desc(RAM_CONSOLE_BASE …)` ×2 | `0xDE5`, `0xDE6` |
| `mmu.c:5483` | `map_section_mmio(0xf9000000u, 0xf9000000u)` | `0xF90` |
| `mmu.c:5486` | `map_section_mmio(0xfc400000u, 0xfc400000u)` | `0xFC4` |

**`xnu_arm_vm_init_full_pmap.c`'s candidate table** — Phase 4 (`xnu_arm_vm_init_full_pmap.c:414-416`) maps `0xf9000000`, `0xfa000000`,
`0xfc400000`; Phase 5 (`xnu_arm_vm_init_full_pmap.c:452-453`) adds the GIC alias and `0x0fa00000`. Same devices, plus `0xFA000000`
(MSM IMEM) as a section of its own beside the identity table's `0x0FA00000`.

**And the candidate table is a probe, not the live table.** It switches itself in
(`write_ttbr0(candidate_l1_base)`, `xnu_arm_vm_init_full_pmap.c:501-502`), verifies, and switches back (`write_ttbr0(r->original_ttbr0)`,
`xnu_arm_vm_init_full_pmap.c:565`), and the last captured log agrees with the source rather than with the comment that calls it "the
one a handed-off kernel would run under": `stage90_xnu_arm_vm_init_full_pmap_live_ttbr0=0x00654000` with
`restored_ttbr0=0x00600000` — i.e. the payload's identity table (`mmu_ttbr0_after=0x00600000`) is what
`TTBR0` holds again afterwards. So the comment is about the table's *shape*, not about what the MMU walks
after the probe returns, and a reader who takes it literally and edits Phase 4 to add a device section
would be editing a table nothing runs on.

**Neither table contains `0xF98`, `0xF99` or `0xFD5`.** Enumerated by name: the *only* `map_section_mmio` /
`map_l1_section_mmio` call sites in the repository are the nine above plus the aliases. Section `0xFC4` is
there, which is why 528's UART core gate `0xFC400704` and its six CBCRs (`0xFC400684 + 0x80n`) are
reachable, and section `0xF90` is there, which is why 528's BLSP1 AHB vote at `0xF9012484` and the
watchdog at `0xF9017000` are — and `hw_watchdog.c:53` says exactly that, in the same words this section is
about: *"`0xf9017000` is inside the 1 MB section `0xf9000000`-`0xf90fffff`, which both the identity table
and the candidate L1 already map as MMIO, so this needs no new mapping."* The eMMC's window is the same
kind of address with the opposite reading.

## 2. XNU's own tables, and the one mechanism that adds a device section to them

The GIC probe's own header states the rule this whole step rests on, and it was measured rather than
inferred (`entry_gic.c:106-114`):

> **XNU's page tables do not map the GIC.** 308's run measured it … `fleh_irq` runs with XNU's tables live,
> which map `[physBase, physBase + memSize)` and nothing at `0xf9002000` … So the first act of this probe is
> `entry_mmio_section(0xf9000000, 0xf9000000, ...)`: one 1 MB section descriptor into XNU's own L1, with the
> live channel's own attribute (0xc on this device, the strongly-ordered encoding read out of `PRRR`),
> through the same recipe the console's mapping uses.

So the mechanism is `entry_mmio_section(va, pa, &slot_before, &desc)` (`entry_stubs.c:2227`), the GIC's call
is at `entry_gic.c:377`, and it is **the same function** the console's live channel uses — one spelling of
the descriptor recipe, which 482 split out on purpose. Three things about it decide how the storage arm
must be written:

- **The table is read at the install, not latched.** `entry_mmio_section` calls `entry_live_ttb_base()`
  (`entry_stubs.c:2169`) every time, which reads `TTBR0`, `TTBR1` and `TTBCR`, picks `TTBR0` when
  `TTBCR.N == 0` and `TTBR1` otherwise, and masks the low 14 bits (walk attributes, not base). 484 split
  this out after a run in which the console's latch named one table and the live one was another: the
  descriptor was written, the read-back agreed, and the next load from the VA was a translation fault.
  In the last captured log, `xnu_live_ttbcr=0x00000002` — `N = 2` — so **every** device address above
  `0x40000000`, which is all of them, is answered by **`TTBR1`**: the log's `xnu_live_ttbr0` and
  `xnu_live_ttbr1` are both `0x8070004a`, base `0x80700000` after the `0xffffc000` mask.
- **The attribute is not an argument.** `g_live_attr` (`entry_stubs.c:740`, measured `0xc`) is the
  strongly-ordered encoding derived once at the console's first live write, from this machine's `PRRR` and
  `SCTLR.TRE` rather than guessed (`:2301-2320`). A GIC mapped Normal is a peripheral whose registers read
  as whatever a cache line last held, and the failure has no symptom until it has a wrong one — so
  `entry_mmio_section` deliberately takes no attribute.
- **It refuses in four ways, and they are not one reading.** `g_live_state != 1` (the live channel does not
  exist yet, `:2232`) → `0`; `l1 < 0x80000000u || (l1 & 0x3fffu)` (the table is not in the kernel's window,
  `:2242`) → `0`; `index >= 4096u` (`:2114`) → `0`; and `(before & LIVE_TTE_TYPE_MASK) != 0u` (`:2119`) — the
  slot is **occupied** — → `0`. The last one is a *skip*, not a clobber, and it is the one the two-window
  mistake trips.

## 3. The two windows are one section

`SECTION_INDEX(addr)` is `addr >> 20` (`mmu.c:19`), `L1_SECTION_SIZE` is `0x00100000` (`mmu.c:5`), and the
table is 4096 entries (`L1_SECTION_COUNT`, `mmu.c:4`). So:

```
0xf9824000 >> 20 = 0xF98 = 3992      core_mem
0xf9824900 >> 20 = 0xF98 = 3992      hc_mem
```

**531's two windows are the same L1 index.** They are `0xf9824900 - 0xf9824000 = 0x900` bytes apart —
2304 bytes, **0.22 %** of one section — and the section spans `0xf9800000`-`0xf98fffff`, which also holds
the legacy node's other two regions as well — `dml_mem` at `0xf9824800` (0x100) and `bam_mem` at
`0xf9804000` (0x7000), both from `sdcc1`'s own `reg` (`msm8974.dtsi:316-318`) — and whatever else MSM8974
keeps in that megabyte. One section is `0x100000 / 0x900` = **455 times** the span of the two
windows it has to cover, which is the whole of the difference between 531's map and this one.

Three consequences, all checkable:

1. **The arm's first act is one call**, `entry_mmio_section(0xf9824000, 0xf9824000, &before, &desc)`, and its
   descriptor is `0xf981040e`: `LIVE_TTE_PA_MASK` (`0xfff00000`) aligned the PA down to `0xf9800000`, then
   `| 0x00010000` (`LIVE_TTE_BLOCK_SH`) `| 0x00000400` (`LIVE_TTE_BLOCK_AF`) `| 0xc` (`g_live_attr`) `| 0x2`
   (`LIVE_TTE_TYPE_BLOCK`). The same arithmetic on the console's own PA is the descriptor the log carries:
   `xnu_live_desc=0xde51040e` = `0xde500000 | 0x1040e`, and its `xnu_live_slot_before=0x00000000` is the
   fault descriptor it replaced.
2. **A two-call arm refuses its own second call.** After the first call the slot for index `0xF98` holds a
   block descriptor whose type field is `0b10`, so `(before & 3) != 0` and `entry_section_install` returns
   `0` without writing (`entry_stubs.c:2119-2120`). An arm built by copying 531's two-window table into two
   `entry_mmio_section` calls therefore maps the controller correctly and *then* reports a mapping failure —
   which is the good direction for a mistake to fail in, and the reason the GIC probe's own guard
   ("returns without touching a single GIC register if the slot was occupied") must not be read as
   over-caution.
3. **The identity-as-PA habit is safe here only by luck, and the shape of the luck is worth naming.** Both
   windows are inside the section, so `pa = va = 0xf9824000` and `pa = va = 0xf9824900` produce the *same*
   descriptor. A device whose two registers straddled a 1 MB boundary would not have that property, and
   nothing in the installer would say so.

## 4. What is *not* in any section — the other two front-end addresses

The same index arithmetic, applied to the addresses 528 and 531 pinned, and to the ones that do work:

| address | what | index | in a mapped section? |
| --- | --- | --- | --- |
| `0xF991E000` | BLSP1 UART2, the console's sink (528 §1) | `0xF99` = 3993 | **no** |
| `0xFD510000` | TLMM pinmux base (528 §5) | `0xFD5` = 4053 | **no** |
| `0xf9824000` | SDCC1 `core_mem` (531 §1) | `0xF98` = 3992 | **no** |
| `0xf9824900` | SDCC1 `hc_mem` (531 §1) | `0xF98` = 3992 | **no** |
| `0xF9012484` | BLSP1 AHB vote (528 §3) | `0xF90` = 3984 | yes (`mmu.c:5483`) |
| `0xF9017000` | the watchdog (`hw_watchdog.c:94`) | `0xF90` = 3984 | yes (`mmu.c:5483`) |
| `0xFC400704` | UART2 core gate (528 §3) | `0xFC4` = 4036 | yes (`mmu.c:5486`) |
| `0xFC400684 + 0x80n` | the six BLSP1 UART CBCRs (528 §8) | `0xFC4` = 4036 | yes (`mmu.c:5486`) |

So of the five blocks 528 named as "a *read* if the payload only reads it", **three are reachable and two
are not** — and the two that are not are the UART's data/control window and the pinmux, i.e. the half of
that step that would actually put characters out. That is not a correction to 528's reading (the addresses
are right); it is the observation that a register address being right is independent of whether it can be
dereferenced, which is the class this project keeps meeting from the other side.

And the three front-end addresses fall in **three different sections** — `0xF98` for storage, `0xF99` for
the console, `0xFD5` for the pinmux — so no two of them share an install, and the mapping list grows by one
line per device.

## 5. The two spellings of "a device section", and why their difference is not the defect

There are two descriptors for a device section in this image, and they do **not** carry the same bits:

| | descriptor | AP field | `DACR` |
| --- | --- | --- | --- |
| identity table, `mmu.c:14` | `STAGE90_PMAP_DESC_SECTION_SO = 0x00010c02` (`stage90.h:4349`) | `0xc00` (`AP[2:0] = 0b011`, full access) | `0x00000003` — domain 0 **manager** (`mmu.c:5496`) |
| live channel, `entry_stubs.c:2122` | `pa \| 0x10000 \| 0x400 \| 0xc \| 0x2` | `0x400` (`AP[0]` only, privileged RW) | `0x00000001` — domain 0 **client** (measured `xnu_live_dacr=0x00000001`) |

This is recorded rather than fixed, and the reason is the check that separates a defect from a design
difference: **the other half changed with it.** A manager domain ignores `AP`; a client domain enforces it,
so `0x400` is the bit that must be set once `DACR` drops to `0x1`, and a reader who "unified" the two by
making the identity table's descriptor `0x400` would break the payload's own device accesses. The rest of
the recipe (`SH`, the block type, the strongly-ordered attribute, the 1 MB PA alignment) is identical in
both, which is what "one spelling" means here — and the thing that would be a genuine defect is a *third*
spelling, which is why `entry_mmio_section` takes no attribute argument.

## 6. What the arm's first lines are, and the readings that must precede any write

The order is forced, and every step of it is a reading the log can carry:

1. **The live channel must exist.** `g_live_state != 1` is the first refusal, and it is not a nit: the
   attribute a device section needs is *the one a console write has already proved* (`entry_stubs.c:2189-2195`).
   An arm that runs before that has nothing to map with, and "retry later" is the correct answer rather
   than a second attribute.
2. **One install, and its three numbers published.** `entry_mmio_section(0xf9824000, 0xf9824000, …)` returns
   the slot's previous contents and the descriptor it wrote; `_l1` (which table it went into), `_l1_moved`
   (whether that is the table the console latched) and the returned status together say *which* of the four
   refusals, if any, fired.
3. **Only then the register file** — and 531 §10's free reading comes before any write to it: `CORE_POWER
   0x0` (bit 7 is `CORE_SW_RST`), `CORE_HC_MODE 0x78` (bits 0 and 13), `CORE_MCI_VERSION 0x050`. Those three
   words are what turn "the mode sequence is a prerequisite" and "the mode sequence is a re-do" into a
   reading instead of an assumption, and they cost one dereference each.

The mapping is **required and not sufficient**: 531 §6's PMIC/power question still decides whether the card
answers at all, and the arm's first block read is bounded by that and not by the mapping.

## 7. The failure mode if the mapping is skipped

It is measured, and it is 484's own first run: `sleh_abort` at interrupt context, `pc = gicd_read+4`,
`far = 0xf9000000` — a translation fault at the first load from the device, with the descriptor sitting in
a table the MMU had stopped walking. The storage arm's version of the same event is `far = 0xf9824900`, and
it is a *worse* read than 484's because the fault would arrive inside the mode sequence rather than at a
probe's first instruction. This is the reason the mapping is stated as the arm's first act rather than as a
prerequisite in a comment.

## 8. What this does not decide

- **Which side of the jump the arm runs on.** The payload's identity table needs `map_section_mmio` in
  `mmu.c`; a caller inside XNU needs `entry_mmio_section`. They are different files, different tables,
  different attributes and different lifetimes, and one device needs one of them per run. Nothing here
  picks, because the arm's job (a `bdevsw` that answers `DKIOCGETBLOCKSIZE` / `DKIOCSETBLOCKSIZE` /
  `DKIOCGETBLOCKCOUNT`, per 530 §4) is inside XNU and the *first* reading — 531 §9's LBA-1 signature — does
  not have to be.
- **The PMIC/power question**, unchanged from 531 §6.
- **Whether mapping the whole megabyte is safe.** It is the image's only MMIO granularity and every other
  device here already takes it, but `0xf9800000`-`0xf98fffff` is not the controller alone.
- **HFS+ versus a purpose-built read-only filesystem** — 529/530's trade, unchanged by an address.
- **Where the volume comes from.** TWRP, withheld: the goal's precondition 「如果os已经能进去了的话」 is still
  unmet, since XNU reaches user mode and then dies at the idle exit's `pop {fp, pc}`.

## 9. Safety

Nothing in this document is a device action, and nothing in it proposes one that writes storage. The one
hazard it inherits is 531 §11's: `POWER_CONTROL 0x29` written as 0 is, on this SoC, a request to the PMIC to
power the eMMC off. The reading it adds has no hazard of its own — an L1 descriptor write is RAM, it is
reverted by a power cycle, and the same write is already made by the GIC probe on every run that reaches
interrupts. The standing constraint is unaffected: nothing in this project is ever written to storage, so
the worst case remains a phone that needs a power press.

No file outside `docs/` was written by this experiment. **526 stays built, gated, frozen and unrun** —
`out/stage90/stage90-qcdt.img` recomputed `7819cddb50d5a341332aa89d4b6f0479e098e1a19f7d46cc9a2df3a1875ff3e4`
— and the phone is off the bus and owes a power press before it can run: `usb 3-10`'s last event is the
`2717:0368` / serial `4a2fe00b` hand-off to `05c6:f006` and that device's disconnect 85 s later, and both
`fastboot devices` and `adb devices` are empty.
