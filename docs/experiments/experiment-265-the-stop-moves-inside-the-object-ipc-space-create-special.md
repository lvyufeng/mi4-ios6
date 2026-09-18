# Experiment 265 — The Stop Moves Inside the Object: `ipc_space_create_special`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

264's stop was `ipc_bootstrap`, at `osfmk/ipc/ipc_init.c` (manifest:512) —
`out/xnu_kernel_obj/osfmk_ipc_ipc_init.o`, 804 bytes of text, 24 of data, 300 of bss, three
definitions (`ipc_bootstrap`, `ipc_init`, `ipc_thread_call_init`) and 27 references, of which eleven
were already satisfied. **`OSFMK_IPC_IPC_INIT_OBJ` is the change.**

It is a small object, but it is the first step in this line whose **new names are a subsystem rather
than a handful of functions.** The 16 added are:

```
entered   mig_init   ipc_table_init   ipc_voucher_init   ipc_importance_init
          semaphore_init   mk_timer_init   host_notify_init   ipc_host_init
          ipc_space_create_special   ipc_importance_thread_call_init
statics   ipc_space_zone   ipc_kmsg_zone   ipc_object_zones
          ipc_port_multiple_lock_data   ipc_port_timestamp_data   ipc_space_reply
```

Ten functions and six statics — the Mach IPC subsystem's whole initialisation surface, in one step.

**And that changes the shape of the prediction.** Every step since 257 has predicted the *next line of
`kernel_bootstrap`*: the target completes, returns, and the run stops at the following stub. This one
cannot. `ipc_bootstrap` does not return after a few calls — it calls eight functions this image does
not have, and the first of them stops the run **inside the function the step just linked**. The
prediction is therefore `ipc_bootstrap+0x16c`, not `kernel_bootstrap+0x244`.

## The prediction

`ipc_bootstrap` is 214 instructions: 22 calls — 21 `bl`s and a tail `b` — with 14 distinct targets.
Its head is all lock-and-zone setup, and every one of those targets is already real:

```
lck_grp_attr_setdefault   0x10    real        zinit            x4 at 0x88 0xc4 0x114 0x144  real
lck_grp_init              0x2c    real        zone_change      x5 at 0xa0 0xdc 0xec 0x124 0x15c  real
lck_attr_setdefault       0x3c    real
lck_spin_init             0x50    real        (ipc_port_multiple_lock_data is passed by address)
```

Then, at `+0x168`, the first name the image does not have:

```
16c: movw r0, #<ipc_space_kernel>
168: bl ipc_space_create_special     <- STUB at 0x800acd98   (inside the object just linked)
```

**The prediction: `stub_hit=ipc_space_create_special`, `xnu_entry_stub_caller=0x800abd70`** —
`caller - 4` = `0x800abd6c` = `ipc_bootstrap+0x168`.

Everything before it is satisfied and real, and everything after it is new: the second
`ipc_space_create_special` (for `ipc_space_reply`), then `mig_init`, `ipc_table_init`,
`ipc_voucher_init`, `ipc_importance_init`, `semaphore_init`, `mk_timer_init` and a tail `b
host_notify_init`.

### An address operand that is a stub today

`ipc_space_kernel` and `ipc_space_reply` are passed **by address** — they are `ipc_space_t` variables —
but both are currently defined in this image as *function* stubs, so the operand is the address of a
`movw r0, #<name>` instruction. That would matter if the callee were real while the operand was not:
`ipc_space_create_special` writes a struct through the pointer, and it would write into a stub's text.
It did not happen here, and it cannot: `osfmk_ipc_ipc_space.o` is the object that defines
`ipc_space_create_special` **and** both variables (`T` and `B` respectively), so the step that makes
the callee real makes the operands real in the same move. Worth recording because it is the kind of
hazard this project's stub generator creates silently, and the reason to check it is a `B` in the same
object rather than a count.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000045
 xnu_entry_kv_in_dram=0x00000045
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_space_create_special
 xnu_entry_stub_caller=0x800abd70

