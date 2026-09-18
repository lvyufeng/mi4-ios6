# Experiment 279 — `ipc_mqueue_init`, and a stop that reports the caller key 278 already reported

**Step:** link the object that defines `ipc_mqueue_init` — `osfmk/ipc/ipc_mqueue.c` (`manifest:514`),
`osfmk_ipc_ipc_mqueue.o`, the name 278's run stopped at.
**Prediction:** `stub_hit=klist_init` with the caller at **`ipc_port_alloc_special+0x94`** — *the same
value 278 measured*, because the last instruction of `ipc_mqueue_init` is a tail call, so the stub's `lr`
is the one its own `pop` restored rather than the address of the `b`.
**Result:** exactly that, both halves. **5 resolved, 9 added** — and the five retired are precisely the
five names 278 obliged. `stub_hit=klist_init`,
`xnu_entry_stub_caller_v=0x800ba300` = **`ipc_port_alloc_special+0x94`**, four roads agreeing, zero
aborts. **The twenty-first prediction in a row.**

## The step, and the object

`osfmk_ipc_ipc_mqueue.o` is **5436 bytes of text, 8 of bss, 238 of rodata, 47 definitions and 46
references**. The 8 bytes of bss are the two counters `ipc_mqueue_full` and `ipc_mqueue_rcv`, 4 each; the
strings are the `"ipc_mqueue_send"` panics and the `"Unknown mqueue type 0x%x: likely memory
corruption!"` panic.

The five retired are exactly the five names the previous step obliged — `ipc_mqueue_init`, `_deinit`,
`_changed`, `_destroy_locked`, `_override_send` — which is the cleanest kind of step this walk takes: the
previous step's own additions coming good, one experiment later. The nine added are all functions, all
defined by objects this build has already compiled:

```
ipc_kmsg_dequeue         ipc_kmsg_enqueue_qos     ipc_kmsg_override_qos
ipc_kmsg_rmqueue         ipc_kmsg_queue_next      ipc_kmsg_delayed_destroy
ipc_kmsg_copyout_size    knote_vanish             mach_msg_receive_continue
```

Seven of them are out of `osfmk/ipc/ipc_kmsg.c` — the first names this walk has obliged out of it — one
more is out of `bsd/kern/kern_event.c`, and one is out of `osfmk/ipc/mach_msg.c`.

Build: 859 → 863 undefined, 785 → 789 function stubs, storage unchanged at 74, text 962244 → 968100,
image bytes 1066104 → 1082488 (one 16 KB alignment step), `.bss` `0x80107a00` .. `0x8013d548`,
headroom 1845944.

## The built function, and the stop

`ipc_port_alloc_special` calls `ipc_mqueue_init(port + 16, FALSE, NULL)`, and the object's whole
`is_set == FALSE` path is real:

```
800ba680 <ipc_mqueue_init>:
800ba680: push {r4, r5, fp, lr}        ; saves lr = `ipc_port_alloc_special+0x94`
800ba688: cmp r1, #0 / beq +0x24       ; is_set is FALSE, so the set arm is skipped
800ba69c: bl 800a8b10 <waitq_set_init> ; the `is_set == TRUE` arm, not taken
800ba6b0: bl 800a758c <waitq_init>     ; REAL (waitq.o, linked in 267)
          ... `ipc_kmsg_queue_init` inlined, imq_seqno/imq_msgcount 0,
              imq_qlimit = 0x50000 = MACH_PORT_QLIMIT_DEFAULT << 16, imq_fullwaiters FALSE
800ba6d4: pop {r4, r5, fp, lr}         ; lr = `ipc_port_alloc_special+0x94` again
800ba6d8: b 800d298c <klist_init>      ; A TAIL CALL, and a stub
```

`waitq_init`'s own body calls only `hw_lock_init` and `waitq_lock`, both real, so there is no stub
between the entry of `ipc_mqueue_init` and its tail.

## The caller key repeats, and this time the prediction said so in advance

