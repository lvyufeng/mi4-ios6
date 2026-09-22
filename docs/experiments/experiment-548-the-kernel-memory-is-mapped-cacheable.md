# 548: the kernel's memory is mapped cacheable, so 546's one checkable premise holds

546 §6 listed exactly one premise that was a property of the pmap rather than of the function - *the idle
stack is mapped cacheable* - and 547's whole mechanism rests on it: if the stack were mapped
strongly-ordered or non-cacheable, then a stale valid line for it could not exist and the push/pop story
would be impossible by construction. This step checks it, host-side, with no device and no build.

**The finding: it holds, and by two independent statements.** XNU maps kernel memory with memory
attribute index **0**; Apple's own name for index 0 is *"cache enabled, buffer enabled"*; and the remap
table XNU installs resolves that index to a **cacheable** normal-memory attribute under either of the two
readings of the `PRRR` field that govern it. And the live `SCTLR` captured during the boot has **TEX remap
enabled**, which is the switch that makes the index mean anything at all.

## 1. Which attribute index XNU uses for kernel memory

| where | what |
| --- | --- |
| `osfmk/arm/arm_vm_init.c:175-176` | the kernel's bootstrap mappings: `ARM_PTE_AF \| ARM_PTE_SH \| ARM_PTE_TYPE \| ARM_PTE_ATTRINDX(CACHE_ATTRINDX_DEFAULT)` |
| `osfmk/arm/pmap.c:2641`, `:5416`, `:6232`, `:6240` | the kernel page templates, same `CACHE_ATTRINDX_DEFAULT` |
| `osfmk/arm/proc_reg.h:630-636` | `CACHE_ATTRINDX_WRITEBACK 0x0 /* cache enabled, buffer enabled */` … `CACHE_ATTRINDX_DEFAULT  CACHE_ATTRINDX_WRITEBACK` |

So the index is **0**, and Apple's comment on index 0 is "cache enabled". Index 3
(`CACHE_ATTRINDX_DISABLE`) is the "no cache" one and it is *not* what kernel memory uses.

## 2. What index 0 resolves to, and why the answer is cacheable under either reading

The remap tables are constants in the same header (`proc_reg.h:530`, `:550`, `ARMA7` branch):

```
#define  PRRR_SETUP   (0x1F08022A)     /* TR0 = 0b10 */
#define  NMRR_SETUP   (0x01210121)     /* IR0 = 0b01 = NMRR_WRITEBACK */
#define  NMRR_WRITEBACK  0x1  /* Write-Back, Write-Allocate */
```

The `PRRR.TRn` field decides whether the attribute comes from `NMRR` or is one of the three fixed normal
types, and the value present is `TR0 = 0b10`:

| reading of `TR0 = 0b10` | the attribute for index 0 | cacheable? |
| --- | --- | --- |
| "use `NMRR`" (the remap-applies case) | `NMRR` IR0 = `NMRR_WRITEBACK` = **Write-Back, Write-Allocate** | **yes** |
| "a fixed normal type" | **Write-Through, no Write-Allocate** | **yes** |

**Both candidate meanings of the field value that is actually there give a cached attribute**, so the
premise does not depend on which one is right - which is why the ambiguity is recorded and does not block
the conclusion. (Which of the two it is *does* decide write-back versus write-through, and that is
residual; see §5 for why the mechanism survives either.)

## 3. And TEX remap is on in the state the pop runs in, which is not a given

The index only selects a cache attribute if `SCTLR.TRE` is set - otherwise those descriptor bits are read
as `TEX`/`C`/`B` and index 0 would be `TEX=0, C=0, B=0`, i.e. strongly-ordered. The captured log carries
the register, and there are **two regimes** in it:

| capture | value | M | C | I | **TRE** | AFE |
| --- | --- | --- | --- | --- | --- | --- |
| `xnu_entry_stub_sctlr_after` (payload era) | `0x00c5487b` | 1 | 0 | 0 | **0** | 0 |
| `xnu_live_sctlr` | `0x30c5787d` | 1 | 1 | 1 | **1** | 1 |
| `xnu_live_pce_after_sctlr` (after the idle enter, i.e. **inside the window**) | `0x30c57879` | 1 | **0** | 1 | **1** | 1 |

Read off the bits, not from memory (bit 0 `M`, bit 2 `C`, bit 12 `I`, bit 28 `TRE`, bit 29 `AFE`). The
payload-era value has `TRE = 0`; XNU's own `arm_vm_init` regime has `TRE = 1`, and the reading taken
**after `platform_cache_idle_enter`** - the state the exit's push and pop execute in - has `TRE = 1`. **So
the attributes in force at the frontier are XNU's, and TEX remap is on in them.** That also settles, in
passing, the direction 546 §6 flagged: the payload's `STAGE90_PMAP_ATTR_MODE_SO_ONLY`
(`out/stage90/stage90-build-config.txt:11`) describes the payload's own bootstrap table, and the readings
show the register state changed regime before the jump.

## 4. What this closes, and what it does not

**Closes:** 546 §6's first bullet. The idle stack's region is mapped with a cacheable normal-memory
attribute, so a *valid stale* line for it can exist, which is the precondition for 546 §3's mechanism and
for 547's reading of the dump ("the load succeeded and the memory gave back the wrong value"). The
mechanism is not impossible by construction.

**Does not close:** everything else those two documents left open - which level holds the line, why the
enable-on cells produce no log, and whether the death is a cache matter at all. And it is a statement
about the *attribute of the mapping*, not about the *contents* of any cache: it says a stale line is
possible, never that one was there.

**One consequence worth naming, because it is the interesting half of §1.** The index that kernel memory
uses is the *same* index the kernel's own `pmap` uses for its page templates, so this is not a special
case of the stack: **every kernel mapping in this boot is cacheable**, which is what makes the window's
missing L2 maintenance (546 §2) matter at all. If kernel memory had been mapped non-cacheable, none of the
phase's cache arms could have had any effect and the four-cell ledger would have a much simpler
explanation; it is not, so the ledger's spread needs the mechanism rather than the attributes.

## 5. Residual, stated

- **Write-back or write-through for index 0** - depends on the meaning of `PRRR.TRn = 0b10`, which is not
  resolved here. It does not change the reading: with `SCTLR.C = 0` the push's stores bypass the cache
  either way and leave the cached copy stale, so 546's step 2 holds under both. (It would matter to a
  *repair* only in that a write-through region's stale L1 line is clean and an invalidate alone suffices;
  the operation 546 §4 specifies is a clean-and-invalidate, which covers both.)
- **The bit positions of the three-bit index under `TRE = 1`.** `ARM_PTE_ATTRINDX` (`proc_reg.h:932-933`)
  places index bits [1:0] at `ARM_PTE_CBSHIFT` and bit 2 at `ARM_PTE_TEX0SHIFT` (6), which is not the
  naive `TEX[2:0]` placement; the index *value* used is 0 either way, and both fields considered above are
  cacheable, so the conclusion does not turn on it. Recorded rather than papered over, because
  [[mi4-one-value-two-definitions]] is the class this project pays for and a bit-position convention is
  exactly a value with two spellings.
- **Whether the idle thread's stack is in the kernel's wired mapping set** as assumed - the chain above is
  from the kernel's own attribute templates, not from reading the descriptor for that stack address out of
  a live table.

## 6. Safety

No device action in this step: `grep` and one `python3` bit-decode over the captured log in `/tmp`, and
over `external/xnu-4570.1.46/osfmk/arm/`. Nothing written outside `docs/` and the memory files, no build
run, no file under `out/` touched, `flash` not used, frozen pair untouched (`1daaf44e624563694e…` /
`f202f2465886aba6…`). The device is off the bus and owes a power press before 533 can run.
