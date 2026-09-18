# Experiment 273 — `mk_timer_init` lands, and the frontier leaves its function through a tail call

**Step:** link the object that defines `mk_timer_init` — `osfmk/kern/mk_timer.c`, `manifest:572`, the
name 272's run stopped at — and take the next one-object step.
**Prediction:** `stub_hit=host_notify_init`, with the caller at **`kernel_bootstrap+0x238`** —
`mk_timer_init` is the last call in `ipc_bootstrap`, and `ipc_bootstrap` reaches the next callee with
a *tail call*, so the stub's `lr` is not anything inside `ipc_bootstrap` at all.
**Result:** exactly that. Resolved 2, added 1, 844 → 843 undefined. The run stopped at
`stub_hit=host_notify_init` with `xnu_entry_stub_caller=0x8000e138` = **`kernel_bootstrap+0x238`**, all
three roads agreeing, zero aborts. **The fifteenth consecutive prediction to hold, and the first whose
answer lies in a different function from the call under test.**

## The step, and what the object brought

`osfmk_kern_mk_timer.o` is **1577 bytes of text, 8 of data, 4 of bss, 12 definitions and 20
references**. The step resolved 2 (`mk_timer_init` and `mk_timer_port_destroy`) and added 1 —
`mach_msg_send_from_kernel_proper`, the first new undefined name since experiment 270 and again a
syscall surface rather than an init-path call. Fifteen of the twenty references were already real
(`zinit`, `zone_change`, `lck_spin_lock`, `lck_spin_unlock`, `zalloc`, `zfree`, `mach_absolute_time`,
`copyout`, `OSCompareAndSwap`, `arm_usimple_lock_init`, `ipc_kobject_set_atomically`,
`thread_call_setup`, `thread_call_cancel`, `thread_call_enter1`,
`thread_call_enter_delayed_with_leeway`), and four were already stubs (`ipc_object_translate`,
`ipc_port_release_send`, `mach_port_allocate_qos`, `mach_port_destroy`) — **all four inside trap
handlers** (`mk_timer_arm_trap` and its siblings), not on this path.

Ten of the twelve definitions are names the image has never heard of: the `*_trap` syscall surface,
`mk_timer_expire`, `mk_timer_qos`, `mk_timer_zone`, one string. That is 250's and 270's shape again —
what an object *defines* is not what the link *needs* — and it is the reason the step is small even
though the file is not.

The function itself is a `sizeof`, a no-op assert, and two calls:

```
mov r0, #104        ; s = sizeof(mk_timer_data_t)
mov r1, #0x68000    ; 4096 * s  = 425984
mov r2, #0x680      ; 16 * s    = 1664
bl zinit("mk_timer") -> mk_timer_zone
mov r1, #6          ; Z_NOENCRYPT
b zone_change       ; a tail call of its own
```

`assert(!(mk_timer_zone != NULL))` leaves no instruction and no undefined reference to `panic` or to
an assert helper, so nothing between the `push` and the `zinit` could stop.

## Why both callees were already known to be real

The prediction needed `zinit` and `zone_change` to return, and they had already been proved to, on the
device, in the previous experiment's run: 271 passed `semaphore_init`, whose body is `zinit`, an
arithmetic ceiling, and `zone_change` as well. A step whose callees have already run once is not a
gamble. What `mk_timer_init` asks for is `zinit(104, 425984, 1664, "mk_timer")` — a **416 KB ceiling,
the largest `max_mem` of any step so far**, which is worth knowing before the pool it draws from stops
being effectively infinite.

## The tail call, which is why the prediction is offset-first

Step 272 left the walk stopped at `mk_timer_init` with `xnu_entry_stub_caller=0x800ac254`, i.e.
`ipc_bootstrap+0x190`. In this image `ipc_bootstrap` ends:

```
800ac250: bl mk_timer_init
800ac254: pop {r4, r5, fp, lr}
800ac258: b host_notify_init     ; a tail call, so no lr is set for it
```