`klist_init` is `bsd/kern/kern_event.c`, and the last instruction of `ipc_mqueue_init` is a `b`, not a
`bl`. So the stub's `lr` is the one `ipc_mqueue_init` inherited from its own caller and carried through
its `pop`: **`ipc_port_alloc_special+0x94` — the address 278's run reported.**

```
278:  stub_hit=ipc_mqueue_init   caller = 0x800ba300   (the `bl` at +0x90, into this function)
279:  stub_hit=klist_init        caller = 0x800ba300   (the same value, through a tail call)
```

`caller-4` resolves to `800ba2fc bl ipc_mqueue_init` for both stops — and in 279 the function that `bl`
calls has **already returned**. So `caller-4` alone no longer says which function's body the run is in,
and the stub's own name is the only discriminator between two consecutive stops. That is 275's shape
exactly, and it is the second time this walk has met it: 273/274/275 were three tail-call steps in a row
that all reported `kernel_bootstrap`, 276 and 277 and 278 reported addresses inside the function entered,
and 279 returns to the tail-call form. The prediction was written as the *value* as well as the name
precisely because the value is expected to repeat.

## What the run measured, and what ran

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `klist_init` | the prediction, named by the stub's own write |
| `xnu_entry_stub_caller` | `0x800ba300` | `ipc_port_alloc_special+0x94`, read out of `g_kv_buf` |
| `xnu_entry_stub_caller_a` | `0x800ba300` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x800ba300` | the value itself, through the ram-console path |
| `xnu_entry_stub_caller_e` | `0x800ba300` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_stub_caller_digits` | `0x2e` | 46 = 21 + 25, a 10-character name |
| `xnu_entry_stub_caller_w0` | `0x62303038` | `800b`, the four bytes in `g_kv_buf` |
| `xnu_entry_stub_caller_w1` | `0x30303361` | `a300`, the next four |
| `xnu_entry_kv_written` | `0x5b` | 91 = a 21-byte stub record plus 34- and 36-byte caller records |
| `xnu_entry_kv_in_dram` | `0x7f` | 127 = 91 + 36, the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken |

661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF again — the eighth build
running.

**What ran by completing:** the port object's message queue was initialized with a real
`waitq_init(port + 16, SYNC_POLICY_FIFO)` — the waitq machinery 267 linked and 268 measured, called here
for the first time from a *port allocation* rather than from the walk's own bootstrap — and
`ipc_mqueue_init` then stopped one instruction from the end, on `klist_init`.

## Safety

One run, `fastboot boot` only, nothing flashed. `persistent_write_attempted=0x00000000` in all 25
places, `failure_mask=0x00000000` in all 87 contracts, zero aborts, and the device returned to Android
on its own (`sys.boot_completed=1`).

## What is next

`klist_init` is `bsd/kern/kern_event.c` — the next step, and the first time this walk has been stopped by
a *BSD* name rather than an `osfmk/` one. It is the last call in `ipc_mqueue_init`, so behind it is the
rest of `ipc_port_alloc_special` (`mov r0, r4` / `pop`), then two more `ipc_port_alloc_kernel` calls and
the three `kernel_set_special_port` calls that finish `ipc_host_init` — the three host special ports —
and then the walk returns to `ipc_init`, to `kernel_bootstrap`, and onward to `mapping_free_prime`
(`8000e170`, real), `machine_init`, `clock_init`, `ledger_init`, and after that stretch `bsd_init` —
where "XNU loads, enters the operating system and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above OSFMK_IPC_IPC_MQUEUE_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x800ba300`;
`tools/host_resolve_entry_addr.sh 0x800ba300` resolves it against `out/stage90/xnu_arm_entry.elf` to
`ipc_port_alloc_special+0x94`, and the stop is named by ` stub_hit=klist_init`. The two are consistent in
the only way they can be: `800ba6d8 b 800d298c <klist_init>` is a tail call, so the stub sees the `lr`
that `ipc_mqueue_init`'s own `pop` restored.
