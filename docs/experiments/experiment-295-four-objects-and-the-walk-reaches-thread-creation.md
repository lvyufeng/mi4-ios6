# Experiment 295 — four objects in `kernel_bootstrap`'s tail, and the walk reaches thread creation

**Step:** link `osfmk_atm_atm.o`, `osfmk_bank_bank.o`, `osfmk_voucher_ipc_pthread_priority.o` and
`osfmk_corpses_corpse.o` for `atm_init`, `bank_init`, `ipc_pthread_priority_init` and `corpses_init` —
the four stubs left in `kernel_bootstrap`'s tail, at +0x2d4, +0x2f4, +0x304 and +0x308.
**Prediction:** that all four run, and the frontier moves into thread creation for the first time —
**`uthread_alloc`, `thread_create_internal + 0x088`, caller `0x8000ad74`** — plus that `.data` moves a
**third** time, 0x80114000 → 0x80118000, on a margin this ledger flagged as close.
**Result:** all of it, and the caller address was confirmable *before* the run for the first time.

## The objects, measured

```
5408 + 6288 + 396 + 2652 bytes of .text, 1236 of .rodata, 4416 + 300 + 4 + 8 of .bss

resolved  29   26 functions and 3 storage
added      7   atm_collect_trace_info, atm_inspect_process_buffer,
               commpage_update_atm_diagnostic_config, proc_getgid, proc_getuid,
               proc_persona_id, gather_populate_corpse_crashinfo
```

The three storage names retired are `bank_ledgers`, `corpse_for_fatal_memkill` and
`exc_via_corpse_forking` — the last two named like functions and *are* storage, which is exactly the
trap 294 recorded. Nothing in the four objects overlaps, so 294's in-step qualifier changes nothing
here.

## The measurement that had to be redone

A `-dr` dump of an object prints the relocations of the **whole section** before the function's own, so
a naive parse of `--disassemble=NAME` attributes every call in the object to that function. Scoped that
way, `ipc_pthread_priority_init` appeared to call **`pthread_priority_canonicalize`** — a stub — and the
prediction would have been that name.

Scoped to the function's own address range, the four bodies are:

| | |
|---|---|
| `atm_init` | 14 calls, all real: `PE_get_default`, `PE_parse_boot_argn`, `zinit` ×3, the lock-group trio, `lck_mtx_init`, `ipc_register_well_known_mach_voucher_attr_manager`, `panic`, `_consume_kprintf_args` |
| `bank_init` | 16 calls, all real: `zinit` ×2, `ledger_template_create`, `ledger_entry_add` ×2, `ledger_template_complete`, `lck_spin_init`, the lock-group trio |
| `ipc_pthread_priority_init` | **3 calls**, all real: `_consume_kprintf_args`, `ipc_register_well_known_mach_voucher_attr_manager`, `panic` |
| `corpses_init` | **3 calls**, all real: `PE_parse_boot_argn` ×3 |

**So all four are clean, and the frontier moves somewhere new.** This is the second measurement defect
of the same family in two experiments — 294's was a per-object rule applied to a multi-object step,
this one is a per-object tool applied to a per-function question — and both were caught before the
device was touched.

## The frontier is thread creation

`kernel_bootstrap`'s next call is `kernel_thread_create` at +0x32c (real, not in the stub list), and the
image's body of it calls `stack_alloc` — real since 294 — and then `thread_create_internal`. Its first
stub:

```
+0x038  zalloc                real
+0x074  __aeabi_memcpy8       real
+0x084  uthread_alloc         ***STUB***   <- the stop, return address +0x088
```

**And the caller address was confirmable before the run**, which is new: `osfmk_kern_thread.o` sits
earlier in the link list than every object this step adds, so `thread_create_internal` is at
**0x8000acec** both before and after the rebuild, `uthread_alloc` is a stub at 0x800fbffc, and the
predicted caller is `0x8000ad74` — the `bl` the image has at 0x8000ad70.

## The build

```
                   predicted        measured
undefined          751              751
function           664              664
storage             87               87
.data              0x80118000       0x80118000
__bss_start        0x8012fc08       0x8012fc68
image              1247420          1247424
text               1130964          1130904
```

`.data` moved for the **third step running**, and the marginal call this ledger flagged was a real one:
the need was about 15036 against 14564 of room. The three moves have taken it
0x8010c000 → 0x80110000 → 0x80114000 → 0x80118000, each 16 KB, with the image
1198104 → 1214488 → 1230896 → 1247424. Headroom 1694824 → 1673768.

`__bss_start` is off by 0x60 and the reason is worth recording: `.bss` *itself* grew by 4576 — the four
objects' 4728 less the three storage slots this step retires — so this is the first time the content of
`.bss` moved `__bss_start` rather than its 0x4000 alignment.

## The run

```
stub_hit=uthread_alloc        xnu_entry_stub_caller=0x8000ad74
```

`0x8000ad74` is `thread_create_internal + 0x088`, the return address of the `bl` at 0x8000ad70.

So one run measured `atm_init`, `bank_init`, `ipc_pthread_priority_init` and `corpses_init` all
completing — `atm_init`'s three `zinit`s and `bank_init`'s four ledger-template calls among them —
`kernel_bootstrap` reaching `kernel_thread_create`, and `kernel_thread_create` reaching
`thread_create_internal` and its `zalloc` and `__aeabi_memcpy8`. **The walk is inside thread creation,
and `kernel_bootstrap`'s body is finished** except for `thread_deallocate` and the branch to
`load_context` at +0x380.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301114 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 296 — `osfmk/kern/uthread.c` for `uthread_alloc` and its three siblings
(`uthread_cleanup`, `uthread_cred_free`, `uthread_zone_free`), which `thread_create_internal` calls on
its error paths, so one step should retire four names at once. Then the rest of
`thread_create_internal`: `kpc_thread_create`, `sched_set_thread_base_priority`,
`sched_thread_mode_demote`, `machine_thread_create` → `machine_thread_state_initialize`,
`ipc_thread_terminate` → `io_free`. After those, `kernel_thread_create` returns a real thread to
`kernel_bootstrap`, which calls `thread_deallocate` and then `load_context` — the first time this walk
crosses into a context switch rather than a function call.
