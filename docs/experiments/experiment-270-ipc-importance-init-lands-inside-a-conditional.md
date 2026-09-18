# Experiment 270 — `ipc_importance_init` lands, inside a conditional

**Step:** link `osfmk/ipc/ipc_importance.o` into the entry image.
**Prediction:** `stub_hit=semaphore_init`, at `ipc_bootstrap+0x18c`.
**Result:** `stub_hit=semaphore_init`, `xnu_entry_stub_caller=0x800abfd0` = `ipc_bootstrap+0x18c` —
the thirteenth prediction in a row, and the first one that is inside a `#if`.

## The change

One object. `osfmk/ipc/ipc_importance.c` (manifest:511), `osfmk_ipc_ipc_importance.o` — 13312 bytes
of text, 24 of data, 48 of bss, **72 definitions and 51 references**.

Large but shallow: of the 72 definitions only **three** are names the image already carries —
`ipc_importance_init`, `ipc_importance_thread_call_init`, `task_importance_list_pids`, all three
current stubs. **3 resolved, 7 added.** This is 250's shape, not 269's: what an object *defines* is
not what the link *needs*, and most of these 72 are functions nothing in the image references yet.

The seven added names are the QoS/boost surface the importance machinery reaches for at runtime
rather than at init — `ipc_port_impcount_delta`, `ipc_port_importance_delta`,
`ipc_port_importance_delta_internal`, `ipc_port_sync_qos_delta`, `task_importance_reset`,
`task_policy_update_complete_unlocked`, `task_update_boost_locked` — reached from
`ipc_importance_send`, `ipc_importance_disconnect_task` and the `*_assertion` paths, none of them from
`ipc_importance_init`.

| | before (269) | after |
| --- | --- | --- |
| undefined | 842 | 846 |
| function stubs | 766 | 770 |
| storage stand-ins | 76 | 76 |
| text | 916612 | 930244 |
| image bytes | 1015640 | 1032048 |
| bss end | 0x8012c6c8 | 0x801306c8 |
| args | +1236992 | +1253376 |
| headroom | 1915192 | 1898808 |
| payload bytes | 1510096 | 1526504 |

Undefined went **up** by 4 because 7 were added and 3 resolved. Storage is unchanged for the third
step running: `task_max`, `thread_max`, `ipc_lck_grp` and `ipc_lck_attr` were all already there.

## Why the prediction is what it is, and why it is inside a `#if`

`ipc_importance_init` sits behind `#if IMPORTANCE_INHERITANCE` in `ipc_bootstrap` (`ipc_init.c:206`),
which is why the *previous* step's stop named it. The source order around it —

```c
	mig_init();
	ipc_table_init();
	ipc_voucher_init();

#if IMPORTANCE_INHERITANCE
	ipc_importance_init();
#endif

	semaphore_init();
	mk_timer_init();
	host_notify_init();
```

— matches the linked image exactly, which is worth stating because this project has twice written a
confident, wrong claim about which code runs by reading C in statement order (experiments 206, 211):

```
800abfbc: bl mig_init
800abfc0: bl ipc_table_init
800abfc4: bl ipc_voucher_init
800abfc8: bl ipc_importance_init      <- the stop (return address +0x188)
800abfcc: bl semaphore_init           <- the prediction (return address +0x18c)
800abfd0: bl mk_timer_init
800abfd4: pop {r4, r5, fp, lr}
800abfd8: b  host_notify_init         <- a tail call; its `lr` is ipc_bootstrap's caller
```

The whole function's body, disassembled from the object:

```
ldr thread_max ; ldr task_max ; add r1, r4, r5 ; add r4, r1, r1, lsl #1   ; (t + th) * 3
add r1, sp, #14 ; mov r2, #26 ; bl PE_parse_boot_argn
lsl r1, r4, #6  ; mov r0, #96 ; mov r2, #96 ; bl zinit   ; str -> ipc_importance_task_zone
bl zone_change(Z_NOENCRYPT)
lsl r1, r4, #5  ; mov r0, #48 ; mov r2, #48 ; bl zinit   ; str -> ipc_importance_inherit_zone
bl zone_change(Z_NOENCRYPT)
bl lck_spin_init(&ipc_importance_lock_data, &ipc_lck_grp, &ipc_lck_attr)   ; inlined lock init
bl ipc_register_well_known_mach_voucher_attr_manager(&ipc_importance_manager, 0, 2,
                                                      &ipc_importance_control)
cmp r0, #0 ; beq done ; bl _consume_printf_args ; done: pop {r4, r5, fp, pc}
```

