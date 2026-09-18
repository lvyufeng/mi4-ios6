# Experiment 266 — `ipc_space_create_special` Completes, and the Stop Is `mig_init`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

265's stop was `ipc_space_create_special`, and `osfmk/ipc/ipc_space.c` (manifest:520) —
`out/xnu_kernel_obj/osfmk_ipc_ipc_space.o` — is the object that defines it: 1008 bytes of text,
12 of bss, nine definitions and 17 references, **13 of them already satisfied**.
**`OSFMK_IPC_IPC_SPACE_OBJ` is the change.**

Its four new names are `ipc_right_destroy`, `ipc_right_terminate`, `ipc_table_alloc` and
`ipc_table_free` — and **none of them is on this call's path**. They belong to `ipc_space_terminate`
and `ipc_space_destroy`, which the bootstrap never reaches. The function this step is about is 26
instructions and calls exactly two things:

```
1c0  push {r4, r5, fp, lr}
1d4  bl zalloc             -> 0x8006f284  real
1f4  bl lck_spin_init      -> 0x80012538  real
224  pop {r4, r5, fp, pc}
228  mov r0, #6            <- the out-of-memory path, also just a return
```

So it completes, and `ipc_bootstrap` calls it **twice** — once for `ipc_space_kernel`, once for
`ipc_space_reply` — so both complete.

It is also the step that retires the address-operand hazard 265 recorded. This object defines
`ipc_space_kernel` and `ipc_space_reply` as `B` symbols, so the two pointers that `ipc_bootstrap`
passes stop being the addresses of stub code in the same move that makes the callee real. The
prediction was made on the assumption that the hazard would show up as a change of *shape* in the
counts; it did: storage stubs fell by three (`ipc_space_zone`, `ipc_space_kernel`, `ipc_space_reply`,
all now real storage) while function stubs rose by three.

## The prediction

`ipc_bootstrap` is unchanged at `0x800abc04`, and the two `ipc_space_create_special` calls are now
real code at `0x800ac04c`. After the second one, the straight line is

```
800abd7c  bl mig_init    ; STUB at 0x800adc68
800abd80  (the instruction after it)
```

**The prediction: `stub_hit=mig_init`, `xnu_entry_stub_caller=0x800abd80`** — `caller - 4` =
`0x800abd7c` = `ipc_bootstrap+0x178`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000035
 xnu_entry_kv_in_dram=0x00000035
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mig_init
 xnu_entry_stub_caller=0x800abd80

No errors detected
```

`0x800abd80` resolves to `ipc_bootstrap+0x17c`, whose `caller - 4` is
`800abd7c: bl 800adc68 <mig_init>` — the prediction, address for address, for the **ninth step in a
row**. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x35`, the lowest value this frontier has recorded: the KV buffer holds a
fixed 45-byte prefix followed by the stub's name verbatim, and `mig_init` is eight characters.

## Cost

| | exp-265 | now |
| --- | --- | --- |
| undefined | 627 | **627** (4 resolved, 4 added) |
| function stubs | 550 | **553** |
| storage stubs | 77 | **74** |
| entry text | 795556 B | **796676 B** (+1120) |
| entry image | 900688 B | **900688 B**, unchanged |
| entry `.bss` end | 0x8010b488 | **0x8010b3c8** |
| derived `args` offset | +1101824 | **+1101824**, unchanged |
| `topOfKernelData` | +3145728 | **+3145728**, unchanged |
| headroom | 2050936 B | **2051128 B** |
| payload text | 1392906 B | **1392906 B**, unchanged |

A true no-op in the derived layout: the undefined count is unchanged at 627, the image is byte-for-byte
the same size, the `args` offset and `topOfKernelData` are unchanged, and even the payload's own size
is identical — only its contents differ. `.bss` ended 192 bytes *lower* than before, because three
zero-size storage stand-ins were replaced by the object's twelve real bytes of `B` storage.

The four added names are worth carrying forward as a block: `ipc_table_alloc` and `ipc_table_free` come
from `osfmk_ipc_ipc_table.o`, which also defines `ipc_table_init` — the third call in
`ipc_bootstrap`'s tail — and `ipc_right_destroy`/`ipc_right_terminate` from `osfmk_ipc_ipc_right.o`.
So the step for `ipc_table_init` will arrive with two of this step's four names already in hand.

## What is next

`mig_init`. Behind it, the rest of `ipc_bootstrap` is still a measured list of six, every target a
stub, and each of them one object from the manifest:

```
800abd7c  bl mig_init             <- STUB   osfmk/kern/ipc_kobject.c   (not the ipc/ directory)
800abd80  bl ipc_table_init       <- STUB   osfmk_ipc_ipc_table.o
800abd84  bl ipc_voucher_init     <- STUB   osfmk_ipc_ipc_voucher.o
800abd88  bl ipc_importance_init  <- STUB   osfmk_ipc_ipc_importance.o
800abd8c  bl semaphore_init       <- STUB   osfmk_kern_sync_sema.o
800abd90  bl mk_timer_init        <- STUB   osfmk_kern_mk_timer.o
800abd94  b  host_notify_init     <- STUB   osfmk_kern_host_notify.o   (a tail call)
```

and then `kernel_bootstrap` continues with `mac_policy_init` at `0x8000dc84` — the run-in to `bsd_init`.

`mig_init`'s home is worth stating because the name points at the wrong directory: the name says
`ipc/`, the manifest line says
`osfmk/kern/ipc_kobject.c` (manifest:551, `osfmk_kern_ipc_kobject.o`, 2404 bytes of text, 68 of data,
12376 of bss, 50 references). `osfmk/kern/ipc_mig.c` is in the manifest too (line 552) and is a
different object. The cheap check is the one this project already uses — find the object that defines
the name, before reading any source.

The tail call is the one to watch. `host_notify_init` is entered with a `b`, not a `bl`, so when it
stops, the stub's own `lr` is *`ipc_bootstrap`'s caller* — `kernel_bootstrap+0x238` — and not a site
inside `ipc_bootstrap`. Experiment 252 met the same distinction from the other side
(`vm_object_bootstrap` ended in `b lck_mtx_init` / `b zone_change`). The prediction for that step will
have to be written against `kernel_bootstrap`, not against `ipc_bootstrap`.

## Reproduce

```bash
# the object, and that its four new names are off the path
grep -n 'osfmk/ipc/ipc_space.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_ipc_ipc_space.o | awk '{print $2}' | sort -u > /tmp/sp.txt
comm -23 /tmp/sp.txt <(arm-none-eabi-nm --defined-only out/stage90/xnu_arm_entry.elf | awk '{print $3}' | sort -u)

# what ipc_space_create_special itself calls
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_ipc_ipc_space.o \
  | awk '/<ipc_space_create_special>:/{f=1} f{print} f&&/^$/{exit}' | grep -o 'R_ARM_CALL\s*\S*'

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_265.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 4
comm -13 <(sort /tmp/undef_265.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 4
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^800abd7c:/{print; exit}'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x800abd80     # -> ipc_bootstrap+0x17c
```
