# 931 — the D13 entry link's 280-object set, classified (2026-10-08)

929 measured that the D13 link stops at `data.o` and that the real-arm-init object list is 4570's
`arm_init` closure. This rung names every object in that list and classifies each against D13.

## The measurement

Parse every `*_OBJ=${STAGE90_ENTRY_…:-$XNU_*_OBJ_OUT/<rel>}` in `build_entry.sh` (280 named objects),
then test each against the `_d13` pool by path, and — for the absent ones — against a
`symbol -> defining D13 object` index over the pool.

| | count |
|---|---|
| named objects | **280** |
| present in `_d13` pool at that path | 197 |
| **absent from `_d13`** | **83** |

`data.o` alone is 4570's `data.s` (per-CPU data + boot stacks); **D13 has no `data.s`** (its model is
`cpu_data_ptr[]`/`cpu_data_master`, `osfmk/arm/cpu.c:835`). The other 82 never stop the build — they
are 4570 objects D13's `arm_init` never reaches, carried in the hand-picked list.

Wait: `bcopy.o`/`bzero.o` — which *are* needed — are **present** in `xnu_asm_obj_d13`, so the
assemble step is not the blocker for them; the count of 83 is the whole set.

## The classification of the 83

### clean re-point — D13 defines the symbols in one other object at another name (13)

| 4570 object | D13 object | syms |
|---|---|---|
| `osfmk_console_serial_console.o` | `osfmk_console_arm_serial_console.o` | 14/19 |
| `pexpert_arm_pe_kprintf.o` | `pexpert_arm_common_pe_kprintf.o` | 6/6 |
| `pexpert_arm_pe_bootargs.o` | `pexpert_arm_common_pe_bootargs.o` | 1/1 |
| `osfmk_arm_strlcpy.o` | **`osfmk_device_subrs.o`** | 1/1 |
| `osfmk_arm_strncpy.o` | **`osfmk_device_subrs.o`** | 1/1 |
| `osfmk_arm_io_map.o` | `osfmk_arm_pmap.o` | 2/2 |
| `osfmk_prng_*` (7 files: yarrow, smf, sha1mod, comp, yarrowUtils, fips_sha1, prng) | **`bsd_dev_random_*`** — D13 moved the whole PRNG/Yarrow tree into `bsd/dev/random/` | 1–8 each |

D13 relocated large subtrees: the Yarrow PRNG to `bsd/dev/random/`, `strlcpy`/`strncpy` into
`osfmk/device/subrs.c`, the PE generics to `pexpert/arm/common/`.

### partial — the object splits (18)

| 4570 object | D13 covers | syms |
|---|---|---|
| `osfmk_arm_cpu_common.o` | `osfmk_arm_cpu.o` | 15/33 |
| `bsd_kern_pthread_shims.o` | `bsd_kern_pthread_support.o` | 16/39 |
| `bsd_kern_kern_cs.o` | `bsd_kern_kern_proc.o` | 13/62 |
| `pexpert_arm_pe_init.o` | `pexpert_arm_common_pe_init.o` | 10/32 |
| `caches_asm.o` | `cache.o` | 3/19 |
| `osfmk_arm_caches.o` | `cache.o` | 3/17 |
| `bsd_netinet_tcp_cc.o` | `bsd_netinet_tcp_subr.o` | 5/27 |
| `data.o` | `locore.o` | 2/8 |
| …10 more | | |

The residual symbols in each are the D13 renames among the names 928's underscore fix does not reach
(different C names, not an underscore).

### no D13 definer — 4570-era subsystems D13 does not have (52)

**Every one of these sources is absent from the whole D13 tree** (`coalition.c`, `telemetry.c`,
`kpc_common.c`/`kpc_thread.c`/`kern_kpc.c`, `waitq.c`, `ltable.c`, `sfi.c`, `corpses.c`, `atm.c`,
`bank.c`, `work_interval.c`, `memset_s.c`, `sys_reason.c`, `kern_overrides.c`, `sys_ulock.c`,
`proc_uuid_policy.c`, `subr_eventhandler.c`, `necp.c`, `content_filter.c`, `mptcp_subr.c`,
`tcp_cc.c`, `os_log.c`, the `corecrypto` HMAC/SHA1 split objects, the `prng/` tree as separate
objects, `mac_mach.c`, …). The test that settles each — *is the symbol defined anywhere in D13's
source?* — is negative for the whole set (`memset_s`, `waitq_init`, `ipc_voucher_alloc`,
`coalition_create`, … none appear in any D13 `.c`).

These are **not missing objects to find**: D13 is iOS 7, which predates KPC, the telemetry framework,
`coalition`/`atm`/`bank`, the corecrypto contraction, and the MPTCP/NECP stack. D13's history simply
does not contain them.

## The finding: the SET must be re-derived, not the names

Twelve absent objects are simply **absent from D13** — the `PRNG/Yarrow`, `caches_asm`/`caches`,
`strlen`/`strncmp`/`strnlen`, `data.s` families that iOS 7 did not have. A per-name re-point therefore
cannot work two ways:

1. **Two 4570 objects may map to one D13 object.** `strlcpy.o` and `strncpy.o` both re-point to
   `osfmk_device_subrs.o`; the `PRNG` objects all re-point into `bsd_dev_random_*`. The 4570 list
   names each separately, so re-pointing by name would link the same D13 object twice — a
   duplicate-definition link error.
2. **Fifty-two have no D13 home at all** and must be supplied as stubs, but only for the symbols
   D13's `arm_init` closure actually references — supplying all 52 would add ~600 unused stubs.

This is 927's "per-symbol, not a rename table" finding at the **object** level, and it makes the
implementation choice forced: **derive the set from D13's own `arm_init` closure over the `_d13`
pools** (`tools/entry_closure.py`), then map each name in that derived closure to an existing D13
pool object (928 measured 636 objects / 44-symbol supply list). Flagging — i.e. correcting 4570's list
name by name — cannot express "one object covers two 4570 slots" or "no D13 home", which is exactly
why 929 named the hard-coded list a one-value-two-definitions defect at the set level.

## What moved

Nothing — this rung is the classification. `build_entry.sh` unchanged; the arm in `out/stage90` is
untouched (`cb4e17f1…`).