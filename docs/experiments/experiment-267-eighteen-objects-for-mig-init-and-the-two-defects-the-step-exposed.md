# Experiment 267 — Eighteen Objects for `mig_init`, and the Two Defects the Step Exposed

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

266's stop was `mig_init`, and the object that defines it is `osfmk/kern/ipc_kobject.c`
(manifest:551) — `osfmk_kern_ipc_kobject.o`, 2404 bytes of text, 68 of data, 12376 of bss, six
functions. **But `mig_init` is not a one-object step, and the reason is the data it walks.**

`mig_e[]` is a 17-entry array of pointers to `struct mig_subsystem`, defined **in that same object**:

```
external/xnu-4570.1.46/osfmk/kern/ipc_kobject.c:169
const struct mig_subsystem *mig_e[] = {
        (const struct mig_subsystem *)&mach_vm_subsystem,
        (const struct mig_subsystem *)&mach_port_subsystem,
        ... 17 entries ...
};
```

and `mig_init` dereferences every entry **with no NULL check** — `ipc_kobject.c:206-215`:

```c
    for (i = 0; i < n; i++) {
	range = mig_e[i]->end - mig_e[i]->start;
	if (!mig_e[i]->start || range < 0)
	    panic("the msgh_ids in mig_e[] aren't valid!");
```

Linking `ipc_kobject.o` alone would give `mig_e[]` seventeen **storage stand-ins** — each a slot of
zeros — so `mig_e[0]->start` would read 0, and the run would enter XNU's **real** `panic` at the first
iteration. `panic` is real code in this image (it is not a stub), so this would not even report a
`stub_hit`: it would print and spin. That is a stop that measures *this step's omission* rather than
the frontier — the argument 262 made for `sched_multiq_dispatch`, applied to a table of 17 entries
instead of 16 bytes.

So the step is 18 objects: `ipc_kobject.o` plus the 17 objects that define the descriptors, which are
**already in the manifest** (`out/xnu_arm_manifest.txt:685-705`) — `out/mach_headers/kserver/**`, MIG
output the build has been compiling all along:

| descriptor | source | | descriptor | source |
| --- | --- | --- | --- | --- |
| `mach_vm_subsystem` | `mach/mach_vm_server.c` | | `lock_set_subsystem` | `mach/lock_set_server.c` |
| `mach_port_subsystem` | `mach/mach_port_server.c` | | `task_subsystem` | `mach/task_server.c` |
| `mach_host_subsystem` | `mach/mach_host_server.c` | | `thread_act_subsystem` | `mach/thread_act_server.c` |
| `host_priv_subsystem` | `mach/host_priv_server.c` | | `vm32_map_subsystem` | `mach/vm32_map_server.c` |
| `host_security_subsystem` | `mach/host_security_server.c` | | `UNDReply_subsystem` | `UserNotification/UNDReplyServer.c` |
| `clock_subsystem` | `mach/clock_server.c` | | `mach_voucher_subsystem` | `mach/mach_voucher_server.c` |
| `clock_priv_subsystem` | `mach/clock_priv_server.c` | | `mach_voucher_attr_control_subsystem` | `mach/mach_voucher_attr_control_server.c` |
| `processor_subsystem` | `mach/processor_server.c` | | `is_iokit_subsystem` | `device/device_server.c` |
| `processor_set_subsystem` | `mach/processor_set_server.c` | | | |

**`OSFMK_KERN_IPC_KOBJECT_OBJ` and `MIG_KSERVER_OBJS` are the change.**

Their object names are *flattened*, and that is worth knowing before looking for them:
`build_xnu_arm_kernel.sh:443` keys an object by its source path with `$XNU/` removed and every `/`
turned into `_`, and these sources live under `out/`, not under the tree — so the name is the whole
absolute path, `_mnt_data_mi4-ios6_out_mach_headers_kserver_mach_mach_vm_server.o`. `build_entry.sh`
now derives that name with a `kserver_obj()` helper that repeats the build's own `sed` rather than
spelling the repository's path into a file name.

## The prediction

`mig_init` calls only `panic` and `_consume_printf_args` — both real — and reads `mig_e`, `mig_buckets`
and `mig_table_max_displ`, all three defined by `ipc_kobject.o` itself. So with the descriptors real it
completes, filling the hash table and printing `mig_table_max_displ`. `ipc_bootstrap` then continues:

```
800abd7c  bl mig_init
800abd80  bl ipc_table_init    ; STUB
```

**The prediction: `stub_hit=ipc_table_init`**, at the `bl` that follows `bl mig_init`.

