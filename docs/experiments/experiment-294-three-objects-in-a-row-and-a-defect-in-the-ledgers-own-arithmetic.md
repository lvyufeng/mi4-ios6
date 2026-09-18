# Experiment 294 — three objects for three stubs in a row, and a defect in the ledger's own arithmetic

**Step:** link `osfmk_kern_stack.o`, `osfmk_kern_thread_policy.o` and `osfmk_arm_pcb.o` for
`stack_init`, `thread_policy_init` and `machine_thread_init` — `thread_init`'s tenth, eleventh and
twelfth calls, at +0xd4, +0xd8 and +0xdc. Three consecutive unconditional calls is the one shape where
a multi-object step costs nothing in prediction quality: whichever of the three bodies stops first, the
answer is one of them, and each was read.
**Prediction:** that all three run, that `thread_init`'s own tail (the four ledger calls 287 already
exercised) is clean, and the stop is **`atm_init` at `kernel_bootstrap + 0x2d8`, caller `0x8000e3d8`** —
plus that the 16 KB boundary moves a **second** time, `.data` 0x80110000 → 0x80114000.
**Result:** both, and the build also found a real defect in the per-object counting rule this ledger has
used since 288.

## The objects, measured together

```
2544 + 12864 + 1132 bytes of .text, 570 of .rodata, 76 + 8 + 4 of .bss

resolved  39   34 functions and 5 storage
added     12 (see below - 15 read per object, 3 of them satisfied inside the step)
```

The five storage names retired are worth their own line, because they are the only thing that can move
`.bss` (the 292 finding): `kernel_stack_size`, `stack_total`, `allow_qos_policy_set`,
`thread_qos_policy_params` and `ads_zone`. `ads_zone` is the name 290 added; this step finally gives it
a real definition, in `machine_thread_init`'s single `str`.

## The three bodies are small enough to quote

* **`stack_init`** — five calls, all real: `arm_usimple_lock_init`, `PE_parse_boot_argn`,
  `_consume_printf_args`, and `panic` twice (both behind the `stack_pages` boot-arg check, which a
  payload carrying no such argument does not take). It writes `kernel_stack_size = 16384`,
  `kernel_stack_pages = 4`, `kernel_stack_mask = 0xffffc000`, `kernel_stack_depth_max = 0`.
* **`thread_policy_init`** — **one call**, `PE_parse_boot_argn`. Nothing else at all.
* **`machine_thread_init`** — `ads_zone = zinit(256, 16384, 16384, "arm debug state")` and a return.
  A `-dr` dump of `pcb.o` appears to attribute nine calls to it; they belong to `machine_switch_context`
  and its neighbours, and the real body is five instructions between the `push` and the `pop`. The one
  stub reachable from `zinit`, `btlog_create` at `zinit + 0x950`, is behind the `.bss` guards
  `task_init` has satisfied in every run since 289.

`thread_init`'s tail is `PE_parse_boot_argn` ×2, `ledger_template_create`, `ledger_entry_add`,
`ledger_set_callback`, `ledger_template_complete` — all real, and **all four of the ledger calls already
ran on the device in 287**, when `coalitions_init` → `init_task_ledgers` was the step. Their only
reachable stubs are the three known no-ops: `trace_backtrace` behind the zero `.bss` `log_leaks`,
`ast_taken_kernel` behind AST flags nothing has set this early, and `btlog_create`.

## The build found a defect in this ledger's own counting rule

```
                   predicted        measured
undefined          776              773
function           687              683
storage             89               90
.data              0x80114000       0x80114000
__bss_start        0x8012bc08       0x8012bc08
bss end            0x80162398       0x80162398
image              1230872          1230896
text               ~1116100         1115928
```

The boundary predictions were right. The counts are the interesting part, because they are off by
exactly the number of names **satisfied inside the step**: `machine_stack_attach`,
`machine_stack_detach` and `machine_stack_handoff` are undefined references in `stack.o` but **are
defined by `pcb.o`**, which this step links at the same time. A per-object measurement cannot see that —
it lists them as obligations of the step, and the link settles them without ever generating a stub.

So the rule the walk has used since 288 needs a qualifier, and this is the first step where it could
bite:

```
single-object step   added = refs - (stub ∪ undef ∪ image nm)
multi-object step    ... then remove any added name another object in the same step defines
```

and the function/storage split must come from the **generated stub object** rather than from the
reference site: `kpc_off_cpu_active` is a `B 0x4` of storage, not a function. With 12 added (11
functions and 1 storage) instead of 15, `706 − 34 + 11 = 683` and `94 − 5 + 1 = 90` are both exact.

The image's +24 over the prediction is `stack.o`'s own `__DATA,__data`, which `.data` had room for
without moving — `.data` grew from 0x17c08 to 0x17c20.

The 16 KB boundary moved for the second step running, and the arithmetic that predicted it was the same
one that failed in 292: `.data` is 16 KB-aligned, `__TEXT,initcode` ends at 0x8010c8bc leaving 14148
bytes, and the step needs about 16100. `.data` went to **0x80114000**, `__bss_start` to
**0x8012bc08**, and the image to **1230896**. The pin followed again unprompted — the writes are
0x80162388 and 0x8016238c, with the reserved slot moving by the same 0x4000 as `.data`.

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x80162388 and 0x8016238c, both inside the reserved slot at 0x80162388
```

## The run

```
stub_hit=atm_init        xnu_entry_stub_caller=0x8000e3d8
```

`0x8000e3d8` is `kernel_bootstrap + 0x2d8`, the return address of the `bl` at 0x8000e3d4, with the
image's `bl thread_init` at 0x8000e3c4 and `kernel_debug_string_early` at 0x8000e3d0 between them.

So one run measured that `stack_init`, `thread_policy_init` and `machine_thread_init` all ran to
completion — including `machine_thread_init`'s single `zinit` — and that `thread_init`'s tail is clean.
**`kernel_bootstrap` is the function this walk started in**, so what is left of it is its own tail.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301109 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 295 — `osfmk/atm/atm.c` for `atm_init`, then `bank_init` (`osfmk/bank/bank.c`),
`ipc_pthread_priority_init` (`osfmk/voucher/ipc_pthread_priority.c`) and `corpses_init`
(`osfmk/corpses/corpse.c`) — four consecutive stubs in `kernel_bootstrap`'s tail, the same shape as this
step. After them: `kernel_thread_create` and `load_context`, the point where XNU stops initialising
structures and starts a thread.
