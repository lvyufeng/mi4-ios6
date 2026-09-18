# Experiment 274 — `ipc_bootstrap` returns, and the walk's next stop is `mac_policy_init`

**Step:** link the object that defines `host_notify_init` — `osfmk/kern/host_notify.c`, `manifest:548`,
the name 273's run stopped at — and take the next one-object step.
**Prediction:** `stub_hit=mac_policy_init`, with the caller at **`kernel_bootstrap+0x248`** — because
`host_notify_init` is reached by a tail call and returns *into `kernel_bootstrap`*, whose next calls
are a real debug-string print and then `mac_policy_init`.
**Result:** exactly that. Resolved 3, added 0, 843 → 840 undefined. The run stopped at
`stub_hit=mac_policy_init` with `xnu_entry_stub_caller_v=0x8000e148` = **`kernel_bootstrap+0x248`**,
zero aborts, and a second run byte-identical in every report line. **`ipc_bootstrap` returned** — the
whole Mach IPC bootstrap ran on this device from end to end. The run's buffer echo is also *mangled*,
in a way that is the third face of 271's open question and is recorded here as a measurement, not a
mechanism.

## The step, and what the object brought

`osfmk_kern_host_notify.o` is **1616 bytes of text, 0 of data, 364 of bss, 18 definitions and 15
references**. It resolved **3** (`host_notify_init`, `host_notify_port_destroy`,
`host_request_notification`) and added **0** — the 258/260/263/266/268 shape, and the cheapest kind of
step. Thirteen of the fifteen references were already real (`lck_grp_attr_setdefault`, `lck_grp_init`,
`lck_attr_setdefault`, `lck_mtx_init_ext`, `lck_mtx_lock`, `lck_mtx_unlock`, `lck_spin_lock`,
`lck_spin_unlock`, `ipc_kobject_set_atomically`, `panic`, `zalloc`, `zfree`, `zinit`); the two that
were not (`ipc_port_release_sonce`, `mach_msg_send_from_kernel_proper` — the latter added by 273 one
step earlier) were already stubs. Five of the definitions matter to the link, and two of those
(`host_notify_calendar_change`, `host_notify_calendar_set`) are referenced by nothing in this image:
250's shape again — what an object *defines* is not what the link *needs*.

The function is one inlined loop and five calls, with **no branch anywhere in the object**:

```
queue_init(&host_notify_queue[0]) and [1]     ; inlined, four instructions, no call
bl lck_grp_attr_setdefault(&host_notify_lock_grp_attr)
bl lck_grp_init(&host_notify_lock_grp, "host_notify", &host_notify_lock_grp_attr)
bl lck_attr_setdefault(&host_notify_lock_attr)
bl lck_mtx_init_ext(&host_notify_lock, &host_notify_lock_ext, ...)
bl zinit(16, 65536, 256, "host_notify")       ; 16 = sizeof(struct host_notify_entry)
```

Nothing in it can stop, so the prediction was not about the function at all: it was about where
control goes when the function *returns*. Its `zinit` ceiling is 64 KB — the third this walk has
computed from a `sizeof` it read itself, after 271's 1.4 MB `semaphore_max` and 273's 416 KB
`mk_timer`.

## The tail call, and why the prediction is offset-first

Experiment 273 measured it: `mk_timer_init` was the last call in `ipc_bootstrap` proper, and the tail
`b host_notify_init` was reached with `ipc_bootstrap`'s own return address in `lr` (`0x8000e138`). So
`host_notify_init` returning lands **in `kernel_bootstrap`**, not in `ipc_bootstrap` — the first time
this walk has come back *out* of a function since 265 entered `ipc_bootstrap`. From there the built
line is straight:

```
8000e138: movw/movt r0, <a debug string>
8000e140: bl kernel_debug_string_early      ; real code, and 273's run already passed it
8000e144: bl mac_policy_init                ; a stub
```

so the stop is `mac_policy_init` at **`kernel_bootstrap+0x248`**, written as an offset first because
five steps in a row have moved absolutes and none has moved an offset. In this build the absolute is
`0x8000e148` — the new object was laid down after `startup.o`, so nothing before `kernel_bootstrap`
moved either.

