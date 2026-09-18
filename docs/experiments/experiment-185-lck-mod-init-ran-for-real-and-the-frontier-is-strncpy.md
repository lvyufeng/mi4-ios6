# Experiment 185 — `lck_mod_init` ran for real, and the frontier is `strncpy`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=strncpy

No errors detected
```

**The probe was written for `timer_call_get_priority_params` and did not fire.** That is the
second time a probe has been written and not fired (exp-182 recorded the first), and the reason is
the same both times: the frontier is where the code *goes*, not where the call graph says it should
go. `strncpy` was in the undefined list at position 267 of 350 before this run, and in every list
before that - it has been in the closure since the image was first assembled. What changed is not
the symbol, it is that the code that calls it became real.

The prediction was `timer_call_get_priority_params` because that is the first statement of
`timer_call_init_abstime`, reached from `timer_call_init` (`timer_call.c:248`) two lines after
`lck_mod_init` in `kernel_early_bootstrap`. But `lck_mod_init`'s own body ends with a `strncpy` -
`locks.c:169`, `strncpy(LockCompatGroup.lck_grp_name, "Compatibility APIs", LCK_GRP_MAX_NAME)` -
and the run never leaves it. Two objects' worth of closure came in, and the first missing symbol
reached is one that was already there.

## What the run establishes without any printed value

`stub_hit=strncpy` is inside real `lck_mod_init`, and that localizes it precisely. `locks.c:140`:

```c
void
lck_mod_init(void)
{
        if (!PE_parse_boot_argn("lcks", &LcksOpts, sizeof (LcksOpts)))
                LcksOpts = 0;
        queue_init(&lck_grp_queue);
        bzero(&LockCompatGroup, sizeof(lck_grp_t));
        (void) strncpy(LockCompatGroup.lck_grp_name, "Compatibility APIs", LCK_GRP_MAX_NAME);
        ...
```

So the image is past the boot-arg parse, past `queue_init`, and past a `bzero` of the group that
the object's size depends on - and stopped at the fourth statement. **That is the first lock
subsystem code in this image to execute**, and the run says how far it got by where it stopped
rather than by printing anything, which is the property of this method that makes a probe optional
and a frontier name sufficient.

## Two objects, and why in one run

`experiment 184`'s run named `lck_mod_init`, so the object that defines it is `osfmk/kern/locks.o`.
`timer_call.o` is the other half of the same statement pair in `kernel_early_bootstrap`
(`startup.c:236-238`): `lck_mod_init()` then `timer_call_init()`, and `timer_call_init`'s first
three statements are lock calls, so the two objects are one step in every sense but the accident of
which file each function lives in. Each was sized on its own, with the empty object in place of the
one being measured, before either was added:

| | `osfmk/kern/timer_call.o` | `osfmk/kern/locks.o` |
| --- | --- | --- |
| text | 11321 B | 5943 B |
| functions | 32 | 47 |
| undefined refs | 33 | — |
| resolved | `timer_call_cancel` `timer_call_init` `timer_call_queue_init` `timer_call_setup` | `hw_atomic_add` `hw_atomic_and` `hw_atomic_and_noret` `hw_atomic_or` `hw_atomic_or_noret` `hw_atomic_sub` `lck_attr_setdefault` `lck_grp_attr_setdefault` `lck_grp_init` `lck_mod_init` `lck_rw_clear_promotion` `LockCompatGroup` |
| added | 11 | 16 |

343 → 350 for the first, 350 → 355 for the second. The middle number in the second measurement is
351 rather than 350 only because the probe was swapped between the two: the old one satisfied
`lck_mod_init`, the new one satisfies `timer_call_get_priority_params` and leaves `lck_mod_init` to
the object being measured for.

`locks.o`'s resolved list is where the lock subsystem's floor is: **six `hw_atomic_*` functions**,
which are the first real implementations of the atomic operations in this image (until now every
atomic was a generated stub returning zero, and nothing had run one), plus the group machinery and
the group `lck_mod_init` bootstraps. Its added list is the next layer up - `lck_mtx_init_ext`,
`lck_rw_lock`, `lck_mtx_ilk_unlock`, `ipc_kernel_map`, `kmem_alloc_pageable`,
`vm_map_copyin_common`, `assert_wait_deadline`, `thread_wakeup_one_with_pri` - the mutex
implementation, the VM and the scheduler.

| | exp-184 | now |
| --- | --- | --- |
| XNU objects linked | 22 | 24 (`osfmk/kern/timer_call.o`, `osfmk/kern/locks.o`) |
| text | 100972 B | 118156 B |
| image | 181632 B | 198368 B |
| `.bss` | 0x0022c390 – 0x002312c8 | 0x002304a8 – 0x002359c8 |
| boot_args offset | +208896 | +225280 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 343 (299 functions, 44 storage) | 355 (309 functions, 46 storage) |
| headroom | 1895736 B | 1877560 B |

The image is now 193 KB of an 8 MB window, and 1.79 MB below the limit exp-175 derives. The two
new storage stubs are checked against their real types the way the build's sizing rule requires:
`LcksOpts` is `unsigned int` (`osfmk/arm/locks_arm.c:115`) and got `B 0x4`.

## What is next

`strncpy`, which is `osfmk/arm/strncpy.c` - an ARM-specific file, and the neighbour of
`osfmk/arm/strlcpy.c`, whose object this image has carried since exp-173. It is ten lines:

```c
char *
strncpy(char * dst, const char * src, size_t maxlen) {
    const size_t srclen = strnlen(src, maxlen);
    if (srclen < maxlen) {
        memcpy(dst, src, srclen);
        memset(dst+srclen, 0, maxlen - srclen);
    } else {
        memcpy(dst, src, maxlen);
    }
    return dst;
}
```

so it is a leaf, and the step is small. After it, `lck_mod_init` continues to
`lck_grp_attr_setdefault`, `lck_attr_setdefault` (both real in `locks.o` now) and then
`lck_mtx_init_ext`, which is in the added list above and lives in `osfmk/arm/locks_arm.c`.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 118156, image 198368, 355 undefined, 309 stubs

# each object's cost, one at a time, empty object in place of the one being measured
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_LOCKS_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_locks.txt)   # resolved
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_locks.txt)   # added

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the statements the stop is between
sed -n '140,175p' external/xnu-4570.1.46/osfmk/kern/locks.c
sed -n '226,240p' external/xnu-4570.1.46/osfmk/kern/startup.c

# that strncpy was in the closure long before it was the edge
grep -n 'strncpy' /tmp/u_no_locks.txt

# the step after this one
sed -n '31,45p' external/xnu-4570.1.46/osfmk/arm/strncpy.c
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_strncpy.o
grep -n 'lck_mtx_init_ext' out/xnu_kernel_obj/../stage90/xnu_arm_entry_undef.txt
```
