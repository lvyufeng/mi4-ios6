# Experiment 277 — `ipc_host_init`, and the largest stub retirement this walk has made

**Step:** link the object that defines `ipc_host_init` — `osfmk/kern/ipc_host.c` (`manifest:550`, and see
the note below on the 549 this walk had been quoting), `osfmk_kern_ipc_host.o`, the name 276's run
stopped at.
**Prediction:** `stub_hit=ipc_port_alloc_special` with the caller at **`ipc_host_init+0x30`** — because
the walk is standing *in* `ipc_host_init`, whose first call is a real `lck_mtx_init` and whose second is
a `bl ipc_port_alloc_special`, which is what XNU's own `ipc_port_alloc_kernel()` expands to.
**Result:** exactly that, and the counts to the unit. **16 resolved, 8 added** — the largest single-step
retirement in this walk. `stub_hit=ipc_port_alloc_special`,
`xnu_entry_stub_caller_v=0x800b7580` = **`ipc_host_init+0x30`**, three roads agreeing, zero aborts.
**The nineteenth prediction in a row.**

## The step, and the object

`osfmk_kern_ipc_host.o` is **3668 bytes of text, 53 of `.rodata.str1.1`, no data and no bss, 21
functions defined and 26 names referenced**, and the three strings in it are the three panics:
`"ipc_host_init"`, `"ipc_processor_init"`, `"ipc_pset_init"`.

Of the 26 names it references, 12 are already real code and 6 already have stand-ins (5 functions, plus
`realhost`, which is a *storage* stand-in of 0x194 bytes rather than a function), so 18 need nothing.
The 16 the object defines that this image currently **stubs** are the ones that retire:

```
convert_host_to_port          convert_port_to_host        convert_port_to_host_priv
convert_port_to_host_security convert_port_to_processor    convert_port_to_pset
convert_port_to_pset_name     convert_pset_name_to_port   convert_pset_to_port
host_get_exception_ports      host_set_exception_ports    host_swap_exception_ports
ipc_host_init                 ipc_processor_enable        ipc_processor_init
processor_set_default
```

and the 8 that are new obligations are all functions, each defined by an object this build has already
compiled — so each stand-in's *size* still comes from its own definition rather than from a guess, which
is the rule since 244:

```
kernel_set_special_port                   osfmk_kern_host.o
ipc_port_copyout_send                     osfmk_ipc_ipc_port.o
mac_exc_associate_action_label            security_mac_mach.o
mac_exc_create_label                      security_mac_mach.o
mac_exc_create_label_for_current_proc     security_mac_mach.o
mac_exc_free_label                        security_mac_mach.o
mac_exc_update_action_label               security_mac_mach.o
mac_task_check_set_host_exception_ports   security_mac_mach.o
```

Build: 868 → 860 undefined, 792 → 784 function stubs, storage unchanged at 76, text 949860 → 953220,
image bytes 1049720 → 1066104, `.bss` `0x80103a00` .. `0x801395c8` (one 16 KB alignment step, and `.bss`
moved with it), headroom 1862200 bytes. `ipc_host_init` is real in the image at `0x800b7550`.

The other half of the object, and the half this walk has still not taken: five of its 21 functions
(`convert_processor_to_port`, `host_self_trap`, `ipc_pset_enable`, `ipc_pset_init`,
`ref_pset_port_locked`) are referenced by nothing in this image, so they are neither real nor stubs —
they are simply absent, which is the shape 276's four unreferenced label functions had. A definition
nothing references is invisible to a delta of two undefined sets.

## The built function, and where it stops being real

`ipc_host_init` is the function the walk is standing in, so this step runs it. The linked disassembly is
the object's instruction for instruction:

```
800b7550 <ipc_host_init>:
800b7550: push {r4, r5, r6, r7, fp, lr}
800b7554: movw/movt realhost, host_notify_lock_grp, host_notify_lock_attr
800b756c: bl 80013ac4 <lck_mtx_init>                    ; REAL - locks.o since 269
800b7570: movw/movt r6, ipc_space_kernel
800b7578: ldr r0, [r6]                                  ; REAL (ipc_space.c, 265)
800b757c: bl 800ceb54 <ipc_port_alloc_special>          ; A STUB  <- `ipc_port_alloc_kernel()`
800b7580: mov r4, r0                                    ; the return address, and the stop
```

So the run executes the lock init, reads `ipc_space_kernel`, and stops on the first instruction that
becomes a stub again. The caller key is `ipc_host_init+0x30`: a `bl`'s return address *inside* the
function entered, the same shape 276 had, and unlike the three tail-call steps before it.

## `realhost`, written for the first time

Two things about the stand-in are worth recording, because this step is the first time real XNU code
**writes to** a storage stand-in rather than reading one — and a mis-sized stand-in is silently
overwritten by the first real user of it, which is the failure this project has been checking against
since 244.

