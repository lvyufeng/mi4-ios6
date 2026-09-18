# Experiment 276 — `mac_labelzone_init`, and a stop two functions further on

**Step:** link the object that defines `mac_labelzone_init` — `security/mac_label.c`, `manifest:669`,
the name 275's run stopped at.
**Prediction:** `stub_hit=ipc_host_init` with the caller at **`ipc_init+0xe4`** — because
`mac_labelzone_init` completes, the walk lands back in `kernel_bootstrap`, and the `ipc_init` that
265 linked but never ran stops at a `bl ipc_host_init` *inside itself*.
**Result:** exactly that. Resolved 1, added 0. `stub_hit=ipc_host_init`,
`xnu_entry_stub_caller_v=0x800ac340` = **`ipc_init+0xe4`**, three roads agreeing, zero aborts — and
three functions that had never run on this device completed in the one run: `mac_labelzone_init`, and
then `ipc_init` with its two `kmem_suballoc` calls. **The eighteenth prediction in a row.**

## The step, and what the object brought

`security_mac_label.o` is **358 bytes of text, 0 of data, 4 of bss, 8 definitions and 7 references** —
and all seven references were already real (`bzero`, `panic`, `zalloc`, `zalloc_noblock`, `zfree`,
`zinit`, `zone_change`). **1 resolved, 0 added**: 869 → 868 undefined, 792 function stubs and 76
storage, text 949572 → 949860, and the image bytes, `.bss` and the layout **unchanged** — 288 bytes of
text fitted inside the linker script's alignment padding. Four of the five functions the object defines
(`mac_labelzone_alloc`, `mac_labelzone_free`, `mac_label_get`, `mac_label_set`) are referenced by
nothing in this image, so they are not even stubs — 250's shape for the last time.

## The built function, and a tail call of its own

```
0000: push {r4, lr}
0018: bl zinit(16, 0x20000, 16, "MAC Labels")   ; sizeof(struct label) = 16, a 128 KB ceiling
0030: bl zone_change(zone, Z_EXPAND = 3, TRUE)
0040: bl zone_change(zone, Z_EXHAUST = 1, FALSE)
0050: pop {r4, lr}
0054: b zone_change                             ; Z_CALLERACCT = 5, FALSE
```

Every call is real, so `mac_labelzone_init` **completes** — and because it was entered by
`mac_policy_init`'s tail call, that `pop {r4, lr}` carries `kernel_bootstrap+0x248` straight through
its own tail call: control lands back in `kernel_bootstrap` at `8000e148`, which is the address both
274's and 275's stops were reported from.

## Why the prediction is inside `ipc_init`, and why the caller key changes shape

From `+0x248` the line is one debug string, then `8000e154: bl ipc_init`. `ipc_init` is real code this
image has carried since 265 — but 265 never ran it, because `ipc_bootstrap` stopped before
`ipc_bootstrap` returned and `ipc_init` is called *after* it. Its compiled body calls exactly three
things, and the last of them is a stub:

```
bl kmem_suballoc(kernel_map, &min, ipc_kernel_map_size, ...)        ; real (244)
bl panic                                                           ; only on failure
bl kmem_suballoc(kernel_map, &min, ipc_kernel_copy_map_size, ...)   ; real
bl panic                                                           ; only on failure
   ... the `msg_ool_size_small` clamp against `kalloc_max_prerounded` (an immediate 8192 and a
       `subls r2, r1, #20` for `cpy_kdata_hdr_sz`), and the `ipc_kernel_copy_map` flags
       `no_zero_fill` / `wait_for_space` (`orr r1, r3, #5`) — all stores, no calls
800ac33c: bl ipc_host_init                                          ; A STUB
800ac340: add sp, #24
800ac344: pop {r4, r5, r6, r7, fp, pc}
```

so the stop is `ipc_host_init` at **`ipc_init+0xe4`** — the return address of a `bl`, i.e. the
*opposite* of the last three steps, where the caller key pointed at the caller's caller because the
call under test was a tail call. That difference is the reason this prediction had to be written from
the disassembly of the function that would be *entered*, rather than from the one that was left, and it
is why the key is worth stating explicitly each time.

## What the run measured

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `ipc_host_init` | the prediction, named by the stub's own write |
| `xnu_entry_stub_caller` | `0x800ac340` | `ipc_init+0xe4`, read out of `g_kv_buf` |
| `xnu_entry_stub_caller_a` | `0x800ac340` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x800ac340` | the value itself, through the ram-console path |
| `xnu_entry_stub_caller_e` | `0x800ac340` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_stub_caller_digits` | `0x31` | 49 = 24 + 25, the digits' offset |
| `xnu_entry_stub_caller_w0` | `0x61303038` | `800a`, the four bytes in `g_kv_buf` |
| `xnu_entry_stub_caller_w1` | `0x30343363` | `c340`, the next four |
| `xnu_entry_kv_written` | `0x5e` | 94 = a 24-byte stub record plus 34- and 36-byte caller records |
| `xnu_entry_kv_in_dram` | `0x82` | 130 = 94 + 36, the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken |

`tools/host_resolve_entry_addr.sh 0x800ac340` gives `ipc_init+0xe4`, and `caller-4 = 0x800ac33c` is the
`bl 800cddac <ipc_host_init>` itself. Three roads to the same value, all agreeing, and the report's
echo intact.

**What ran in this one run, by completing:** `mac_labelzone_init` created the `"MAC Labels"` zone
(`zinit(16, 131072, 16)`, a 128 KB ceiling) and made its three `zone_change` calls; the walk returned
to `kernel_bootstrap`; and `ipc_init` ran its whole body — **two `kmem_suballoc` calls that built
`ipc_kernel_map` and `ipc_kernel_copy_map`** (each with the `panic` on failure not taken), the
`msg_ool_size_small` clamp against `kalloc_max_prerounded`, and the two `ipc_kernel_copy_map` flags.
Those are the first IPC *maps* this kernel has created, and the second and third `kmem_suballoc`s of the
boot after 250's kalloc map. 661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF
again — the fifth build running.

## Safety

One run, `fastboot boot` only, nothing flashed. `persistent_write_attempted=0x00000000` in all 25
places, `failure_mask=0x00000000` in all 87 contracts, zero aborts, and the device returned to Android
on its own.

## What is next

`ipc_host_init` is `osfmk/kern/ipc_host.c` (`manifest:549`) — the next step. Behind it in
`kernel_bootstrap`'s line: `mapping_free_prime` (`8000e170`, real), then `machine_init`, `clock_init`
and `ledger_init`, and after that stretch `bsd_init` — where "XNU loads, enters the operating system and
runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above SECURITY_MAC_LABEL_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x800ac340`;
`tools/host_resolve_entry_addr.sh 0x800ac340` resolves it against `out/stage90/xnu_arm_entry.elf` to
`ipc_init+0xe4`.
