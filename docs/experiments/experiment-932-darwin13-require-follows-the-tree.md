# 932 — the entry link's object list follows the tree (2026-10-08)

929/931 established that the `REAL_ARM_INIT` object list in `build_entry.sh` is 4570's hand-picked
`arm_init` closure, and that on D13 it names 83 objects D13 does not build (or reaches only through
the pool). This rung makes the link follow the selected tree.

## The change

Two branches, both gated on the tree's own discriminator (`osfmk/sys/types.h`), so 4570 is untouched.

### (1) `require` is tolerant for the tree-object pools on D13

    require() {
        [[ -f $1 ]] && return 0
        if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then
            case "$1" in
                "$XNU_KERNEL_OBJ_OUT"/*|"$XNU_ASM_OBJ_OUT"/*) return 0 ;;
            esac
        fi
        say "no $1 - $2" >&2; exit 2
    }

The first `require` is `require "$ARM_DATA_OBJ"` → `out/xnu_asm_obj_d13/data.o`, which D13 cannot
build because **D13 has no `data.s`**. An absent *named* XNU object on D13 is one the selected tree
does not build — requiring it is requiring 4570. The `STAGE90_*` platform-block objects and `$LIBGCC`
stay fatal: their absence is a step-not-run error a plain build must keep reporting, and the scoping
(`"$XNU_KERNEL_OBJ_OUT"/*|"$XNU_ASM_OBJ_OUT"/*`) is what keeps that true.

### (2) the link's object list is filtered on D13

The `436` pool glob adds the **whole** `_d13` pool (930). The 4570 names are still in `LINK_OBJS`
after it, and on D13 a 4570 name can be (a) a **different object at the same path** the glob already
added — a duplicate definition — or (b) a path D13 does not build. So on D13, objects that are absent
or already present by path are dropped:

    if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then
        ...keep objects that exist and are not already seen; drop the rest...
        say "  931: N named -> M kept (A absent 4570 name(s), D duplicate)"
    fi

## Verification

| check | result |
|---|---|
| `bash -n src/entry/build_entry.sh` | ok |
| D13 run, `data.o` wall | **cleared** — no `no …/data.o` stop |
| D13 run, next stop | `MSM8974PlatformExpert.o` (platform block, correctly **fatal**) |
| D13 `require` scoping | platform objects + libgcc stay fatal |
| 4570 | both branches gated on `osfmk/sys/types.h`, which 4570 does not ship → `require` identical, filter never runs |
| `make check` | 0 |

The 4570 arm could not be rebuilt end-to-end here: both the pristine and the edited script stop
identically on a pre-existing `out/` drift (`stage90_root_media.c` compiled with
`STAGE90_XNU_EMMC_STRATEGY=0` while the entry asks for 1; and the `xnu_kernel_obj` pool is a RELEASE
build beside a STAGE90_XNU entry). This is the operator `out/` state, **not** the edit — HEAD fails
the same way, verified by running it. 4570-neutrality is structural: the gates are tree checks and
4570 is not that tree.

## The next wall

With the require and the list following the tree, the D13 link stops at the **platform block**:
`out/xnu_platform_obj_d13/MSM8974PlatformExpert.o` is absent because the platform C++ does not compile
against D13 yet — `MSM8974PlatformExpert.cpp` fails on `mach_voucher_attr_raw_recipe_array_t` /
`ipc_voucher_t` in `out/mach_headers/mach/mach_host.h`, i.e. it is reading **4570's** MIG headers
(`XNU_*_OUT` selects them, but that build passed `MACH_HEADERS_OUT=<d13>`, so the generated
`mach_host.h` there is the D13 one — the failure is the D13 `mach_host.h` itself referencing voucher
types D13's MIG output does not carry, a MIG-output wall). That, and the 36-symbol D13 supply list
(931), are the next rungs.

## What moved

`src/entry/build_entry.sh`: the `require` helper and a D13-gated filter before the link. No tree edit,
no device. The arm in `out/stage90` is restored (`cb4e17f1…`).