No errors detected
```

`0x800abd70` resolves to `ipc_bootstrap+0x16c`, whose `caller - 4` is
`800abd6c: bl 800acd98 <ipc_space_create_special>` — the prediction, address for address, for the
**eighth step in a row**, and the first one whose prediction was a location *inside* a function that
did not exist in the previous image. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

The lock and zone setup before the stop all ran: `lck_grp_attr_setdefault`, `lck_grp_init`,
`lck_attr_setdefault`, `lck_spin_init`, four `zinit`s and five `zone_change`s — the same shape as 259's
telemetry and 264's waitq, applied to `ipc_space_zone`, `ipc_kmsg_zone` and the object zones.

`kv_written == kv_in_dram == 0x45`. The KV buffer holds a fixed 45-byte prefix and then the stub's
name verbatim — which is why every previous stop read `0x37` plus its name's length — and
`ipc_space_create_special` (24 characters) is the longest name this frontier has ever stopped at.

## Cost

| | exp-264 | now |
| --- | --- | --- |
| undefined | 619 | **627** (8 resolved, 16 added) |
| function stubs | 543 | **550** |
| storage stubs | 76 | **77** |
| entry text | 794436 B | **795556 B** (+1120) |
| entry image | 900664 B | **900688 B** (+24) |
| entry `.bss` end | 0x8010b2c8 | **0x8010b488** |
| derived `args` offset | +1101824 | **+1101824**, unchanged |
| `topOfKernelData` | +3145728 | **+3145728**, unchanged |
| headroom | 2051384 B | **2050936 B** |
| payload text | 1392882 B | **1392906 B** (+24) |

The eight resolved are the object's own three functions plus five of its statics
(`ipc_kernel_map`, `ipc_port_max`, `ipc_pset_max`, `ipc_space_max`, `msg_ool_size_small`) — names the
entry image had been carrying as stand-ins since it first referenced IPC. The text grew 1120 bytes and
the image only 24, so the growth fitted the linker script's alignment padding and `.bss` moved by a few
hundred bytes; nothing derived moved, and the payload moved by exactly the image's 24.

## What is next

`ipc_space_create_special` — `osfmk/ipc/ipc_space.c`, and it is the object that also defines
`ipc_space_kernel` and `ipc_space_reply`, so it is the same one-object step this project has been
taking, with the added property that it retires the address-operand hazard above. Behind it the rest of
`ipc_bootstrap`'s tail is a measured list, every target still a stub:

```
800abd6c  bl ipc_space_create_special   <- stub   0x800acd98   both `ipc_space_kernel` and `ipc_space_reply`
800abd80  bl ipc_table_init             <- stub
800abd84  bl ipc_voucher_init           <- stub
800abd88  bl ipc_importance_init        <- stub
800abd8c  bl semaphore_init             <- stub
800abd90  bl mk_timer_init              <- stub
800abd94  b  host_notify_init           <- stub (a tail call: its `lr` is `ipc_bootstrap`'s caller)
```

Six more stops are already visible in this one function, and then `kernel_bootstrap` continues with
`mac_policy_init` at `0x8000dc84`. That is the run-in to `bsd_init`.

One prediction note for those six: the tail call is the case the caller report cannot read the same
way. When `host_notify_init` is reached it is entered with a `b`, not a `bl`, so the stub's own `lr`
is *`ipc_bootstrap`'s caller* — `kernel_bootstrap+0x238` — and not a site inside `ipc_bootstrap`.
Experiment 252 met the same distinction from the other side (`vm_object_bootstrap` ended in `b
lck_mtx_init` / `b zone_change`, and its stop was reported against the function that contained the
tail call); it is worth re-checking when that step comes.

## Reproduce

```bash
# the object, and that its new names are a subsystem
grep -n 'osfmk/ipc/ipc_init.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_ipc_ipc_init.o | grep -E ' T | [BD] '
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_ipc_ipc_init.o | wc -l        # 27

# the 16 it adds, and where the first one is reached
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_ipc_ipc_init.o | awk '{print $2}' | sort -u > /tmp/ipc.txt
arm-none-eabi-nm --defined-only out/stage90/xnu_arm_entry.elf | awk '{print $3}' | sort -u > /tmp/img.txt
comm -23 /tmp/ipc.txt /tmp/img.txt                                       # the 16

# its calls in order, and the first unsatisfied one
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_ipc_ipc_init.o \
  | awk '/<ipc_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' | grep -oE 'R_ARM_(CALL|JUMP24)\s*\S*'

# the two variables the first call is handed, and the object that owns them
for n in ipc_space_create_special ipc_space_kernel ipc_space_reply; do
  for o in out/xnu_kernel_obj/*.o; do arm-none-eabi-nm --defined-only "$o" | grep -qE " [TBD] $n\$" && echo "$(basename $o) $n"; done
done

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_264.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 8
comm -13 <(sort /tmp/undef_264.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 16
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | sed -n '/<ipc_bootstrap>:/,/^$/p' | grep -n 'ipc_space_create_special'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x800abd70     # -> ipc_bootstrap+0x16c
```