(One correction to this step's own build-time comment: its first version wrote the *name* correctly and
the call site wrongly — it gave `caller - 4` as `ipc_bootstrap+0x178`, which is `bl mig_init`'s return.
The measurement below is what corrected it, and the comment now stands corrected in place.)

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003b
 xnu_entry_kv_in_dram=0x0000003b
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_table_init
 xnu_entry_stub_caller=0x800abd84

No errors detected
```

`0x800abd84` resolves to `ipc_bootstrap+0x180`, whose `caller - 4` is
`800abd80: bl 800c366c <ipc_table_init>` — the prediction, for the **tenth step in a row**. The
`mig_init` call before it returned, which means the 17 subsystem descriptors were read as real
structures and the hash table built from them.
`failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x3b`: the KV buffer is a fixed 45-byte prefix plus the stub's name
verbatim, and `ipc_table_init` is 14 characters.

## Defect one: the payload outgrew its own high-VA alias window, silently

**This step's first run did not reach any of that.** It stopped with no line after
`mmu_high_bootstrap_collection_dependency_resolution_virt`, which is the last log line before
`mmu_high_bootstrap_selftest` calls the payload's synthetic root (`stage90_kernel_root`) *through the
high-VA alias* — the same call that has succeeded on every run since Stage85.

The cause is arithmetic, and the step's own size is what exposed it. `mmu.c`'s `build_identity_table()`
mapped the alias of the payload's own image as **two fixed sections**:

```c
map_section_dram(STAGE90_HIGH_ALIAS_BASE, 0x00000000u);
map_section_dram(STAGE90_HIGH_ALIAS_BASE + L1_SECTION_SIZE, 0x00100000u);
```

— 2 MB, since Stage86, while the *identity* map beside it has been a loop over
`__stage90_image_end` for just as long. The entry image's 18-object link grew by 114752 bytes, the
payload embeds that image, so the payload's own image grew by the same:

```
payload image end   0x1fdfc0  ->  0x21a000     (0x200000 is the wall: it crossed by 8256 bytes)
g_boot_args         below 2 MB  ->  PA 0x20c264
the alias pointer the selftest passes
                    below 2 MB  ->  0xc020c264   <- unmapped
```

The margin is the part worth recording: the previous payload's image ended at **0x1fdfc0**, 8256 bytes
under the 2 MB wall, so the entry image's +114752 did not have to be large to break it — any step of
more than 8 KB would have.

and the log says exactly that, in the lines the selftest prints before the call:

```
mmu_high_bootstrap_args_phys=0x0020c264
mmu_high_bootstrap_args_virt=0xc020c264
```

`mmu_high_bootstrap_selftest` hands `stage90_kernel_root` the alias of `boot_args`, of `PE_state` and
of the bootstrap state block, and the root reads `args->deviceTreeP` through that alias. One
dereference above the last mapped section, at the first instruction that touched it. No abort line,
no `panic`, no `stub_hit` — a fault taken before any of the payload's own reporting could run, and the
device came back on the hardware watchdog.

**The fix is the loop the identity map already had**, in both tables that build the alias:

- `mmu.c`'s `build_identity_table()` now maps `STAGE90_HIGH_ALIAS_BASE + off -> PA off` for
  `off < __stage90_image_end`, and reports rather than faults if the image ever reaches the window's
  end.
- `xnu_arm_vm_init_full_pmap.c`'s Phase 5 gets the same loop, bounded by *its* next alias (the
  RAM-console alias at 0xc0300000) instead of the GIC alias.
- `STAGE90_GIC_ALIAS_BASE` moved from 0xc0200000 to **0xc0400000** to leave the image alias room, and
  `STAGE90_HIGH_ALIAS_BASE` and `STAGE90_GIC_ALIAS_BASE` are now defined **once**, in `stage90.h`,
  instead of twice (they were duplicated in the two files).
- The selftest records the window and the entries it now depends on:
  `mmu_high_alias_sections=0x00000003` (the third section is the one `g_boot_args` lives in),
  `mmu_entry_high_alias_sec2=0x00210c02`, the five `mmu_high_bootstrap_alias_*_entry` lines, and
  `mmu high bootstrap: calling the root through the alias` / `the root returned` around the call.

## Defect two: the run harness silently skipped four runs

The four runs between the first failure and the fix **never booted anything**, and nothing said so.
`run_and_capture.sh` summarises the *previous* log before it does anything else, and `summarise_log`'s
last statement was:

```bash
  [[ $abort -gt 0 ]] && say "  an abort was logged - ..."
