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

Defined in `stages/stage90/mmu.c:8` and copied in `stages/stage90/xnu_arm_vm_init_full_pmap.c:47`.

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

Defined in `stages/stage90/xnu_arm_vm_init_full_pmap.c:59`.

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
| RAM-console alias | `0xc0100000` | `RAM_CONSOLE_BASE` | 1 MB | section SO — **candidate table only** (F-AM1) |
| GIC alias | `0xc0200000` | `0xf9000000` | 1 MB | section SO |
| IMEM low alias | `0x0fa00000` | `0x0fa00000` | 1 MB | section SO |
| Candidate L1 self-map | `candidate_l1_base` | same | 1 MB | section SO *(candidate table only)* |
| L2 pool self-map | `l2_pool_base` | same | 1 MB | section SO *(candidate table only)* |

## Findings

**F-AM1 — the two L1 tables resolve the same VA conflict in opposite directions, and the
candidate table's choice breaks the device-tree high alias.** `0xc0100000` is claimed twice:
the high-alias extension maps `0xc0100000 -> 0x00100000`, and the RAM-console alias maps the
same VA to `RAM_CONSOLE_BASE`. One L1 slot, two callers:

- `mmu.c`'s `build_identity_table()` keeps the **second-MB alias** and comments the RAM-console
  alias out, citing the conflict ("to avoid conflict with deviceTreeP high-alias at
  `0xc010c18c`"). That is the table the payload actually runs under, which is why
  `boot_args->deviceTreeP` works today: PA `0x10c18c` has high alias `0xc010c18c`, inside
  `0xc0100000–0xc01fffff`.
- `xnu_arm_vm_init_full_pmap.c` keeps the **RAM-console alias** and verifies it (it reads
  `RAM_CONSOLE_SIG` through `0xc0100000` and compares against `RAM_CONSOLE_BASE`), which
  means the second megabyte of the high alias does not exist in the candidate table.

So under the candidate L1 — that is, after the handoff, which is exactly when a real kernel
would run — dereferencing `boot_args->deviceTreeP` at `0xc010c18c` reads the RAM console
buffer instead of the device tree. The Stage-owned handoff target does not touch the device
tree, and the handoff requires its jump target below PA `0x00100000`, so nothing on the
current path is affected. A real XNU would be: it reads the device tree from `boot_args`
early, and the failure would present as a garbage or malformed device tree, not as a mapping
fault. This has to be resolved before the candidate table becomes the live handoff table, and
resolving it means changing `full_pmap`'s own alias verification too — so it is a deliberate
change with a hardware run behind it, not a drive-by edit.

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

## Phase 1 target attributes

RAM, kernel text/data and the payload's own image become **Normal, Write-back
write-allocate, shareable**; every MMIO region stays **Strongly-ordered**. MMIO must not
become Normal — it has no cache to be coherent with, and Normal device access is not
guaranteed to be ordered with respect to the GIC, timer or PS_HOLD.

Proposed constants, decoded with the same tool:

| Descriptor | Value | Decoded |
| --- | --- | --- |
| Section, Normal WBWA, shareable, PL1-only | `0x0001140e` | section, executable, AP `0b01`, TEX=`001`, C=1, B=1, S=1 |
| Small page, Normal WBWA, shareable, PL1-only | `0x0000045e` | small page, executable, AF=1, AP `0b00`, TEX=`001`, C=1, B=1, S=1 |

### Rules the change must obey

1. **The page tables themselves must stay non-cacheable, or every table write must be
   cleaned.** The candidate L1 and L2 pool are self-mapped (last two rows of the table
   above). If that self-map becomes Normal-cacheable while the MMU walks it, a written entry
   can sit dirty in the D-cache and the MMU will read the stale value from memory. Keeping
   the tables Strongly-ordered is the simple, safe answer, and the tables are small.
2. **`ram_console` must be cleaned before any reboot.** `log_puts()` writes to
   `RAM_CONSOLE_BASE` and issues `dsb sy; isb`. That is a complete guarantee with caches off
   and is *not* one with the D-cache on: the log would sit dirty across
   `platform_reboot()` and `/proc/last_kmsg` — the dead-man dump and every diagnostic in
   Phases 2–4 — would silently return stale or garbage text. Fix the write path, and
   clean-and-invalidate the log region in the dead-man dump.
3. **Enable the I-cache before the D-cache,** and add explicit clean/invalidate around the
   TTBR switch and after any code or segment copy.
4. **Compare against `exclusive_probe`.** The Phase 1 `LDREX`/`STREX` result must beat the
   Strongly-ordered baseline recorded by `stage90_exclusive_probe_run()`. If
   `monitor_tracks` is still 0 afterwards, the attribute change is not doing what it was
   supposed to and nothing built on atomics can be trusted yet.
5. **Resolve F-AM1 first if the candidate table is to become the handoff table.** Adding
   attributes to a mapping whose contents are wrong just makes the wrong answer cacheable.
