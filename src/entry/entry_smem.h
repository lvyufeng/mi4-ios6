/*
 * 911c: the Mi 4's RAM bank layout, measured from the Qualcomm SMEM RAM-partition table.
 *
 * ------------------------------------------------------------------------------------------------
 * Why a header, and why the numbers are transcribed
 * ------------------------------------------------------------------------------------------------
 *
 * The same reason `entry_usb.h` and `entry_gic.h` are ones: this image cannot include the device's
 * own Linux headers, so every number `entry_smem.c` reads is transcribed, and a transcribed offset
 * is this project's most repeated defect ([[mi4-one-value-two-definitions]]). One definition, in a
 * file about the thing it describes, and the value's *OWNER* named beside it - here the Android
 * checkout at `external/android_kernel_xiaomi_cancro`, whose `arch/arm/mach-msm/{smem_private.h,
 * memory_topology.c}` and `include/mach/msm_iomap.h` spell every offset below.
 *
 * ------------------------------------------------------------------------------------------------
 * What this rung is: a MEASUREMENT, and why it must measure
 * ------------------------------------------------------------------------------------------------
 *
 * The goal's newest clause is that XNU recognize the Mi 4's **3 GB** of RAM. A repo-wide sweep
 * (911c's design, doc section 10) found the repo holds **no low-bank base or size at all** - only
 * the high bank `[0x80000000, 0xde700000)` (`src/stage90.h:29-30`). Every "low bank" anywhere in
 * the tree is an *inference* from "3 GB minus 1.5 GB", and the one line that looks like a capture
 * (`80000000-de6fffff : System RAM`, `docs/history/stage0-payload-plan.md:53`) is prose. So the
 * 3 GB half cannot be *assumed*; it has to be **read off the device**. The authoritative in-repo
 * source is the Qualcomm SMEM RAM-partition table, the very table the Android kernel's
 * `meminfo_init` reads to build its memory-bank list.
 *
 * **This file writes NOTHING.** It maps the SMEM window, walks the heap table of contents, validates
 * a candidate partition table by its magic, and publishes the banks it finds as `xnu_live_smem_*`
 * keys. There is no hazard the way a USB write would be: a fault would stop the run at the probe,
 * which is a reading, not a brick.
 */
#ifndef STAGE90_ENTRY_SMEM_H
#define STAGE90_ENTRY_SMEM_H

/*
 * **The SMEM window, from the device tree and the device's own checkout.** The *physical* base is
 * `0x0fa00000` (the `smem@fa00000` DT node; Qualcomm's `MSM8974_MSM_SHARED_RAM_PHYS`), size 2 MB
 * (`msm_iomap.h:120-122`: `MSM_SHARED_RAM_SIZE SZ_2M`). **This is the same address range the
 * epilogue's `RESTART_REASON` store targets** (`src/stage90.h`'s `MSM_IMEM_BASE_PHYS 0x0fa00000` +
 * `0x65c` = `0x0fa0065c`), whose store faults today (`far=0x0fa0065c`, fsr `0x805`,
 * `docs/experiments/experiment-745:76`) - so the *low identity* mapping below serves two purposes
 * at once: this probe reads SMEM through it, and it un-faults the epilogue's store. See the two
 * mappings in `entry_smem.c`.
 */
#define STAGE90_SMEM_PHYS_BASE   0x0fa00000u
#define STAGE90_SMEM_SIZE        0x00200000u

/*
 * **The high alias to reach the low PA through.** The entry image installs its MMIO sections with
 * `entry_mmio_section(va, pa, ...)`, which is `entry_section_install(va >> 20, pa, ...)` - a 1 MB
 * section keyed by the top 12 bits of the virtual address and **with no VA guard** (911c design).
 * The handed-off kernel runs `arm_vm_init.c`'s tables with `TTBCR.N = 2` and, on this device,
 * `TTBR0 == TTBR1` (the live channel's `xnu_live_ttbr0 == xnu_live_ttbr1`), so a mapping installed
 * for ANY 1 MB-aligned VA resolves regardless of which side of `0x40000000` it falls on. This alias
 * sits in the hole between the two banks (`0xa0000000..0xdfffffff`), far from the RAM console
 * (`0xde500000`) and the high bank, so the alias VA can never be confused with the bank it describes
 * (the "two numbers that differ by one arithmetic step" rule, [[mi4-board-constants-from-device-dt]]).
 */
#define STAGE90_SMEM_ALIAS_BASE  0xe0000000u

/*
 * **The SMEM heap, from `smem_private.h:22-49`.** SMEM begins with four `smem_proc_comm` words
 * (4 x 16 B), then `version[32]` (128 B), so `heap_info` is at offset 4*16 + 32*4 = 0xD0 and the
 * heap table of contents begins immediately after heap_info's four words at 0xE0. Each TOC entry is
 * four words `{allocated, offset, size, reserved}` (16 B). `smem_alloc` does not carry an index: it
 * scans the TOC for an allocated entry of the requested size, and the entry's `offset` is relative to
 * the SMEM base. So this probe **scans the TOC and validates every allocated entry by magic** rather
 * than computing a deep-enum id - which is also what makes it robust to a base that is not SMEM at
 * all (the walk simply finds no magic and reports `_ptable_found = 0`).
 */
#define STAGE90_SMEM_HEAP_INFO_OFF   0x000000d0u
#define STAGE90_SMEM_HEAP_TOC_OFF    0x000000e0u
#define STAGE90_SMEM_HEAP_TOC_N      512u
#define STAGE90_SMEM_TOC_STRIDE      16u
#define STAGE90_SMEM_TOC_ALLOCATED   0u
#define STAGE90_SMEM_TOC_OFFSET      4u
#define STAGE90_SMEM_TOC_SIZE        8u

