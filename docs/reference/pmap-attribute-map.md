# ARMv7 Page-Mapping Attribute Map

Every mapping the Stage payload creates, its ARMv7 short-descriptor encoding, and what
Phase 1 has to change. Written down because the roadmap requires it: every later fault gets
blamed on memory attributes first, and a hand-decoded descriptor that is wrong will be
believed for a long time. Decode any descriptor here with:

```bash
python3 tools/decode_armv7_descriptor.py --section    0x00010c02
python3 tools/decode_armv7_descriptor.py --smallpage  0x00000012
```

Field positions follow Apple's own ARM pmap,
`external/xnu-4570.1.46/osfmk/arm/proc_reg.h` (`ARM_PTE_*`), which is the ARM reference this
project builds against — not a decoder written from memory.

## The descriptors currently in the tree

Two constants, both Strongly-Ordered, used for **every** mapping the project creates.

### `L1_DESC_SECTION_SO = 0x00010c02` — 1 MB section

Defined in `stages/stage90/stage90.h` (`STAGE90_PMAP_DESC_SECTION_SO`) and aliased as
`L1_DESC_SECTION_SO` in `stages/stage90/mmu.c` and `stages/stage90/xnu_arm_vm_init_full_pmap.c`.

| Field | Value | Meaning |
| --- | --- | --- |
| type (bits 1:0) | `0b10` | section (1 MB) |
| XN (bit 4) | 0 | executable |
| AP[2] (bit 15) | 0 | — |
| AP[1:0] (bits 11:10) | `0b11` | with AP[2]=0: **PL1 RW, PL0 RW (full access)** |
| TEX[2:0] (bits 14:12) | `000` | — |
| C (bit 3), B (bit 2) | 0, 0 | **Strongly-ordered** |
| S (bit 16) | 1 | shareable (architecturally ignored for Strongly-ordered) |
| nG (bit 17) | 0 | global |
| domain (bits 8:5) | 0 | domain 0 |

### `L2_DESC_PAGE_SO = 0x00000012` — 4 KB small page

Defined in `stages/stage90/stage90.h` (`STAGE90_PMAP_DESC_PAGE_SO`), aliased in
`stages/stage90/xnu_arm_vm_init_full_pmap.c`.

| Field | Value | Meaning |
| --- | --- | --- |
| type (bit 1) | 1 | small page (4 KB) |
| XN (bit 0) | 0 | executable — this is why Stage85's code execution at `0x80000000` works |
| AF (bit 4) | 1 | access flag set |
| AP[1] (bit 9), AP[0] (bit 5) | 0, 0 | **PL1 RW, PL0 no access** |
| TEX[2:0] (bits 8:6) | `000` | — |
| C (bit 3), B (bit 2) | 0, 0 | **Strongly-ordered** |
| S (bit 10) | 0 | **non-shareable** |
| nG (bit 11) | 0 | global |

Note the comment on this constant says "AP=11". It is not: on a small page bit 4 is the
Access Flag in the ARMv7 layout, so AP is only AP[1:0] and reads `0b00` — privileged-only,
which is the correct kernel mapping. The comment is wrong in a harmless direction.

## Every mapping the payload creates

All of these are built by `stage90_xnu_arm_vm_init_full_pmap_run()` into
`stage90_candidate_l1` / `stage90_candidate_l2_pool`, or by `mmu.c`'s `build_identity_table()`
into `stage90_l1_table`. `virt_base` is `0x80000000` (`STAGE90_VIRT_BASE`). The two tables
are not identical — see F-AM1 for the one place they disagree, which matters.

The descriptor column describes the **default** `SO_ONLY` mode. Under
`STAGE90_PMAP_ATTR_MODE_NORMAL_NC` every "DRAM" row uses the Normal/Non-cacheable descriptor
instead, and the MMIO rows do not change — see *Consistency per physical address* below for
which is which.