`realhost` is 0x194 (404) bytes, sized from `osfmk_kern_host.o` — which is *not* linked, which is why it
is a stand-in at all. `ipc_host_init` writes `realhost.lock` through `lck_mtx_init`, and then 26 stores
of `IP_NULL`/`NULL` at offsets 144 through 400, whose last one at 400 ends exactly at the stand-in's
last byte. That is the case an undersized stand-in would fail, and the size is not a guess. The eight
`mac_exc_*` calls are on the exception-port paths and are not reached here: the object itself defers
label initialization — its own comment says so — so `realhost.exc_actions[i].label` is set to `NULL`, not
to a MAC label. None of the 8 new obligations was reached.

## What the run measured

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `ipc_port_alloc_special` | the prediction, named by the stub's own write |
| `xnu_entry_stub_caller` | `0x800b7580` | `ipc_host_init+0x30`, read out of `g_kv_buf` |
| `xnu_entry_stub_caller_a` | `0x800b7580` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x800b7580` | the value itself, through the ram-console path |
| `xnu_entry_stub_caller_e` | `0x800b7580` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_stub_caller_digits` | `0x3a` | 58 = 33 + 25, a 22-character name this time |
| `xnu_entry_stub_caller_w0` | `0x62303038` | `800b`, the four bytes in `g_kv_buf` |
| `xnu_entry_stub_caller_w1` | `0x30383537` | `7580`, the next four |
| `xnu_entry_kv_written` | `0x67` | 103 = a 33-byte stub record plus 34- and 36-byte caller records |
| `xnu_entry_kv_in_dram` | `0x8b` | 139 = 103 + 36, the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken |

`tools/host_resolve_entry_addr.sh 0x800b7580` gives `ipc_host_init+0x30`, and `caller-4 = 0x800b757c` is
the `bl 800ceb54 <ipc_port_alloc_special>` itself. Four roads to the same value, all agreeing, and the
report's echo intact:

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_port_alloc_special
 xnu_entry_stub_caller=0x800b7580
 xnu_entry_stub_caller_a=0x800b7580
 xnu_entry_stub_caller_e=0x800b7580
```

661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF again — the sixth build
running.

## A measurement defect on this side of the wire, not the device's

That 661/661 is worth one paragraph, because the first attempt to check it **reported 660 of 661
mismatched** and the difference was in how the check was written, not in the device.

The dump prints each word's *value* as eight hex digits. The first comparison here read those digits
against the image's raw bytes at the same offset — which are little-endian. The word at `0x80002024` is
`0xe305c480`; the image holds `80 c4 05 e3`. Comparing the number to the number gives **0 mismatches of
661**.

This is the project's oldest recurring failure with a new face: *a reported number can be an artifact of
how it was taken rather than of what it measured* — and here the report was mine. It is recorded because
the wrong reading (`the entry image's memory is no longer faithful`) is exactly the kind of conclusion
this walk has been careful not to draw from one road, and because the corrected reading is the sixth
consecutive confirmation of the opposite. It also leaves the open question alone: 271/272/274's
fetch-or-execution anomaly is about what an instruction's *effect* is during the run, not about what the
bytes are.

## A manifest citation one line late

`ipc_host.c` is line **550** of `out/xnu_arm_manifest.txt`; 549 is `osfmk/kern/ipc_clock.c`. 275's and
276's prose both say 549, and that is this walk's one manifest citation a line early — made while
predicting, not while measuring, and corrected here. Everything earlier in the sequence checks out
(`mac_base.c` 664, `mac_label.c` 669, `host_notify.c` 548, `ipc_kobject.c` 551, `ipc_mig.c` 552).

## Safety

One run, `fastboot boot` only, nothing flashed. `persistent_write_attempted=0x00000000` in all 25
places, `failure_mask=0x00000000` in all 87 contracts, zero aborts, and the device returned to Android
on its own (`sys.boot_completed=1`).

## What is next

`ipc_port_alloc_special` is `osfmk/ipc/ipc_port.c` — the next step, and by XNU's own linkage the
largest single object on this path: it defines `ipc_port_alloc_special`, `ipc_port_make_send`,
`ipc_port_copy_send`, `ipc_port_release_send`, `ipc_port_copyout_send` and much else, so several of the
names `ipc_host.o` left as stubs are in it. It also stops `ipc_host_init` immediately, which means the
three host special ports (`HOST_SECURITY_PORT`, `HOST_PORT`, `HOST_PRIV_PORT`) are the thing this walk is
about to build. Behind that in `kernel_bootstrap`'s line: `mapping_free_prime` (`8000e170`, real),
`machine_init`, `clock_init`, `ledger_init` — and after that stretch `bsd_init`, where "XNU loads, enters
the operating system and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above OSFMK_KERN_IPC_HOST_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x800b7580`;
`tools/host_resolve_entry_addr.sh 0x800b7580` resolves it against `out/stage90/xnu_arm_entry.elf` to
`ipc_host_init+0x30`.
