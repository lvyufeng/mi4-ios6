# 922 — Darwin-13's C++ spine: the option half of the per-component MASTER split (2026-10-08)

921c named the wall: `out/xnu_kernel_obj_d13/` had **4 `iokit/` files and no `libkern/c++/`** — no
`OSObject`, `IOService`, `IORegistryEntry`, `OSMetaClass` — because D13's ARM `RELEASE` attribute list
omits `iokitcpp`/`libkerncpp`. That reading was right about the *symptom* and wrong about the
*mechanism*. The attributes are not missing from the tree: **`iokit/conf/MASTER:62` declares
`options IOKITCPP … # <iokitcpp>` and `libkern/conf/MASTER:59` declares `LIBKERNCPP`.** The manifest
selector simply never opened those files. This rung fixes the selector, and the C++ spine compiles.

## The defect: the option set was read from one MASTER dir, not the tree's whole set

`tools/xnu_config/list_sources.py:expand_options()` runs `make_defines.sh <config>` with **no
`XNU_MASTER_DIR`**, so `select_master.sh` defaults to one directory — for D13, `osfmk/conf`. D13's
`osfmk/conf/MASTER.arm:12` writes the RELEASE attribute list, but the *options those attributes select*
are declared per component. `IOKITCPP` and `LIBKERNCPP` are declared in `iokit/conf` and
`libkern/conf` and nowhere else, so the expansion of `osfmk/conf` produced neither, `optional
iokitcpp`/`optional libkerncpp` never matched, and 74 C++ manifest rows were dropped:

    iokit/conf/files:  IORegistryEntry.cpp IOService.cpp IOServicePM.cpp … optional iokitcpp   (52 rows)
    libkern/conf/files: OSMetaClass.cpp OSSet.cpp OSString.cpp …           optional libkerncpp (22 rows)

**This is 920's defect, one level up.** 920 made the *device* table the union over every MASTER dir
(`devices.configuration_lines` → `_master_dirs()`), because D13 splits `pseudo-device` declarations the
same way. The *option* set was left reading one dir. Apple's `config` builds **one global option set**
from every MASTER in its search path, so the answer is the union — exactly the rule 920 already
applied to devices.

The tell was on the fragment: a test configuration `D13CPP = [ RELEASE iokitcpp libkerncpp ]` in a
`.local` fragment parsed into the declaration stream (`select_master.sh D13CPP` printed
`D13CPP# RELEASE iokitcpp libkerncpp`), but `make_defines.sh D13CPP` emitted **no `-DIOKITCPP`** —
because the two attribute names were declared nowhere in `osfmk/conf`. With `XNU_MASTER_DIR=iokit/conf`
the same command emits `-DIOKITCPP=1`. One directory was the whole difference.

## The fix: `expand_options` unions over the tree's MASTER dirs

`devices.py`: `_master_dirs()` was split into a thin env wrapper and `master_dirs(root)` — the rule
lives in one place, and a caller that carries its tree as an **argument** (`list_sources.py --xnu`,
experiment 916) does not have to export `XNU_TREE` to ask a path question.

`list_sources.py:expand_options()` now loops `devices.master_dirs(xnu)`, expanding the config once per
directory and taking the union of the `-D` names. On a one-MASTER-dir tree (4570) the loop runs once
and the result is the single expansion it always was, so the change is the identity there.

## The second defect the first one hid: the C++ dialect is the tree's

With the C++ rows selected, **82 of 96 compile**. Eight fail with `[-Wc++11-narrowing]` — a
brace-initializer whose value does not fit the declared type, e.g.
`int foo = { self->reserved | 0xC0000000 };`. This is not a code bug: it is an **error in clang ≥ 16
and a warning/pass in the Xcode-5 clang that built D13**. The build pins no `-std`, so `clang++` used
its own default (gnu++14) and read 2013-era C++ through 2014 eyes. Two more (`S5L8930XIO.cpp`,
`AppleARMNMI.cpp`, both calling `UINT32_MAX`) reach IOKit headers whose macro is defined only under the
legacy `stdint.h` the older dialect selects.