| Purpose | VA | PA | Granularity | Descriptor |
| --- | --- | --- | --- | --- |
| Stage image identity | `0x00000000` | `0x00000000` | 1 MB | section SO |
| Extra identity headroom | `0x00100000` | `0x00100000` | 1 MB | section SO |
| High-VA kernel image | `0x80000000`–`0x800fffff` | `0x00000000`–`0x000fffff` | 4 KB × 256 | L2 small page SO |
| RAM direct map | `0x80200000` + 256 MB | same | 1 MB × 256 | section SO |
| RAM-console identity | `0xde500000`, `0xde600000` | same (identity) | 1 MB × 2 | section SO |
| GIC identity | `0xf9000000` | `0xf9000000` | 1 MB | section SO |
| MSM IMEM identity | `0xfa000000` | `0xfa000000` | 1 MB | section SO |
| PS_HOLD identity | `0xfc400000` | `0xfc400000` | 1 MB | section SO |
| High alias of image | `0xc0000000` | `0x00000000` | 1 MB | section SO |
| High alias, 2nd MB | `0xc0100000` | `0x00100000` | 1 MB | section SO |
| RAM-console alias | `0xc0300000` | `RAM_CONSOLE_BASE` | 1 MB | section SO *(candidate table only)* |
| GIC alias | `0xc0200000` | `0xf9000000` | 1 MB | section SO |
| IMEM low alias | `0x0fa00000` | `0x0fa00000` | 1 MB | section SO |
| Candidate L1 self-map | `candidate_l1_base` | same | 1 MB | section SO *(candidate table only)* |
| L2 pool self-map | `l2_pool_base` | same | 1 MB | section SO *(candidate table only)* |

## Findings

**F-AM1 — the two L1 tables resolved the same VA conflict in opposite directions. FIXED
2026-09-16 by moving the alias, not by picking a winner.** `0xc0100000` was claimed twice:
the high-alias extension mapped `0xc0100000 -> 0x00100000`, and the RAM-console alias mapped
the same VA to `RAM_CONSOLE_BASE`. One L1 slot, two callers:

- `mmu.c`'s `build_identity_table()` kept the **second-MB image alias** and commented the
  RAM-console alias out, citing the conflict ("to avoid conflict with deviceTreeP high-alias
  at `0xc010c18c`"). That is the table the payload actually runs under, which is why
  `boot_args->deviceTreeP` works today.
- `xnu_arm_vm_init_full_pmap.c` kept the **RAM-console alias** and verified it, which meant
  the second megabyte of the image alias did not exist in the candidate table.

So under the candidate L1 — after the handoff, which is exactly when a real kernel would run
— dereferencing `boot_args->deviceTreeP` at `0xc010c18c` would have read the RAM console
buffer instead of the device tree.

The fix is `xnu_arm_vm_init_full_pmap.c`'s `STAGE90_RAM_CONSOLE_ALIAS_BASE`: `0xc0100000` →
`0xc0300000` (free in both tables). Both mappings now exist, both verifications run
unchanged, and the candidate table agrees with the identity table about the image alias.
Taking the other direction — deleting the RAM-console alias, as `mmu.c` did — would have been
the smaller diff but would have required rewriting `full_pmap`'s alias verification and its
satisfied-mask semantics, and the alias is genuinely useful to have.

This changes a hardware-validated code path, so it needs a run: the verification reads
`RAM_CONSOLE_SIG` through the new VA and compares it against the identity mapping, so a
mistake here surfaces as a `full_pmap` failure with `FAIL_RAM_CONSOLE` set, not as silence.
The change is confirmed present in the built image (the loaded constant is `0xc0300000` at
the verification site).

**F-AM2 — every section grants PL0 read/write.** AP[1:0] = `0b11` with AP[2] = 0 is full
access, not privileged-only. There is no userspace in a Stage payload so nothing exploits it
today, but this is not a kernel mapping's protection and Phase 1 should move to
`AP[1:0] = 0b01` (PL1 RW, PL0 no access).

**F-AM3 — sections are shareable, small pages are not.** S=1 on the sections, S=0 on the L2
small pages. On a single-core bring-up this is moot; for Normal memory on a multi-core part
it is wrong, and it is an inconsistency that will be read as intentional. Phase 1 makes RAM
shareable in both formats.

**F-AM4 — there is no Normal or cacheable mapping anywhere.** Confirmed by decoding both
constants: TEX/C/B is `000/0/0` in each. This is roadmap finding F2, and it is why
`LDREX`/`STREX` cannot be relied on today (`stages/stage90/exclusive_probe.c` measures what
they actually do here).

## ARMv7 short-descriptor encodings used above

TEX[2:0] / C / B select the memory type:

| TEX | C | B | Memory type |
| --- | --- | --- | --- |
| `000` | 0 | 0 | Strongly-ordered |
| `000` | 0 | 1 | Device |
| `001` | 0 | 0 | Normal, Non-cacheable |
| `001` | 1 | 0 | Normal, Write-through |
| `001` | 1 | 1 | Normal, Write-back, write-allocate |
| `1xx` | x | x | Normal, cached via the PRRR/NMRR attribute indirection |

S (section bit 16 / small-page bit 10) selects shareability for Normal memory; it is ignored
for Strongly-ordered and Device.

## Consistency per physical address

The constraint on any attribute change is not "keep everything Strongly-ordered" — it is that
**every virtual mapping of a given physical address must use the same memory type.** The
ARMv7 cache is physically indexed, so two VAs mapping one PA with different cacheability is
UNPREDICTABLE. Granularity may differ (a 1 MB section and a 4 KB page may both map the same
PA) as long as the attributes agree.

Listing every mapping by its PA, not its VA, is therefore what decides whether a change is
safe. The two tables are not identical — the identity table is missing the RAM direct map,
the RAM-console alias and the self-maps — but for the PAs they share they agree:

| PA region | What it is | VAs that map it | Classification |
| --- | --- | --- | --- |
| `0x00000000`–`0x001fffff` | payload image: code, `.data`, `.bss`, **and both page tables** | `0x00000000`, `0x80000000`–`0x800fffff` (L2, first MB only), `0xc0000000`, `0xc0100000`, plus each table's self-map | DRAM |
| `0x00200000`–`0x801fffff` | not mapped | — | — |
| `0x80200000`–`0x901fffff` | DRAM direct map (candidate table only) | `0x80200000`+ (VA = PA) | DRAM |
| `0xde500000`–`0xde6fffff` | Android ram_console window (ramoops) | `0xde500000`, `0xde600000`, `0xc0300000` | DRAM |
| `0x0fa00000` | MSM IMEM (restart reason) | `0x0fa00000`, `0x0fa00000` | **MMIO** |
| `0xf9000000` | GIC distributor + CPU interface + ARM timer | `0xf9000000`, `0xc0200000` | **MMIO** |
| `0xfc400000` | MSM8974 PS_HOLD | `0xfc400000` | **MMIO** |

DRAM and MMIO are disjoint PA sets, so the split is clean: no PA is wanted as both. That is
the whole reason a single switch can move DRAM to Normal without touching a single MMIO
mapping.

Three consequences that look like mistakes and are not:

1. **The page tables become Normal**, because they live in `.bss` inside PA 0–2 MB and must
   not disagree with the image mappings of that region. The requirement that matters is that
   they be *non-cacheable*, and Normal-Non-cacheable satisfies it — Strongly-ordered was
   never the point. (Caching them would be a separate question: with the D-cache on, table
   walks and table writes would need real cache maintenance. Not attempted here.)
2. **The payload's own code, data and stack also change memory type**, for the same reason.
   With caches off this is a weaker *ordering* model, not a caching one, and every shared
   access in the payload already sits behind a `dsb`.
3. **Device registers are classified by physical address, not by whoever is mapping them.**
   The TTBR0 roundtrip selftest builds a recovery table containing both DRAM and MMIO and
   installs it briefly; a GIC register mapped Normal in that window — with a timer interrupt
   able to arrive during it — is exactly the kind of thing that would be blamed on something
   else. `mmu.c`'s `ttbr_section_desc_for_pa()` therefore classifies IMEM, the GIC block and
   PS_HOLD explicitly.

## Phase 1a as implemented

`STAGE90_PMAP_ATTR_MODE` (`stages/stage90/stage90.h`):

| Mode | DRAM descriptors | Effect |
| --- | --- | --- |
| `SO_ONLY` (0) — **default** | `0x00010c02` / `0x00000012` | Byte-for-byte the behaviour of every stage so far. Verified: the SO_ONLY build contains **zero** Normal-NC descriptor values. |
| `NORMAL_NC` (1) | `0x00011c02` / `0x00000452` | DRAM becomes Normal, Non-cacheable, shareable. MMIO is unchanged. |

### How that "zero" was verified — and how it was got wrong first

The claim has been made repeatedly in this project's commits and notes, and the method
behind it was unsound twice, in opposite directions:

- `grep -c '#1106'` also matches `#11068` — a **substring false positive**.
- Counting only `movw rN, #imm` misses descriptors carried elsewhere. The section constant
  `0x00010c02` is *never* materialised whole: the compiler builds it as a low half
  (`#0x0c02`) plus `orr ..., #0x10000` for the S bit. So a `movw`-only scan reports **0 for
  a value that is present 53 times** — a **false negative**, and precisely the shape of
  error that could hide a real Normal descriptor.

Neither error changed the conclusion, which is the dangerous part: it was right by luck.

`tools/count_descriptors.py` replaces the ad-hoc greps, and its `--diff` mode is the sound
test — comparing the immediate multisets of two builds that differ only in
`STAGE90_PMAP_ATTR_MODE`, so the difference *is* the descriptor change whatever instruction
carries it. Run against the two builds:

| Immediate | `SO_ONLY` | `NORMAL_NC` |
| --- | --- | --- |
| `0x1c02` (Normal-NC section low half) | **0** | 30 |
| `0x0452` (Normal-NC page) | **0** | 1 |
| `0x0c02` (SO section low half) | 53 | 49 |
| `0x0012` (SO page) | 17 | 16 |

Both parts of the claim now hold soundly: the switch demonstrably rewrites 30–31 mapping
sites, and `SO_ONLY` demonstrably contains none of the Normal-NC values — including the
low half, which the old method could not see at all.

This is the smallest change that makes `LDREX`/`STREX` architecturally defined, and it is
deliberately *non-cacheable*: with no cache enabled it needs no cache maintenance anywhere,
no `ram_console` flush, and no change to how page-table writes become visible. Enabling the
I-cache and D-cache is a separate, later step, and it is the one that needs the
clean/invalidate discipline.

MMIO stays Strongly-ordered in both modes — GIC, timer, IMEM and PS_HOLD. Normal device
access is not guaranteed to be ordered with respect to anything, and the timer interrupt that
drives the dead-man runs through exactly those registers.

Build and gate it as its own run:

```bash
STAGE90_EXTRA_CFLAGS='-DSTAGE90_PMAP_ATTR_MODE=1' ./build.sh
./preflight_boot_check.sh --allow-attr-normal-nc

# and with the probe, to get the comparison that matters:
STAGE90_EXTRA_CFLAGS='-DSTAGE90_PMAP_ATTR_MODE=1 -DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
```

The comparison to make is `stage90_exclusive_probe_result` between the two modes:
`monitor_tracks` (T2 succeeds *and* T3 fails) and `exclusives_usable` must go from the
Strongly-ordered baseline to 1. If they do not, the attribute change is not doing what it was
supposed to and nothing built on atomics can be trusted yet.

### Rules the change must still obey

1. **Page tables stay non-cacheable** — satisfied by NORMAL_NC, and the reason caches are a
   separate step.
2. **`ram_console` must be cleaned before any reboot** once the D-cache is on. `log_puts()`
   writes to `RAM_CONSOLE_BASE` and issues `dsb sy; isb`; with caches off that is a complete
   guarantee and with the D-cache on it is not. The log would sit dirty across
   `platform_reboot()` and `/proc/last_kmsg` — the dead-man dump and every diagnostic in
   Phases 2–4 — would silently return stale or garbage text.
3. **Enable the I-cache before the D-cache,** with explicit clean/invalidate around the TTBR
   switch and after any code or segment copy.
4. **Compare against `exclusive_probe`,** as above.
5. **F-AM1 is fixed,** so the candidate table can become the handoff table as far as the image
   alias and the device-tree alias are concerned.

### The later step: caches on

Phase 1a is deliberately non-cacheable. The step after it turns the I-cache on and then the
D-cache, which is what makes the WBWA descriptors below meaningful — and it is the step that
introduces the cache-maintenance obligations. Those descriptors are recorded here because
they are the target, decoded with the same tool, not because 1a uses them:

| Descriptor | Value | Decoded |
| --- | --- | --- |
| Section, Normal WBWA, shareable | `0x0001140e` | section, executable, AP `0b01` (PL1-only), TEX=`001`, C=1, B=1, S=1 |
| Small page, Normal WBWA, shareable | `0x0000045e` | small page, executable, AF=1, AP `0b00`, TEX=`001`, C=1, B=1, S=1 |

Note AP: `0b01` on the section is PL1-only, fixing F-AM2 while the attribute change is being
made anyway.
