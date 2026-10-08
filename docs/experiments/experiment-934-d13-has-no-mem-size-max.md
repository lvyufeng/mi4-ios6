# 934 — the ceiling arm is a 4570 shape; D13 has no `MEM_SIZE_MAX` (2026-10-08)

933 made the build roots follow the tree, and the D13 link advanced to the **kernel-pool arm
agreement**: the carried switch set (4570's) asks for `STAGE90_XNU_MEM_SIZE_MAX=0x5e500000`, and the
pool's `osfmk_arm_arm_vm_init.o` carries the marker `unset`, so `build_entry.sh` refused:

    REFUSING: this build set STAGE90_XNU_MEM_SIZE_MAX=0x5e500000 but …/osfmk_arm_arm_vm_init.o
              carries the ceiling arm 'unset'

The first reading was "rebuild the D13 pool with the arm's switches". It is the wrong reading.

## The finding

`tools/patch_mem_size_max.py` makes `MEM_SIZE_MAX` a port in `osfmk/arm/arm_vm_init.c`:

    #ifdef STAGE90_XNU_MEM_SIZE_MAX
    #if (STAGE90_XNU_MEM_SIZE_MAX != 0x40000000)
    #define MEM_SIZE_MAX STAGE90_XNU_MEM_SIZE_MAX
    int entry_xnu_mem_size_max_arm_on(void)  { return 1; }
    #else
    #define MEM_SIZE_MAX 0x40000000
    int entry_xnu_mem_size_max_arm_off(void) { return 0; }

**Darwin 13's `arm_vm_init.c` has no `MEM_SIZE_MAX`.** `grep -rn MEM_SIZE_MAX <D13>/osfmk/` is empty.
Where 4570 clamps (`if (mem_size > MEM_SIZE_MAX) mem_size = MEM_SIZE_MAX;`), D13 does
`arm_vm_init.c:327`:

    max_mem = mem_size = sane_size = gMemSize;

— no clamp, and `pmap_bootstrap(gMemSize, …)` at `:483` takes `memSize` straight. There is **nothing to
port**: the patch is staged into 4570 (6 hits) and **never into D13** (0 hits). So on D13 the marker can
never appear, and the check's advice ("rebuild the kernel object with the arm's switch set") names a
rebuild that cannot succeed.

The D13 wall is therefore not "the pool was built without the switch" — it is **the 4570 switch set
carried onto a tree that has no such port**. Dropping the switch clears it (`xnu_entry_911b: … carries
NO ceiling marker and this build sets no STAGE90_XNU_MEM_SIZE_MAX - Apple's clamp, the default image`).

**3 GB on D13 is not this arm anyway.** 911b raises *4570's* clamp; the D13 route is the region-list
port of experiment 915 (two banks, a real physmap), a different mechanism.

## The change

The check becomes **tree-aware**: on the D13 tree, a set `STAGE90_XNU_MEM_SIZE_MAX` is named and
refused, instead of failing deep with advice that cannot help:

    if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then
        if [[ -n $_memsize_req && $_memsize_req != 0x40000000 ]]; then
            say "REFUSING: STAGE90_XNU_MEM_SIZE_MAX=$_memsize_req is a 4570 switch and this is the"
            say "          Darwin-13 tree, whose arm_vm_init.c has NO MEM_SIZE_MAX to port …"
            exit 2
        fi
    fi

Both the tree gate and the switch being explicit are what make this a fact rather than a comment: the
gate is `osfmk/sys/types.h`, the same discriminator the rest of the pivot uses, and **4570 ships no such
header**, so the guard never runs on 4570 — its behaviour there is byte-identical.

## Verification

| check | result |
|---|---|
| `bash -n src/entry/build_entry.sh` | ok |
| D13 link **with** the switch | refused, tree-aware message (was: the deep `unset` FAIL) |
| D13 link **without** the switch | memsize check passes (`xnu_entry_911b: … the default image`) |
| D13 link, next wall | `FAIL: 'thread_bootstrap_return' is not defined by …/xnu_asm_obj_d13/locore.o` |
| 4570 | guard gated on `osfmk/sys/types.h`, which 4570 does not ship → never runs |
| `make check` | 0 |

## The next wall: the arm_init object map is 4570's

With the ceiling arm out of the way the link stops at the **locore rename block** — and it is the same
defect as 931, one object map lower. The block keeps three names and `locore_`-prefixes the other
seventeen globals `locore.o` defines:

    LOCORE_KEEP=(thread_bootstrap_return thread_exception_return thread_syscall_return)
    LOCORE_RENAME_EXPECTED=(ExceptionVectorsBase ExceptionVectorsEnd ExceptionVectorsTable
                            fleh_addrexc fleh_dataabt fleh_dec fleh_decirq fleh_fiq_generic
                            fleh_irq fleh_prefabt fleh_reset fleh_swi fleh_undef ResetHandlerData …)

That is **4570's `locore.o` map**. D13's `locore.s` is a *different file* — it defines `__start`,
`intstack`/`intstack_top`, `debstack`/`debstack_top`, `sleep_test`, and **none** of the twenty names.
D13 splits them across three objects:

| 4570 `locore.o` name | D13 object |
|---|---|
| `thread_bootstrap_return`, `thread_exception_return`, `thread_syscall_return` | `cswitch.s` |
| `ExceptionVectorsBase`, `ExceptionVectorsEnd`, `ExceptionVectorsTable`, `ExceptionVectorPanic`, `fleh_*` | `exctramps.s` / `exctramps_hi.s` |
| `fleh_reset` | `traps_lo.s` |
| `ResetHandlerData` | (absent from D13) |

So the frontier is the same one 931 named at the object level, now at the **symbol-map** level: the
entry's `REAL_ARM_INIT` closure — both its 280-object member list (931) and this locore rename/KEEP map
— is 4570's hand-picked `arm_init` closure and must be re-derived from **D13's own** `arm_init.o`
closure over the `_d13` pools (`tools/entry_closure.py`). The locore block is the next sub-rung of that
work: which D13 object owns each of the twenty names, and which of the three "keep" names D13's
`cswitch.o` must not rename.

*Provenance: `out/xnu_kernel_obj_d13/osfmk_arm_arm_vm_init.o`, `out/xnu_asm_obj_d13/locore.o`,
D13 `osfmk/arm/{locore,cswitch,exctramps,traps_lo}.s`, this session's `/tmp/d13_entry_link4.log`.
Host-side, reversible, no press.*