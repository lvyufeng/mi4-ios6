# Experiment 99 — Stage90 Phase 2: the conforming `boot_args` on hardware, under the caches

Date: 2026-09-17
Commit under test: `948211c`, plus the gate change described below
Build switches: `STAGE90_PMAP_ATTR_MODE = NORMAL_WB`,
`STAGE90_CACHE_MODE = ICACHE_DCACHE`, `STAGE90_XNU_BOOT_ARGS = 1`;
`STAGE90_HANDOFF_MODE = HARD_SKIP`; both nets armed
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/kmsg-ba1.txt`

## What this run is for

`STAGE90_XNU_BOOT_ARGS` builds a `boot_args` that conforms to XNU's entry contract
(`osfmk/arm/start.s`) as a **second object**, alongside the ladder's own identity-based one,
which cannot be changed because the ladder's validation requires `physBase == 0x8000`. It was
implemented on 2026-09-16 and had been host-checked only — `tools/host_boot_args_check.sh`
executes the module for the host with `__stage90_image_end` passed by `--defsym` from the built
ELF — so what the hardware run adds is that the module's own invariants hold when the payload
executes it, with the linker's real symbols, under the Phase 1 caches.

The audit's standing note was that the `memSize` claim behind this object was the one thing it
could not bound by reading. That remains true and this run does not change it: 93 MB is
`CONFIG_PHYS_OFFSET`-to-first-`memblock-remove` from the cancro device tree, a source-derived
value, not something a payload run can validate. What *is* now hardware-exercised is the rest of
the contract holding on the real image.

## The result

```
xnu_ba_virt_base       =0x80000000     link-time virtual base, 1 MB aligned
xnu_ba_phys_base       =0x00000000     1 MB aligned - the unaligned 0x8000 problem dissolved
xnu_ba_mem_size        =0x05d00000     93 MB, ends at the first memblock hole, section-clean
xnu_ba_top_of_kernel_data=0x0011c000   16 KB aligned, above the image, inside the span
xnu_ba_image_base      =0x00008000
xnu_ba_image_end       =0x00119000     the real linker value
xnu_ba_table_bytes     =0x0000a000     room for the L1, an L2 and the trampoline
xnu_ba_device_tree_ptr =0x00110370     our Apple-format tree, in .bss
xnu_ba_device_tree_length=0x00007294
xnu_ba_machine_type    =0x00009074
xnu_ba_checks          =10             xnu_ba_failures = 0
stage90 xnu_boot_args: conforms to the XNU entry contract
```

and the whole payload still ends `kernel_entry returned success` with no failed line anywhere.

## A gate gap this run closed

`STAGE90_XNU_BOOT_ARGS` was in `build.sh`'s config dump but the gate had no case for it, so a
build with it on passed the gate silently. That contradicts the gate's stated job — refusing
what the caller has not explicitly allowed — and the omission was invisible precisely because
this switch is harmless: it is non-fatal by construction and changes no mapping or boot decision.

The gate now has a **later-phase probes** section reporting both `STAGE90_XNU_BOOT_ARGS` and
`STAGE90_XNU_MSM8974_SHIM`, and says why they need no flag (they are the next phases' work, not
preconditions) and what to read for each verdict (`xnu_ba_checks`/`xnu_ba_failures`,
`msm8974_shim_failures`). If either ever stops being inert, it needs a flag of its own — the
section says that too.

## Where Phase 2 stands

| Phase 2 bullet | Status |
| --- | --- |
| conforming `boot_args` | ✅ built, host-checked, and now run on hardware |
| Apple-format device tree | ✅ built and walked with XNU's own reader (`tools/host_dt_check.sh`) |
| a `topOfKernelData` region with bootstrap page tables | **partly** — the region is reserved and its invariants hold; the tables XNU will adopt are not built yet |
| validate with 4570's own readers on hardware | **partly** — the readers run on the host against our structures; the device validation is the payload's own checks |

The remaining Phase 2 work is the `topOfKernelData` bootstrap tables and running XNU's readers
*on the device* rather than on the host. Neither is blocked; both are the next steps. Phase 3's
shape is already known from the source reading — an MSM8974 replacement for
`pe_arm_init_interrupts`, because the stock one returns 0 on every non-Apple board class.
