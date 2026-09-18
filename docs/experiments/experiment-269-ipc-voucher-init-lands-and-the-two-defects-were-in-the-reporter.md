# Experiment 269 — `ipc_voucher_init` lands, and the two defects were in the reporter

**Step:** link `osfmk/ipc/ipc_voucher.o` into the entry image.
**Prediction:** `stub_hit=ipc_importance_init`, at `ipc_bootstrap+0x188`.
**Result:** `stub_hit=ipc_importance_init` — the twelfth prediction in a row — but only after
finding and fixing two defects, **both of them in this image rather than in XNU**: the data-abort
handler's own report path could fault and re-enter itself (313 entries), and `entry_epilogue`'s
`why` line had been printing garbage.

## The change

One object. `osfmk/ipc/ipc_voucher.c` (manifest:522) is the largest single step since 257: 12335
bytes of text, 120 of data, 2200 of bss, 73 definitions and 35 references. A voucher is a
first-class Mach object — a hash table, two zones, an attribute-manager registry, and the whole
`mach_voucher_*` / `host_*_mach_voucher_*` MIG surface, 22 of whose definitions the image had been
carrying as stubs since 265.

**Resolved 22, added 4.** The four added names — `ipc_port_make_send_locked`,
`ipc_port_make_sonce_locked`, `ipc_port_dealloc_special`, `ipc_object_translate` — are all reached
from `convert_*_to_voucher` and friends, none of them from `ipc_voucher_init`. The other nine of the
object's undefined references (`task_max`, `ipc_port_alloc_special`, `ipc_port_nsrequest`,
`ipc_port_release_send`, `kernel_task`) were already stubs, because a stub exists exactly when
something in the link references the name.

The prediction and its reasoning are in `build_entry.sh`'s 269 block, including the thing this step
can *not* measure: `task_max` is a storage stand-in of size 4 whose value is 0, so
`ipc_voucher_max = (task_max + thread_max) * 2` computes 3072 where the real kernel computes 4096,
and the resulting zone ceiling is 192 KB instead of 256 KB — silently smaller, invisible to any
`--wrap`, because the read is an `ldr` and not a call. The repair is `osfmk/kern/task.o`, and it is
its own step.

| | before (268) | after the link | after this step's diagnostics |
| --- | --- | --- | --- |
| undefined | 860 | 842 | 842 |
| function stubs | 784 | 766 | 766 |
| storage stand-ins | 76 | 76 | 76 |
| text | 904484 | 915940 | 916612 |
| image bytes | 1015520 | 1015640 | 1015640 |
| bss end | 0x8012a508 | 0x8012c648 | 0x8012c6c8 |
| args | +1232896 | +1236992 | +1236992 |
| headroom | 1917688 | 1915320 | 1915192 |

## The first run: the prediction held, and the report was unreadable

```
MI4IOS6_STAGE90_XNU real XNU entry: 47
 xnu_entry_kv_written=0x00001fe4
 xnu_entry_kv_in_dram=0x00001fe4
 xnu_entry_kv_dropped=0x00000011
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_importance_init
 xnu_entry_stub_caller xnu_entry_data_abort_dfar xnu_entry_data_abort_dfar xnu_entry_data_abort_dfar ...
```

The stop was right. Everything else about that report was wrong, and three things in it disagreed
with each other:

- `kv_written` was 8164 — the whole 8192-byte buffer, for a boot that reaches one stub. 268's
  equivalent run wrote 61 bytes.
- `kv_dropped` was 17, which is *exactly* the number of `entry_kv` calls `fleh_dataabt` made.
- The blob was some three hundred copies of `xnu_entry_data_abort_dfar` with **no `=` and no value
  after any of them**, and the `why` line read `47` where the string is `a symbol this image does
  not provide was called`.

