# 914 — The msm8974 platform expert (PE): what it must provide, and what we already have (2026-10-08)

913 §6 showed the Darwin-13 ARM core and the generic PE (`pexpert/arm/common`, `pexpert/gen`)
compile green under our toolchain. What is *missing* is the **board** half of the PE — the msm8974
device set the generic files dispatch to. This doc fixes the contract before writing a line of it,
so the PE is designed from measurements rather than modelled from HD2's QSD8250 bytes (which carry
no reuse license — `docs/reference/hd2-ios7-lab-analysis.md`).

## 1. The PE's obligation, read off the unlinked layer

Compiling Darwin-13 `osfmk/arm/*.c` alone leaves **175** undefined symbols in
`out/xnu_arm_obj_d13/undefined.txt`. The PE-related ones — the set the generic PE must satisfy — are
exactly:

```
PE_cpu_machine_init     PE_init_kprintf      PE_parse_boot_argn
pe_arm_init_timebase    pe_arm_get_timebase  serial_putc  serial_getc
ml_init_interrupt       ml_get_interrupts_enabled  ml_set_interrupts_enabled
clock_timebase_init
```

The other ~163 are the kernel proper (pmap helpers, `kprintf`, `printf`, libkern, bsd) and are a
*later* rung; the PE's job is the 11 above plus the `gPESocDispatch` table the generic PE fills.

## 2. Split: generic (compiles today) vs board (to write)

| part | file(s) | status |
|---|---|---|
| boot-args parse, DT walk, identify-machine, kprintf core, bringup | `pexpert/arm/common/{pe_bootargs,pe_identify_machine,pe_kprintf,pe_bringup,pe_support}.c`, `pexpert/gen/*` | **compiles** (11/11, 913 §6) |
| `serial_putc`/`serial_getc` | `pexpert/arm/common/pe_serial.c` | compiles, but calls **`gPESocDispatch.uart_putc`** — needs the board table |
| timebase | `pe_arm_init_timebase`/`get_timebase` → `gPESocDispatch` | board |
| interrupt controller | `ml_init_interrupt` → `gPESocDispatch.interrupt_init` | board |
| cpu machine init | `PE_cpu_machine_init` | board |

So the **board deliverable is one `msm8974` PE source** that (a) defines `SocDeviceDispatch
gPESocDispatch` with the msm8974 methods, (b) implements `PE_cpu_machine_init`, and (c) is selected
in place of HD2's `pe_qsd8250_leo.c` / the other `pe_*` board files. HD2's `pe_armsupport.c` and the
Samsung/BCM/TI/APQ `pe_*` files are **other boards'** PEs — examples of the *shape*, never compiled.

## 3. The handoff ABI is already satisfied (measured)

`boot_args` differs between the two generations and this matters more than it looks:

| field | Darwin-13 (`pexpert/arm/boot.h:66-78`) | 4570 (`:45-59`) |
|---|---|---|
| `Revision` … `CommandLine[256]` | present | present |
| `bootFlags` | **ABSENT** | present (Rev 2) |
| `memSizeActual` | **ABSENT** | present |

Our entry image builds the **4570 shape** (`src/stage90.h:106`, Rev 2, with `bootFlags`/
`memSizeActual`). Darwin-13 reads only through `CommandLine` — the leading fields are
**byte-for-byte identical** — so the extra trailing two words are inert and **the handoff is
compatible as-is**. No entry-image change is needed for the PE to boot. (The three `memSize`
definitions — `mi4-the-three-memsize-definitions` — still apply; this doc changes none of them.)

What the board PE adds on top of `boot_args` is **not** a new handoff: the interrupt dispatch is
already ours (`fleh_irq` in slot 6, `entry_irq.c`/`entry_timebase.h`), so `ml_init_interrupt` on the
board PE has to agree with *that* image, not invent a second controller.

## 4. The msm8974 values, already in the repo

The board PE must not re-derive these; it cites them:

| what | value | source in-repo |
|---|---|---|
| GIC distributor | `0xf9000000` | `src/entry/entry_gic.h:27` |
| GIC CPU interface | `0xf9002000` | `:28` |
| GIC timer PPI (per-CPU) | 18 / 19 | `:115-116` |
| GIC timer INTID used by the image | 20 | `:135` |
| virtual timer control | `CNTV_CTL` (`ENABLE`/`IMASK`/`ISTATUS`) | `entry_timebase.h:108-110` |
| USB (later rung) | `0xf9a55000` | `entry_usb.h:35` |