Six calls, and every one is real in this image: `PE_parse_boot_argn`, `zinit` and `zone_change`
since 250, `lck_spin_init` since 262, and `ipc_register_well_known_mach_voucher_attr_manager`
**since 269** — it is defined by `ipc_voucher.c`, and 269's run reaching `ipc_importance_init` means
`ipc_voucher_init` returned, which means that function and its `user_data_attr_manager_init` caller
already ran on the device once. `_consume_printf_args` is real too (retired by 199), so even the
error branch is not a stub — it would print "Voucher importance manager register returned" and
continue, which is a visibly different outcome from a `stub_hit`.

## The result

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003b
 xnu_entry_kv_in_dram=0x0000003b
 xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x800cd5d8
 xnu_entry_why_byte=0x00000061
 xnu_entry_abort_entries=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=semaphore_init
 xnu_entry_stub_caller=0x800abfd0
```

59 bytes written, nothing dropped, **zero aborts**, and the `why` line correct — which is the first
run since 268 whose report needed no repair, and the return on 269's two fixes.

`0x800abfd0` resolves to `ipc_bootstrap+0x18c`, the return address of the `bl semaphore_init` at
`+0x188`. The *offset* is the durable form: `ipc_bootstrap`'s base has not moved this time
(`0x800abe44`, unchanged from 269, because the object landed after it), but 267, 268 and 269 each
moved the absolute address and none moved the offset.

## The stand-in this step cannot see, now in its second place

`ipc_importance_max = (task_max + thread_max) * 2` reads `task_max`, still a storage stand-in whose
value is 0, so it computes `(0 + 1536) * 2 = 3072` where the real kernel computes
`(512 + 1536) * 2 = 4096`, and both `zinit` ceilings are a quarter smaller than they should be
(3072 × 96 = 288 KB against 4096 × 96 = 384 KB; 3072 × 48 = 144 KB against 4096 × 48 = 192 KB).

The image shows the arithmetic even though one input is invisible in it. `add r1, r4, r5` is
`thread_max + task_max`; `add r4, r1, r1, lsl #1` is that sum times three; and the two `lsl`
immediates are 6 and 5 — the compiler folded `* 2 * sizeof(struct ...)` into `* 3` and then
`<< 6` (96 = 3 × 32) and `<< 5` (48 = 3 × 16). So the ×2 that is *not* in the instruction stream is
exactly the factor whose other operand is the zero stand-in.

269's `ipc_voucher_init` shrinks one zone the same way. This step makes it two. The repair is
`osfmk/kern/task.o`, which defines `task_max = CONFIG_TASK_MAX` in `.data` — and it stays its own
step, because it is a much larger object and because the whole point of the frontier method is one
measurable change per run.

## Safety

All 87 contracts report `failure_mask=0x00000000`, all 25 that report it say
`persistent_write_attempted=0x00000000`, none says `1`, nothing is flashed — the run is a
non-persistent `fastboot boot` of a boot image — and the device returned to Android on its own
("No errors detected").

## What is next

- `semaphore_init` — `osfmk/kern/sync_sema.c` (manifest:587), `osfmk_kern_sync_sema.o` (9768 bytes
  of object). In call order after it: `mk_timer_init` (`osfmk/kern/mk_timer.c`, manifest:572), then
  `ipc_bootstrap` **returns** — its instructions are `pop {r4, r5, fp, lr}` at `+0x190` followed by
  a tail `b host_notify_init`, which is the 252 shape: a stop there has its `lr` pointing at
  `kernel_bootstrap+0x238`, not at anything inside `ipc_bootstrap`, so the prediction for it must be
  written against `kernel_bootstrap`.
- Then `kernel_bootstrap` continues with `mac_policy_init`, and after that `bsd_init` — where "XNU
  loads, enters the OS and runs its basic drivers" stops being a forecast.
- `osfmk/kern/task.o`, for `task_max`, as above.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The stop is `stub_hit=semaphore_init` in the results blob, with `xnu_entry_stub_caller` equal to
`ipc_bootstrap+0x18c` and every `xnu_entry_abort_*` field zero.
