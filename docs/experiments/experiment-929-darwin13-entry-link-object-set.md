# 929 — the D13 entry link's object set, measured (2026-10-08)

928 made the D13 asm pool whole (33/0) and the closure walk converge (636 objects, 44-symbol supply
list). This rung runs the **actual** D13 entry link and measures what it needs.

## The measurement

    STAGE90_ENTRY_ARM_CHANGE=1 XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu \
      <the carried switch set> bash src/entry/build_entry.sh

The build gets all the way through the entry's own sources, the RAM-disk Mach-O check, the HFS blob
check and the AES known-answer tests, and stops at the **first link object**: 

    no out/xnu_asm_obj_d13/data.o - run ./tools/assemble_arm_layer.sh first

`data.o` is `osfmk/arm/data.s`, 4570's per-CPU data and boot stacks. **D13 has no `data.s`** — it
transplants a per-CPU model built on `cpu_data_ptr[]`/`cpu_data_master` (`osfmk/arm/cpu.c:835`), so
the object cannot be built and must not be required.

Because the build stops at the first `require`, the full set was taken by temporarily making
`require()` report-and-continue (`/tmp/build_entry_probe.sh`), then restoring `build_entry.sh`:

**59 link objects are in no `_d13` pool.** They split cleanly, and the split is the whole finding.

### (a) re-point — the symbols exist in a D13 object at another name (24)

| 4570 object | D13 definer | syms |
|---|---|---|
| `pexpert_arm_pe_kprintf.o` | `pexpert_arm_common_pe_kprintf.o` | 6/6 |
| `pexpert_arm_pe_bootargs.o` | `pexpert_arm_common_pe_bootargs.o` | 1/1 |
| `osfmk_arm_strlcpy.o` | `osfmk_device_subrs.o` (D13 moved it into `subrs.c`) | 1/1 |
| `osfmk_arm_strncpy.o` | `osfmk_device_subrs.o` | 1/1 |
| `osfmk_arm_cpu_common.o` | `osfmk_arm_cpu.o` | 18/33 |
| `osfmk_console_serial_console.o` | `osfmk_console_arm_serial_console.o` | 14/19 |
| `bsd_kern_kern_cs.o` | `bsd_kern_kern_cs.o` **at a D13 path** | 14/62 |
| `pexpert_arm_pe_init.o` | `pexpert_arm_common_pe_init.o` | 13/32 |
| `osfmk_arm_machine_task.o` | `osfmk_kern_task.o` | 4/5 |
| `osfmk_arm_io_map.o` | `osfmk_arm_…` | 2/2 |
| …and 14 more (partial) | | |

The `common/` split is the same shape 927 measured for `pe_init`/`pe_identify_machine`: D13 keeps the
**generic** PE under `pexpert/arm/common/` and the board symbols in its board PE
(`pexpert/arm/pe_qsd8250_leo.c`, the HD2 fork — the analogue of our MSM8974 board PE).

### (b) no D13 definer — 4570-era subsystems D13 does not have (35)

`osfmk_kern_{kpc,kpc_common,kpc_thread,kern_stackshot,telemetry,ltable,sfi,sched_multiq,waitq,
kern_monotonic,memset_s}.o`, `osfmk_ipc_{ipc_voucher,ipc_importance}.o`, `osfmk_kern_coalition.o`,
`osfmk_vm_vm_compressor.o` (163 syms), `osfmk_corpses_corpse.o`, `osfmk_atm_atm.o`,
`osfmk_bank_bank.o`, the split corecrypto `cchmac`/`ccsha1`/`ccdbrg`/`cc_clear`/`cc_cmp_safe` objects,
`osfmk_prng_random.o` (partial), `bsd_kern_kern_{kpc,ktrace}.o`, `iokit_Kernel_IOInterruptAccounting.o`,
`libkern_os_{internal,log}.o`, `pexpert_arm_pe_{consistent_debug,serial}.o`, `mac_mach.o` (partial).

**These are not missing objects to find — they are 4570's closure, hard-coded into
`build_entry.sh`'s `STAGE90_ENTRY_*_OBJ` list.** `build_entry.sh` in `REAL_ARM_INIT` mode links a
hand-picked set carried from 4570's `arm_init` closure (`build_entry.sh:1841-1860`: "the choice of
which objects is deliberate and small"). On D13 that set names 4570 objects that D13's `arm_init`
never reaches.

## The design

`build_entry.sh`'s object set is `one value, two definitions` at the object level — the same class as
926 (the pool) and 927 (the header path). The fix is the same rule: **follow the selected tree.**

But unlike 926/927 this cannot be a path substitution, because the *set* differs, not just the
names. Two steps:

1. **Derive the set, do not spell it.** Replace the hard-coded `STAGE90_ENTRY_*_OBJ` list with the
   closure of **D13's own `osfmk_arm_arm_init.o`** over the `_d13` pools (`tools/entry_closure.py`,
   whose walk 928 measured at 636 objects / 44-symbol supply list). The 44 are the entry's own stubs
   — and the project already defines most of them (`arm_init_cpu`, `vcputc`, `version`, `EntropyData`,
   `ExceptionVectorsTable`, `gPhysSize`, `fiqstack_top`, `fleh_*`, `initialize_screen` all appear in
   `src/entry/entry_stubs.c`). The walk's pool must **include `src/entry/entry_stubs.c`'s object** so
   the closure sees the stubs and does not re-report them.

2. **Make the require tree-aware.** For 4570 keep the exact list (byte-identical image); for D13 the
   require list is the derived one. `data.o` is dropped by the tree rule (D13 has no `data.s`), and
   the `common/` PE objects are named from D13's own build.

## Why this is the honest shape

927's Appendix already established the closure is **per-symbol, not a rename table**. This rung
measures the same thing one level up: at the **object** level, 24 of 59 re-point and 35 are 4570
closure that D13 does not share. A rename table would have silently linked 4570's `kpc`/`telemetry`/
`vm_compressor` into a D13 kernel and produced a link that tells you nothing about D13.

## What moved

Nothing yet — this rung is the measurement and the design. `build_entry.sh` was restored unchanged;
the arm in `out/stage90` was restored to `cb4e17f1…` (sha verified).