The CPU is Cortex-A15-class (`arm_target.sh` → `armv7-unknown-netbsd-eabi`, `-mcpu=cortex-a15`),
NEON/VFPv4 — which is *why* the entry image already sets `__ARM_L2CACHE_SIZE_LOG__=21` and reads
`FPSID`/`MVFR0` (`arm_init.c`'s HD2-*edited* path replaced by the generic one under
`XNU_ARM_EXCLUDE_HD2=1`).

`PE_parse_boot_argn` + the DT walk come *from the generic files*; the board adds only the addresses
the DT does not carry (GIC bases are in the DT's `interrupt-controller@f9000000` node — the board PE
may read them from `gPEClockFrequencyInfo`/the DT rather than hardcode, which is the point of feeding
the real device tree).

## 5. What this doc does NOT decide

- The **two-bank map** (3 GB region list) — that is the handoff's `memSize`/region list, a separate
  rung (§4.4 of 913), not a PE input.
- **AMFI** — separate (§4.5).
- Which board file the PE *build* picks — that is a `pexpert/conf/files.arm` / `MASTER` selection,
  same mechanism the harness already drives.

## 6. Next concrete step

Write `pexpert/arm/pe_msm8974.c` (our file, our license) implementing the §1 symbol set against the
§4 constants, and add it to the probe as an extra dir so it compiles green before any device work.
This is host-side and reversible; no press.

*Provenance: `out/xnu_arm_obj_d13/undefined.txt` (175 symbols, this session), `boot_args` diff read
from both trees' `pexpert/arm/boot.h`, in-repo constants cited above; device untouched.*

## 7. The board PE, written and compiling green (2026-10-08)

`src/platform/darwin13/pe_msm8974.c` is written — ours, our license, modelled on the *shape* of the
reusable `pe_socsupport.c`/`pe_bcm2835.c` only. It defines exactly the one symbol the generic PE
reaches for, `PE_init_SocSupport_stub()`, and fills `gPESocDispatch`. Three findings corrected the
§1/§2 model:

- **`PE_cpu_machine_init` is NOT a board symbol on ARM.** It is defined generically in
  `iokit/Kernel/IOCPU.cpp:271` (`void PE_cpu_machine_init(cpu_id_t, boolean_t)`) and declared in
  `pexpert/pexpert/pexpert.h:285`. `osfmk/arm/cpu.c:1055` calls it; no board file defines it. So the
  board deliverable is **the dispatch table alone** — smaller than §2's sketch.
- **`gPESocDispatch` is declared in the reusable file** (`pexpert/arm/common/pe_socsupport.c`, winocm
  APSL-2.0 — the same license the generic core carries), so the board only *assigns to* it; the
  object's `U gPESocDispatch` is satisfied by that file. Same for `PE_early_puts`/`PE_early_putc`
  (`pe_kprintf.c`) — the board PE's only two undefined symbols are both reusable-generic.
- **`files.arm` lists every board unconditionally**; the `#if defined(BOARD_CONFIG_$(MACHINE_CONFIG))`
  gate is the whole selection, and `-DBOARD_CONFIG_$(MACHINE_CONFIG)` comes from `MakeInc.def:245`
  against `SUPPORTED_ARM_MACHINE_CONFIGS` (`:123`). So adding msm8974 is a board-config name, not a
  new mechanism.

**The probe switches this forced** (`tools/build_xnu_arm_layer.sh`): `XNU_ARM_EXTRA_DIRS` now accepts
an **absolute path** (our source lives outside the tree — we never write into `external/`);
`XNU_ARM_EXTRA_DEFINES` carries the board config `-DBOARD_CONFIG_MSM8974=1`; `XNU_ARM_EXTRA_COMPONENT`
names the component when the path can't (absolute), defaulting to `pexpert`; and the HD2-exclusion
heuristic is **scoped to tree-relative dirs only**, because our own source legitimately *names* HD2
symbols in its citations and a comment-scan would skip it for mentioning what it does not use.

**The number:** the board PE compiles **1 of 1** against Darwin-13 with no errors, emitting
`T PE_init_SocSupport_stub` + `T PE_init_SocSupport_msm8974`, and its only undefineds are the two
reusable-generic symbols above. The `osfmk/arm` count is unchanged (**15 of 15** with
`XNU_ARM_EXCLUDE_HD2=1`) and the **4570 control is 32 of 32**, `make check` exit **0**.

*Provenance: `out/xnu_arm_obj_d13/pe_msm8974.o` (`arm-none-eabi-nm`), the compile above; device
untouched, host-side, reversible, no press.*