## What the run measured

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `mac_policy_init` | the stub's own write, and the name `kernel_bootstrap+0x248` predicts |
| `xnu_entry_stub_caller_v` | `0x8000e148` | = `kernel_bootstrap+0x248`, the prediction |
| `xnu_entry_kv_written` | `0x60` | 96 bytes the code believes it recorded |
| `xnu_entry_kv_in_dram` | `0x84` | 132 = 96 + 36 (`entry_kv` from the epilogue), the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_why_byte` | `0x61` | `'a'`, the epilogue's reason string |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken at all |
| `xnu_entry_stub_caller_digits` | `0x33` | 51 — the offset in `g_kv_buf` the digits belong at |

Build: 843 → 840 undefined, 768 → 765 function stubs, storage unchanged at 75, text 936996 → 938308,
image bytes 1048440 **unchanged** (the growth fitted the linker script's alignment padding — the
258/260/263/266/268 shape), `.bss` `0x800ff6c8` .. `0x80134808`.

**Two runs of this build were identical in every report line**, including all 661 words of the code
dump; the only differences between the two logs are the 32 timebase timestamps. And 661 of 661
instruction words of `entry_kv` through `entry_stub_hit` match the linked ELF **again**, in this
build's own addresses — the third build in which memory has been faithful to the file.

`ipc_bootstrap` returning is the milestone this step was for. Everything that was stubbed when 265
entered it — `ipc_space_create_special` twice, `mig_init`'s 17 descriptors, `ipc_table_init`,
`ipc_voucher_init`, `ipc_importance_init`, `semaphore_init`, `mk_timer_init`, `host_notify_init` —
has now run on the hardware.

## The run's buffer echo is mangled, and what that measures

The last block of the report is written by the epilogue as `entry_write("MI4IOS6_STAGE90_XNU real XNU
entry")` followed by `entry_write(g_kv_buf)`, and in this run it reads:

```
MI4IOS6_STAGE90_XNU real XNU entry8000e148t=mac_policy_init
 xnu_entry_stub_caller=0x
```

The reconstruction is exact and worth doing byte by byte. The buffer's first record, written during
the run, is `" stub_hit=mac_policy_init\n"`. Delete its first eight characters (`" stub_hi"`), put this
run's eight caller digits in their place, and the result is `"8000e148t=mac_policy_init\n"` — which is
what the console shows, character for character. So eight bytes were overwritten **in place** at
`g_kv_buf[0..7]`, by the eight digits of `0x8000e148` in their correct order. The digits' own offset — `g_stub_caller_digits = 0x33` = 51, the field the same report
prints — was never written, so the C-string echo stopped at the first zero byte there.

Three roads agree about the second half of that: the echoed text ends at `...=0x`, the echo *stopping*
is the zero at 51, and `_w0`/`_w1` — the epilogue's `entry_word_at` reads of `&g_kv_buf[51]` and
`[55]` — are both 0, where 273's build read the stored digits at exactly those two words. So this is
not a console artifact: the bytes at the digits' offset are zero *and* the bytes that should not be
there are at the buffer's start.

The instructions that do this are in memory **exactly as the linker wrote them** — 661 of 661 words
match the ELF, including

```
800020c0:  add   lr, r2, #3        ; r2 = 48 = the index after the key
800020c8:  add   lr, r5, lr        ; r5 = g_kv_buf
8000210c:  sub   lr, lr, #1
8000212c:  strb  r4, [lr, #1]!     ; the digit store, pre-increment, eight times
```

— and `kv_written`/`kv_in_dram` are index values the code *computes* (`add r1, r2, #12` and a store)
rather than accumulates, so they are 96 and 132 whichever way the digit store behaves. **Which
instruction's effect differs from what memory says is not measured and is not claimed.** What is
measured is the shape of the difference, and it is 271's open question with a third face: there the
digit *arithmetic* was wrong (`0x30 + d` where the code computes `0x57 + d`) and the addresses right;
here the arithmetic is right and the address is not, twice, in the two `entry_kv` calls that run while
XNU's page tables are live.

One road this reading does *not* have, and it is worth naming because 271 and 272 each had it: the
echo stopped at 51, so the epilogue's own `entry_kv` call (`xnu_entry_stub_caller_e`, `g_kv_len` 96 →
132) is in the buffer but not in the console, and nothing in this run reads the *words* at
`g_kv_buf[0..7]`. A probe that reads those two words beside the existing `_w0`/`_w1` would make "the
digits are at 0" a two-road claim instead of one.

## Safety

Two runs, `fastboot boot` only, nothing flashed. Both returned the device to Android on its own,
`persistent_write_attempted=0x00000000`, every contract's `failure_mask=0x00000000`, and the payload's
watchdog and preflight are unchanged from the runs that recovered themselves on 2026-09-17.

## What is next

`mac_policy_init` is `security/mac_base.c` (`manifest:664`), `security_mac_base.o` — **10087 bytes of
text, 1280 of data, 2120 of bss, 115 definitions and 72 references** — the first object this walk
would take out of `osfmk/` and the BSD layer into the security framework, and by far the largest
single step since 267. Behind it in `kernel_bootstrap`'s own line: `ipc_init` (real since 265, whose
object is already linked) and then `mapping_free_prime` (`8000e170`), and after that stretch `bsd_init`
— where "XNU loads, enters the operating system and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above OSFMK_KERN_HOST_NOTIFY_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x8000e148`, and
`tools/host_resolve_entry_addr.sh 0x8000e148` resolves it against `out/stage90/xnu_arm_entry.elf` to
`kernel_bootstrap+0x248`. The stop is named by the stub's own write — in this run the `stub_hit=` key
is one of the eight characters the digits overwrote, so the name arrives as `...8000e148t=mac_policy_init`,
and the identification of the hit rests on the name plus the caller's value, both of which the
disassembly of that call site agrees with.
