# 923 — Darwin-13's board macro: a new board, `MSM8974_CANCRO` (2026-10-08)

922 delivered D13's C++ spine but left it 89/96: seven C++ files still failed, and four of them failed
on a single `#error` — the fork's `nokextd/IOS7NoKextd035.h`, which refuses to compile unless a board
is named. `IOService.cpp` is one of those four and is a base class the whole IOKit object graph
derives from, so the link cannot happen without a board. This rung establishes one, and with it the
C++ block compiles **96/96** — the entire spine, base classes included.

## The gate

Five translation units `#include` the fork's `nokextd/IOS7NoKextd035.h`:

    iokit/Kernel/IOService.cpp      iokit/Kernel/IOUserClient.cpp
    iokit/Kernel/IOStartIOKit.cpp   libkern/c++/OSKext.cpp   libsa/bootstrap.cpp

Its line 6–8 was `#if !defined(BOARD_CONFIG_QSD8250_LEO) / #error … requires the LEO compilation
target / #endif`. The fork is the HD2 port, so its board is `QSD8250_LEO`, and the gate is written as
a LEO test when it is really a **board** test (the header's whole job is to force `NO_KEXTD=1`, and
that is a property of any board that boots a fixed kernel, not of LEO).

## Why not `-DBOARD_CONFIG_QSD8250_LEO`

Reusing LEO compiles the gate — but LEO is also the switch for the fork's HD2 lab instrumentation, and
it is bolted onto **core** files:

- `osfmk/arm/trap.c:56` — `#include "ios7lab_fault_witness.h"` under LEO (the page-fault path);
- `osfmk/kern/thread_act.c:82` — HD2 preference-RPC headers under LEO;
- `pexpert/arm/pe_qsd8250_leo.c` — the HD2 board PE, a two-hundred-plus-line file whose body is
  wrapped in `#if defined(BOARD_CONFIG_QSD8250_LEO)` and which `#include "IOS7LeoMDPFault.h"`.

Measured: building D13 with `-DBOARD_CONFIG_QSD8250_LEO` **gains** three C failures (`trap.c`,
`thread_act.c`, `pe_qsd8250_leo.c`, all "file not found" on the HD2 headers) on top of the C++ win.
`trap.c` is load-bearing — carrying the wrong board's fault-path tracing into the machine is not a
build detail. So the board is a **new name**, `BOARD_CONFIG_MSM8974_CANCRO` (Xiaomi Mi 4, `cancro`),
which satisfies the gate without opening any `ios7*` LEO branch.

## The edits (staged — `external/` is re-provisionable)

`external/` is a gitignored checkout, so both edits are made by `tools/stage_d13_board.sh`, idempotent
and marked with the `MSM8974_CANCRO` sentinel, and re-derived by `tools/check_d13_board_staged.sh`
(in `make check`):

1. **`osfmk/arm/PlatformConfigs.h`** — a `BOARD_CONFIG_MSM8974_CANCRO` block mapping it to
   `__ARM_PROCESSOR_CLASS_CORTEX_A9__` + `__ARM_PROCESSOR_CLASS_QUALCOMM_A9__`, following the
   `BOARD_CONFIG_MSM8960_TOUCHPAD` precedent (the Qualcomm-Krait class), one generation on. That
   class is consumed at exactly one site, `osfmk/arm/locore.s:229` ("Enable automatic-clock gating" —
   a Cortex-A9 CP15 `c15` write, the right path for MSM8974's Krait 400).
2. **`../nokextd/IOS7NoKextd035.h`** — widen the guard from "must be LEO" to "must be a board"
   (`LEO or MSM8974_CANCRO`). This is the correct reading of the gate; LEO was the only board.

The build (`tools/build_xnu_arm_kernel.sh`) defines `-DBOARD_CONFIG_MSM8974_CANCRO` for D13 only,
gated on the same `osfmk/sys/types.h` filesystem test 922 used (D13 ships it, 4570 does not), so 4570
compiles with **no board macro at all** — which is what it did before, since 4570 is not a board port.

## The companion: `__STDC_LIMIT_MACROS`

Three more C++ files (`IOMedia.cpp`, `IODeviceTreeSupport.cpp`, `IOS7LeoSDCC2.cpp`) failed on
`UINT32_MAX`. It is defined in D13's `EXTERNAL_HEADERS/stdint.h:99`, gated at `:77` by
`#if (! defined(__cplusplus)) || defined(__STDC_LIMIT_MACROS)` — the pre-C++11 rule that a C++
translation unit must ask for the limit macros. The C path gets them for free; the C++ path needs the
macro. It is added to the C++ flag set under the same tree gate.

## Result

| | before 923 | after 923 |
|---|---|---|
| D13 C++ compiled | 89/96 | **96/96** |
| D13 C compiled | 601/606 | 601/606 (unchanged) |
| D13 objects | 690 | **697** |
| `make check` | 0 | **0** (now also runs the board-staged check) |

The whole C++ spine now exists, base classes included: `IOService`, `IOUserClient`, `OSKext`,
`IOStartIOKit`, and the ~90 others.

## 4570 neutrality

The board macro, the widened nokextd gate and `__STDC_LIMIT_MACROS` are all gated on the D13-only
filesystem test, so on 4570 the flag sets are empty and the build is the build it was. Re-measured:
**osfmk 214/214 objects byte-identical**, libkern+iokit C++ **62/62 byte-identical**, manifest
**byte-identical**.

## The walls the link still meets (named, not closed)

The spine compiles; five **C** files remain, and they are the link's next prerequisites:

1. **`osfmk/arm/locore.s` and five siblings (the `.s` dialect).** `asm.h:204` is
   `#ifdef _ARM_ARCH_7 / #define SLIDABLE 1`, and `_ARM_ARCH_7` is set by `arm/arch.h:15` from
   clang's own `__ARM_ARCH_7A__` — so `SLIDABLE` is **forced to 1 by the tree**, overriding this
   project's `-DSLIDABLE=0` exception, and the SLIDABLE `LOAD_ADDR_GEN_DEF` emits Darwin Mach-O
   **non-lazy pointers** (`.section __DATA,__nl_symbol_ptr,non_lazy_symbol_pointers … .indirect_symbol`)
   that clang's **ELF** assembler has no equivalent for (`expected string in directive`). `bcopyinout.s`,
   `hw_lock.s`, `machine_routines_asm.s`, `traps_lo.s` include the same macro; `cswitch.s` is the
   idle-stack patch. This is the last substantial piece of the assembly layer and it is a **tree
   edit** (the fork assumes the Darwin assembler; our ELF path needs the non-slidable branch).
2. **`libkern/kxld/` (4 files)** — `kxld_object.h:60` fails `expected ')'`; the kext loader, whose
   `NO_KEXTD` path the board gate selects but whose files still compile.
3. **`bsd/nfs/nfs_node.c`** — `strncasecmp` undeclared (a `<strings.h>` not reached under this config).

## Verification

| check | result |
|---|---|
| D13 C++ block | 96/96 |
| D13 C block | 601/606 (the 5 above) |
| `tools/stage_d13_board.sh` | idempotent; reproduction of both edits diff-clean |
| `tools/check_d13_board_staged.sh` | ok (in `make check`) |
| 4570 osfmk objects | 214/214 byte-identical |
| 4570 libkern+iokit C++ | 62/62 byte-identical |
| 4570 manifest | byte-identical |
| `make check` | 0 |

## What this does not do

It does not link the D13 image, does not assemble the six D13 `.s` dialect files, and does not fix the
three C walls above. It establishes the board macro D13 needs — the last *configuration* wall between
the compiling C++ spine and a link.