/* A TOC entry's `offset` is bounded so the walk never dereferences an offset past the window. A
 * partition table is ~1.9 KB (`0x10 + 32*56 < 0x800`), so an offset within the first megabyte of
 * SMEM is the only place it can be; a larger one is a corrupt or non-SMEM entry and is skipped, not
 * followed ([[mi4-a-device-address-can-be-right-and-undereferenceable]]). */
#define STAGE90_SMEM_OFF_MAX         0x00100000u

/*
 * **The RAM-partition table, from `memory_topology.c:28-58`.** Magic pair `_SMEM_RAM_PTABLE_MAGIC_1
 * 0x9DA5E0A8` / `_SMEM_RAM_PTABLE_MAGIC_2 0xAF9EC4E2` at the table's offset 0; `version` at 4;
 * `reserved1` at 8; `len` (the number of *valid* partitions) at 0x0c; `parts[]` at 0x10.
 *
 * `sizeof(struct smem_ram_ptn)` is **56**, NOT 48: the struct is `__packed` and holds `char name[16]`
 * plus `start, size, attr, category, domain, type` (6 x 4 = 24) plus `reserved2..reserved5`
 * (4 x 4 = 16) = 16 + 24 + 16 = 56. A 48-byte stride would walk into the middle of every second
 * partition and read garbage `start`/`size` pairs - the exact "a stand-in can be the right size and
 * the wrong value" defect ([[mi4-stand-in-size-is-not-value]]), so the stride is spelled from the
 * struct's own field list and `build_entry.sh` refuses a build whose stride is not 56.
 */
#define STAGE90_SMEM_PTABLE_MAGIC0   0x9da5e0a8u
#define STAGE90_SMEM_PTABLE_MAGIC1   0xaf9ec4e2u
#define STAGE90_SMEM_PTABLE_LEN_OFF  0x0000000cu
#define STAGE90_SMEM_PTABLE_PARTS_OFF 0x00000010u
#define STAGE90_SMEM_PTABLE_PART_N   32u
#define STAGE90_SMEM_PART_STRIDE     56u
#define STAGE90_SMEM_PART_NAME_OFF   0u
#define STAGE90_SMEM_PART_START_OFF  16u
#define STAGE90_SMEM_PART_SIZE_OFF   20u
#define STAGE90_SMEM_PART_ATTR_OFF   24u
#define STAGE90_SMEM_PART_CATEGORY_OFF 28u
#define STAGE90_SMEM_PART_DOMAIN_OFF 32u
#define STAGE90_SMEM_PART_TYPE_OFF   36u
#define STAGE90_SMEM_PART_NAME_MAX   16u

/*
 * **Which partitions are a physical RAM bank.** `memory_topology.c`'s `meminfo_init(type,
 * min_bank_size)` keeps a partition when `parts[i].type == type && parts[i].size >= min_bank_size`,
 * and every board calls it as `meminfo_init(SYS_MEMORY, SZ_256M)` (`board-8064.c:3702` and peers).
 * `SYS_MEMORY` is 1 (`msm_memtypes.h:44-49`); so a bank is a partition of `type == 1` at least 256 MB
 * large. This is the *same* predicate the Android kernel uses, cited rather than guessed - a bank
 * filter of my own invention would be a claim, not the device's rule.
 */
#define STAGE90_SMEM_SYS_MEMORY      1u
#define STAGE90_SMEM_MIN_BANK_SIZE   0x10000000u

/*
 * How many banks the probe will publish, and the sum. The Mi 4 has two (a low and a high bank); four
 * is headroom for a device whose table splits them differently, and the *sum* of every bank is the
 * device's real total - the number the goal's "3 GB" is about. Publishing the count and the sum
 * beside the per-bank cells means a reader never has to add them up in the log.
 */
#define STAGE90_SMEM_MAX_BANKS       4u

/*
 * The probe, called once from the first wrapped idle the handed-off kernel makes - the same site the
 * USB probes use (`__wrap_Idle_load_context`), for the same reason: the live channel is only a
 * console write's own proof (`g_live_state`), which is `entry_mmio_section`'s first refusal, so a
 * device read is meaningful only after that proof. **It is idempotent (`g_smem_probed`)** and returns
 * without dereferencing the alias if the section it needs could not be installed.
 *
 * When `STAGE90_XNU_SMEM_PROBE` is 0 the whole body compiles to a `return`, so the call site in
 * `entry_trace.c` is unconditional and the ON and OFF arms are one source file rather than two.
 */
void entry_smem_probe(void);

/*
 * The readings a *second* reader has to be able to compare, published as named symbols (494's rule):
 * the ptable's found/not-found verdict and the device's total bytes are the two numbers later stages
 * (the 911e region-list port, and the payload's own memory sizing) must agree with. A live key cannot
 * be the right-hand side of that comparison, and a constant typed into the payload would be a claim
 * rather than a reading. Defined unconditionally so a non-SMEM build carries zeros, not an undefined
 * symbol (the `g_stage90_usb_id_cap` pattern in `entry_usb.h`).
 */
extern uint32_t g_stage90_smem_ptable_found;
extern uint32_t g_stage90_smem_total_bytes;
extern uint32_t g_stage90_smem_banks;

#endif /* STAGE90_ENTRY_SMEM_H */