The pairing of the second and third says what happened: the handler ran, and it ran many times, and
each entry stopped between writing its first key and writing that key's value. The run was
reproduced identically a second time, and reading the image's own literals and disassembling
`entry_stub_hit`, `entry_epilogue`, `entry_kv` and `entry_write` ruled out a build or verifier error.

## Measuring the storm instead of arguing about it

Four `.bss` counters in the handler — entries, and what the first entry saw — turned the reading
into a measurement:

```
 xnu_entry_abort_entries=0x00000139
 xnu_entry_abort_first_dfar=0x0000003f
 xnu_entry_abort_first_pc=0x800020dc
 xnu_entry_abort_first_kv_len=0x00000034
```

313 entries. The first fault's `dfar` was `0x3f` = `first_kv_len + 11`, which is exactly the address
of the value record's trailing newline store *if the buffer's base register were zero* — and
`first_kv_len` was 52, i.e. the fault landed right at the start of the value write for
`xnu_entry_stub_caller`, after its key had been copied.

Then a second instrument, this one inside `entry_kv` itself (a `volatile` step number and the
address it was about to use, written before each group of stores), plus `dfsr`, the raw `lr`, the
instruction word at `pc`, and the address the *code* has for the buffer:

```
 xnu_entry_kv_written=0x0000004e        (78 bytes)
 xnu_entry_abort_entries=0x00000002
 xnu_entry_abort_first_dfar=0x0000a69c
 xnu_entry_abort_first_pc=0x80002118
 xnu_entry_abort_first_kv_len=0x00000034
 xnu_entry_abort_first_dfsr=0x00000005
 xnu_entry_abort_first_lr=0x80002120
 xnu_entry_abort_first_insn=0xe7d50000
 xnu_entry_abort_first_step=0x00000004
 xnu_entry_abort_first_step_addr=0x800f8ff3
 xnu_entry_abort_first_kvbuf=0x800f8fbc
 xnu_entry_abort_first_sp=0x800f8f50
```

`0x80002118` is `ldrb r0, [r5, r0]` — the hex-digit table lookup — and `0xe7d50000` is that
instruction word, read back from the image. `dfsr = 5` is "translation fault, section", with the
write bit clear: a **read**. `first_kvbuf = 0x800f8fbc` is the linker's own `g_kv_buf`, so the
globals' addresses are right at runtime. And `dfar = 0xa69c` = `low16(&g_hex) + 8`: the digit table's
own low half plus the first nibble's index.

## Two readings, and they cannot both be true

`dfar = low16(&g_hex) + index` is what the hardware computes if the table's address reached the
register with only its low half — `movw r5, #0xa694` executed, `movt r5, #0x800c` did not:

```
800020c8: movw  r5, #0xa694
800020cc: str   r6, [ip, #8]
800020d0: add   lr, r4, lr
800020d4: movt  r5, #0x800c
 ...
80002118: ldrb  r0, [r5, r0]
```

But the two instructions that follow the pair are straight-line, the fall-through is the only way
into the loop, and every path from the function's entry passes through both. So the register cannot
have been half-set — unless the address was right and `dfar` is not describing this fault at all,
which is what an imprecise abort on Strongly-ordered memory looks like: the project runs `SO_ONLY`,
and for an imprecise abort `DFAR` is unpredictable. Against that: the fault status says *read* and
the canon run's faulting `pc` was a *store*, so at least one of the two runs reported a status and a
pc that cannot belong together, and this run's `pc`/`lr` pair (`pc + 8`) is self-consistent.

Also against "the address was right": if the *page* were unmapped, the same address reads fine after
the teardown has the mmu off, and `entry_write_kv`'s own digit table — a second `.text` array a few
hundred bytes away — is read that way on every line of the report.

The honest state of it: **the fault is real and reproducible, the two readings disagree, and the
instrument that reported it is the thing in doubt.** That is the same shape as the twenty-four
entries in `mi4-measurement-defects`, and it is why the fix below does not depend on resolving it.

## What closed the step