**Measured:** D13 C++ is **82/96 at the default dialect, 93/96 under `-std=gnu++98`** — the default of
D13's era. The flags are **gated on the same filesystem test 917 uses to tell the trees apart** (D13
ships the legacy `osfmk/sys/types.h`, 4570 does not), so on 4570 `CXX_STD_FLAGS` is empty and every
4570 object is unchanged — which it must be, because gnu++98 **regresses** 4570: `IOPMrootDomain.cpp`
uses a genuine C++11 construct and fails only under the old dialect. The fix gives each tree the
dialect it was written in rather than imposing one on both.

The fork's own build agrees: `makedefs/MakeInc.def:427` is `CXXFLAGS_GEN = -fapple-kext` and pins **no**
`-std`, i.e. it relied on the compiler's default, which in that era was gnu++98.

## Result

| | before 922 | after 922 |
|---|---|---|
| D13 manifest rows | 409 | **741** |
| D13 C++ compiled | 0 (not selected) | **89/96** |
| D13 objects | 372 | **690** |
| D13 C failed | 5 | 5 (unchanged) |
| 4570 manifest | 728 | **728 — byte-identical** |
| 4570 C++ objects | — | **62/62 byte-identical** |
| `make check` | 0 | **0** |

The spine now exists: `OSObject`, `OSMetaClass`, `OSArray`/`OSData`/`OSDictionary`/`OSSet`/`OSString`,
`IORegistryEntry`, `IOService`, `IOPlatformExpert`, `IOCPU`, `IOWorkLoop`, and the rest of IOKit's
`Kernel/`. The platform block (`src/platform/darwin13/*.cpp`) compiles into
`out/xnu_platform_obj_d13/{MSM8974PlatformExpert,MSM8974RootResource,MSM8974Timer,MSM8974GIC}.o` and
now has the C++ bodies it subclasses.

## The walls the link meets next (named, not closed)

The C++ spine is not yet **complete**, and the two walls that remain are the next rung's:

1. **The board macro (`BOARD_CONFIG_*`) — 4 files.** The fork gates five translation units on its own
   board target: `IOService.cpp:2`, `IOUserClient.cpp`, `IOStartIOKit.cpp`, `OSKext.cpp` (and
   `libsa/bootstrap.cpp`) each `#include "../../../nokextd/IOS7NoKextd035.h"`, whose line 7 is
   `#error IOS7NoKextd035 requires the LEO compilation target` unless `BOARD_CONFIG_QSD8250_LEO` is
   defined. `IOService.cpp` is a **base class the whole IOKit object graph needs**, so this is not
   optional for the link. The fix is the HD2 playbook (memory `mi4-hd2-fork-build-boot-map`): add an
   **`MSM8974_CANCRO` machine config** and its `-DBOARD_CONFIG_*`, and **stage** the untracked-tree
   file-list rows — the same shape as 918/920, one more level up.

2. **`UINT32_MAX` include-order — 3 files.** `IOMedia.cpp`, `IODeviceTreeSupport.cpp` and (via the
   same header) `IOS7LabPL181.cpp` use `UINT32_MAX`, which **is** defined in D13's
   `EXTERNAL_HEADERS/stdint.h:99`; they reach a stdint without it, an include-order shadow of the same
   shape 917 fixed for `<sys/types.h>` (a header with the right guard resolved to the wrong file).
   Named, not diagnosed.

## Verification

| check | result |
|---|---|
| 4570 manifest byte-compare | identical (728 → 728 rows, `diff` clean) |
| 4570 libkern+iokit C++ objects | 62/62 byte-identical to the committed pool |
| D13 `expand_options` with `XNU_MASTER_DIR=iokit/conf` | `-DIOKITCPP=1` (the union's member) |
| D13 C++ block | 89/96 at `-std=gnu++98`, 82/96 at the default |
| D13 manifest | 409 → 741 rows |
| `make check` | **0** |

## What this does not do

It does not link the D13 image (`build_entry.sh` still selects 4570's object pool with 4570's closure),
it does not establish the board macro, and it does not finish the assembler dialect work 921c named
(6 `.s` files). It delivers D13's **C++ spine** and proves it is inert on 4570.