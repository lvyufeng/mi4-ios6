# 928 — the de-underscore rule covers undefined references too (2026-10-08)

The D13 entry closure's last measured wall was a supply list of **63 symbols "not defined anywhere in
the pool"**. Nineteen of them were not missing at all: they were the *same* symbol spelled two ways,
and the assembly step was renaming one spelling and not the other.

## The defect

`tools/assemble_arm_layer.sh` sets `__NO_UNDERSCORES__`, so an `EXT(x)` in XNU's assembly expands to
`x` (not `_x`). But a D13 `.s` file does not only *define* `_x` names it then calls — it also
*refers to* C symbols the C build emits unprefixed, written with Apple's underscore: `.extern
_sleh_abort`, `bl _panic`, `ldr r0, =_arm_dcache_inv_all`. The de-underscore step renamed only the
**definitions** (`nm --defined-only`), so those references kept their underscore.

The result only becomes visible when the objects are pulled into a link and the linker asks the pool
for the name:

    traps_lo.o:  U _sleh_abort          vs  trap.o:            T sleh_abort
    traps_lo.o:  U _doexception         vs  trap.o:            T doexception
    cache.o:     U _arm_dcache_inv_all  vs  cpufunc-v7.o:      T arm_dcache_inv_all
    hw_lock.o:   U _lck_mtx_lock_acquire ...

…19 symbols over ten D13 asm objects (`traps_lo.o` 13, `cache.o` 8, `exctramps.o` 7, `hw_lock.o` 5,
`cswitch.o` 2, `cpufunc-v7.o` 2, `bcopy.o`/`machine_routines_asm.o`/`OSAtomic.o`/`stubs.o` 1 each).

## The fix

An undefined symbol carries its own name, so `objcopy --redefine-sym old=new` fixes the reference with
no change to what is defined. Same rule, both directions — one `nm` pass, keyed on the flag's own
convention:

    "$NM" "$OUT/$name.o" | awk '($1 == "U" && $2 ~ /^_[a-zA-Z]/) \
                            || ($2 ~ /^[TDBR]$/ && $3 ~ /^_[a-zA-Z]/) { print ($1 == "U") ? $2 : $3 }'

(`nm` prints the type a `--defined-only` listing would suppress, so one pass serves both halves.
`_start` is still excluded — the linker script enters at that exact name.)

## Verification

| check | result |
|---|---|
| D13 asm pool (`XNU_KERNEL_CONFIG=RELEASE`) | **33 ok, 0 failed**; `traps_lo.o` now `U sleh_abort`, `U arm_dcache_inv_all` |
| D13 closure walk, re-run | converges: **636 objects, supply list 63 → 44** |
| 4570 asm pool, old rule vs new rule | 3 objects differ — `cpu_in_cksum.o`, `WKdmCompress_new.o`, `WKdmDecompress_new.o` |
| the 8 changed 4570 refs | all now resolve to the **unprefixed** name the 4570 pool defines (`kprintf`, `enable_kernel_vfp_context`, `hashLookupTable_new`); the old rule left them dangling |
| are those 3 objects in the 4570 entry link? | **no** — outside the entry closure, so the 4570 entry image is untouched |
| `make check` | 0 |

**4570 neutrality.** The rule is *stricter* for 4570, not merely equal: 4570's `cpu_in_cksum.o`
carried `U _kprintf` against a pool defining `kprintf`, i.e. an unresolved reference the old rule
could not fix because it only looked at definitions. The 3 changed objects are not linked into the
4570 entry image, so nothing the project ships changes — but the change is the correct direction on
both trees.

## What the remaining 44 are

The real supply set, and it has the same shape as 4570's (`panic`/`_sleh_abort`/`PE_early_puts`/
`vcputc`/`arm_init_cpu` are all **in** the D13 pool already):

- banner glue: `arm_init_cpu`, `arm_init_idle_cpu`, `PE_init_SocSupport_stub`, `initialize_screen`,
  `draw_panic_dialog`, `vc_progress_initialize`/`_set`, `vc_display_icon`, `vcattach`, `vcputc`;
- `version`/`ostype`/`osrelease`/`version_major`/`version_minor`, `_mh_execute_header`;
- per-CPU/timer data with no D13 home: `EntropyData`, `ExceptionVectorsTable`, `gPhysSize`,
  `fiqstack_top`, `fleh_addrexc`, `fleh_decirq`;
- LKM/devfs stubs: `mach_gss_*` (5), `lockd_request`/`lockd_shutdown`, `UND*_rpc` (5);
- C++ runtime: `__cxa_atexit`, `__dso_handle`, `upl_get_internal_*` (3);
- the project's own shims: `_disable_preemption`/`_enable_preemption`, `adler32_vec`;
- the 5 `__aeabi_*` (compiler runtime, `src/xnu_aeabi_runtime.c`).

So the next rung is **(a) more pool coverage** (D13 sources whose symbols the closure needs but the
manifest did not select) **plus (b) a small stub set** — the entry stubs will largely port.

## What moved

`tools/assemble_arm_layer.sh`, the de-underscore awk. No tree edit, no device, no other file.