**The digits are computed arithmetically.** `(d < 10 ? '0' + d : 'a' + d - 10)` instead of
`g_hex[d]` — one line in a reporting path, and it removes the only read in that path with an
address the code has to form. The run that followed:

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000040
 xnu_entry_kv_in_dram=0x00000040
 xnu_entry_kv_dropped=0x00000000
 xnu_entry_abort_entries=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_importance_init
 xnu_entry_stub_caller=0x800abfcc
```

**Zero aborts.** 64 bytes written, nothing dropped: ` stub_hit=ipc_importance_init\n` (30) plus
` xnu_entry_stub_caller=0x800abfcc\n` (34). The step is done and the report is clean.

`0x800abfcc` is `ipc_importance_init`'s return address — the instruction after the `bl` that stops
the run:

```
800abfc0: bl ipc_table_init
800abfc4: bl ipc_voucher_init
800abfc8: bl ipc_importance_init     <- the stop
```

`ipc_bootstrap+0x188`, one call past 268's `+0x184`. Said as an offset because the absolute address
moved twice here — the diagnostics below pushed `ipc_bootstrap` from 0x800abe28 to 0x800abfcc — and
268 had already established that the offset is the durable form.

## The two defects this step found and fixed

**1. The data-abort handler's report path could fault, and a fault in it is not one bad line — it
is three hundred.** The handler recorded its facts and then made 17 `entry_kv` calls. One of those
records faulted, which re-entered the handler, which wrote another record, which faulted. The
handler now writes everything into `.bss` and goes straight to the epilogue, which reports all of it
through `entry_write_kv` after the mmu is off — the one reporting path in this image with a record
of working, and the reason this run's first fault was readable at all. A second entry can no longer
happen; if one does, it is counted, named, and leaves.

**2. `entry_epilogue`'s `why` line had been printing garbage.** It read `47` in one run and nothing
at all in another, where the string is 47 characters of English. The parameter is passed correctly
and the compiler does the right thing with it — `str r0, [sp, #4]` on entry, `ldr r0, [sp, #4]` for
the report — so what the run says is that the *slot* did not survive. The string is now copied into
`.bss` at entry and the pointer is printed beside it:

```
 xnu_entry_why=0x800ca2b8
 xnu_entry_why_byte=0x00000061        ('a')
```

The pointer was always sane. The fix is to stop reading it from a slot that is not.

Both defects are in this project's own code, both were invisible while the run ended in a stub for
a reason that looked plausible, and both are the same class as the 267 lesson: **a reporting path
that can fail its caller turns one unknown into two.**

## Safety

25 of the run's contracts report `persistent_write_attempted=0x00000000`, none reports `1`, nothing
is flashed — every run is `fastboot boot` of a non-persistent image — and the device returned to
Android on its own after each of the four runs this step made.

## What is next

- `ipc_importance_init` is the stop: `osfmk/ipc/ipc_importance.c`. In call order after it,
  `semaphore_init` (`osfmk/kern/sync_sema.c`, manifest:587) and `mk_timer_init`
  (`osfmk/kern/mk_timer.c`, manifest:572), then `ipc_bootstrap` returns at `+0x18c`
  (`pop {r4, r5, fp, lr}`), and `kernel_bootstrap` continues with `host_notify_init`
  (`osfmk/kern/host_notify.c`, manifest:548) and `mac_policy_init`.
- `osfmk/kern/task.o`, so that `task_max` stops being a zero-valued stand-in — a value used in
  arithmetic rather than as a pointer, and the only way to see it is to link the real definition.
- The abort-measurement question above, as its own experiment: one run that makes the handler report
  `&g_hex` as the code has it, alongside a fault that genuinely reaches the handler. Two readings
  that cannot both be true is a measurement problem, not a mystery to be narrated.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The stop is `stub_hit=ipc_importance_init` in the results blob, with
`xnu_entry_stub_caller` equal to `ipc_importance_init`'s return address, and every
`xnu_entry_abort_*` field zero.