```

When the log has no abort line the list's status is 1, the function returns 1 — and under `set -e` the
caller exits. The script printed the summary of a log it had already printed and stopped before the
gate, before `fastboot`, before anything. Four "runs" re-read the same stale 13441-byte log.

This is the project's own recorded failure class — *a measurement can be the thing that is wrong* — in
its purest form: the fix looked like it had failed when it had never been tried. The tell was that the
log was **byte-identical** across runs whose payloads differed; the diagnostic kv lines added to the
payload never appeared, and that is what a fresh log cannot do. It is an `if` now.

## Cost

| | exp-266 | now |
| --- | --- | --- |
| undefined | 627 | **863** (2 resolved, 238 added) |
| function stubs | 553 | **787** |
| storage stubs | 74 | **76** |
| entry text | 796676 B | **904036 B** (+107360) |
| entry image | 900688 B | **1015440 B** (+114752) |
| entry `.bss` end | 0x8010b3c8 | **0x8012a4c8** |
| derived `args` offset | +1101824 | **+1228800** |
| `topOfKernelData` | +3145728 | **+3145728**, unchanged |
| headroom | 2051128 B | **1923896 B** |
| payload text | 1392906 B | **1508686 B** (+115780) |

The 238 added names are the largest single addition this frontier has made — 261's 40 and 262's 28 put
together are a sixth of it — and they are the shape of the step rather than a surprise: the whole Mach
IPC dispatch surface (`convert_port_to_*`, the `mach_host_*` calls, the `*_notify` kobject hooks,
`ipc_kmsg_alloc`, `ipc_port_destroy`, …) arrives with the 17 descriptors that index it. Each will be
resolved one object at a time when its turn comes, and most already have objects in the manifest.

The payload grew 115780 bytes: 114752 of embedded entry image and 1028 of this step's own fix.

## What is next

`ipc_table_init` — `osfmk/ipc/ipc_table.c`, and a step carried over from 266: that object also defines
`ipc_table_alloc` and `ipc_table_free`, which 266's link added, so two of its names are already in hand.
Behind it `ipc_bootstrap`'s tail is still a measured list:

```
800abd84  bl ipc_voucher_init     <- STUB   osfmk_ipc_ipc_voucher.o
800abd88  bl ipc_importance_init  <- STUB   osfmk_ipc_ipc_importance.o
800abd8c  bl semaphore_init       <- STUB   osfmk_kern_sync_sema.o
800abd90  bl mk_timer_init        <- STUB   osfmk_kern_mk_timer.o
800abd94  b  host_notify_init     <- STUB   osfmk_kern_host_notify.o   (a tail call)
```

and then `kernel_bootstrap` continues with `mac_policy_init` at `0x8000dc84` — the run-in to `bsd_init`.

Two things about the payload are worth carrying forward. **The alias window is now derived from the
image, but the entry image is copied into a window of its own** (`STAGE90_XNU_ENTRY_SIZE`, computed by
`build_entry.sh`) — that one *is* checked at build time and by the jump, so it is the tracked half of
the same property. And the payload is now 1.5 MB and growing at ~115 KB per step of this size: the
next time the entry image grows by more than a few hundred KB, the alias loop's limit (4 MB) is the
next wall, and the loop reports it instead of faulting.

## Reproduce

```bash
# the object that defines mig_init, and the table it walks
grep -n 'osfmk/kern/ipc_kobject.c' out/xnu_arm_manifest.txt
arm-none-eabi-objdump -r out/xnu_kernel_obj/osfmk_kern_ipc_kobject.o | sed -n '/\[.data\]/,/^$/p'
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_ipc_kobject.o \
  | awk '/<mig_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'panic|_consume_printf_args'

# each descriptor and the object that owns it - the 17 come from out/mach_headers/kserver
for n in mach_vm_subsystem mach_port_subsystem task_subsystem UNDReply_subsystem; do
  arm-none-eabi-nm -A --defined-only out/xnu_kernel_obj/*.o | grep -E " [BDR] $n\$"
done
grep -n 'kserver' out/xnu_arm_manifest.txt | head

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_266.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 2
comm -13 <(sort /tmp/undef_266.txt) <(sort out/stage90/xnu_arm_entry_undef.txt) | wc -l   # 238
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^800abd80:/{print; exit}'

# the payload defect: the alias window, the section that g_boot_args lives in, and the fix
grep -n 'STAGE90_GIC_ALIAS_BASE\|STAGE90_HIGH_ALIAS_BASE' stages/stage90/stage90.h
arm-none-eabi-nm out/stage90/stage90.elf | grep -E ' (g_boot_args|__stage90_image_end)$'
sed -n '/The high alias of this payload/,+22p' stages/stage90/mmu.c

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'mmu_high_alias_sections\|calling the root\|root returned' /tmp/cancro-last_kmsg.txt
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x800abd84     # -> ipc_bootstrap+0x180
```