so the stub for `host_notify_init` sees whatever that `pop` restored, which is `ipc_bootstrap`'s *own*
return address — the instruction after `bl ipc_bootstrap` in `kernel_bootstrap`, which is
`8000e134: bl ipc_bootstrap` → return `0x8000e138`. `kernel_bootstrap` is at `0x8000df00`, so the
prediction was written as **`kernel_bootstrap+0x238`**: offset first, because four consecutive steps
had moved the absolute address and none had moved an offset. This is the shape experiment 252 met
from the other side — 252 was a tail call *into* a function the walk had already left — and the same
rule applies: the prediction has to be written against the caller's caller.

## What the run measured

| key | value | what it says |
| --- | --- | --- |
| `xnu_entry_stub_caller` | `0x8000e138` | `kernel_bootstrap+0x238`, read out of `g_kv_buf` |
| `xnu_entry_stub_caller_a` | `0x8000e138` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x8000e138` | the value itself, read in the epilogue |
| `xnu_entry_stub_caller_e` | `0x8000e138` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_kv_written` | `0x61` | 97 bytes in the in-DRAM buffer |
| `xnu_entry_kv_in_dram` | `0x85` | 133 = 97 + 36, the control for a faithful read |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no data or prefetch abort was taken at all |
| `xnu_entry_why_byte` | `0x61` | `'a'`, the first character of the epilogue's reason string |

Four roads to the same value — the two in-run calls, the `.bss` copy, and the epilogue's own write —
and the fourth is the one that has ever disagreed. This build's report is internally consistent, which
matters because the previous experiment's caution was that a report can be internals-inconsistent at
the same source and the same `.text` size.

The run also re-verified the 272 probe in a *different* image: 661 of 661 words of `entry_kv` through
`entry_stub_hit` match the linked ELF again, in this build's own addresses.

## The instrument's off-by-one, found by this run's own numbers

The `_w0`/`_w1` probe — which reads the eight bytes `entry_kv` stored, so the report can be checked
against memory rather than trusted — reported

```
xnu_entry_stub_caller_digits=0x00000035
 xnu_entry_stub_caller_w0=0x65303030   ; '0' '0' '0' 'e'
 xnu_entry_stub_caller_w1=0x0a383331   ; '1' '3' '8' '\n'
```

for a value whose digits are `8000e138`. The window therefore began at the **second** digit. The
cause is in `entry_stub_hit`, which computed the offset as `g_kv_len + 26` with the comment
`" " + "xnu_entry_stub_caller" (22) + "=0x"` — the key is 21 characters, not 22, so the correct
constant is **25**. It is harmless to both experiments that used it (271's corrupted digits were
positions four and five of `800ac0b4`, `a` and `c`, inside the window either way, and every value is
also reported independently by `entry_kv` and by `_v`), and the constant is corrected in the tree so
the next run's window starts at the first digit. It is recorded here because it was found the way the
project's other instrument defects were found: **the report's own numbers disagreed with each other
before anything else did.**

## Safety

One run, one report, `fastboot boot` only, nothing flashed. The run reports
`persistent_write_attempted=0x00000000`, every contract's `failure_mask=0x00000000`, and the device
returned to Android on its own. The payload's watchdog and preflight are unchanged from the runs that
recovered themselves on 2026-09-17.

## What is next

`host_notify_init` is itself a stub (`out/stage90/xnu_arm_entry_realstubs.c:233`), so the next step is
the object that defines it: `osfmk/kern/host_notify.c`, `manifest` entry to be measured before the
link. From there the walk returns into `kernel_bootstrap` and does not leave it for a while — its next
callees are `mac_policy_init` (`8000e144`), then `ipc_init` (`8000e154`), then `mapping_free_prime`
(`8000e170`) — and the object at the end of that stretch is `bsd_init`, where "XNU loads, enters the
operating system and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above OSFMK_KERN_MK_TIMER_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The two lines that carry the result are the last two of the entry image's report:

```
 real XNU entry stub_hit=host_notify_init
 xnu_entry_stub_caller=0x8000e138
```

and `tools/host_resolve_entry_addr.sh 0x8000e138` resolves that against
`out/stage90/xnu_arm_entry.elf` to `kernel_bootstrap+0x238`.
