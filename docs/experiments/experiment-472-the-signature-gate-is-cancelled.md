# Experiment 472 — the signature gate is cancelled, and the exec reaches the task transition

**471 ended by naming exactly one configuration change and why it was the only way forward:** a kernel
built from this project's configuration is a *production secure kernel*. `bsd/kern/kern_cs.c:82-84` is
`#if SECURE_KERNEL` / `const int cs_enforcement_enable = 1;`, `cs_enforcement()` therefore returns 1
unconditionally, and `bsd/kern/mach_loader.c:1126-1130` then answers `LOAD_FAILURE` for any file without
an embedded `LC_CODE_SIGNATURE` — which the RAM disk's Mach-O does not have. The reading was taken off
the object rather than argued: `nm` reported `R cs_enforcement_enable`, and the `cs_enforcement_disable`
boot-arg string was **absent from the same object**, because `cs_init`'s `#if !SECURE_KERNEL` block
(`:133-155`) that reads it is compiled out. So there was no run-time route either, and 471's
instruction was to cancel the macro.

**The change is one word in a list that already existed** — `-USECURE_KERNEL`, in
`tools/build_xnu_arm_kernel.sh`'s cancellation list, beside `-UCONFIG_NO_PRINTF_STRINGS`, with the
reason written where the list is: twenty-four files read the macro and they use all four spellings
(`#if SECURE_KERNEL`, `#if !SECURE_KERNEL`, `#ifdef SECURE_KERNEL`, `#ifndef SECURE_KERNEL`), so a
`#define SECURE_KERNEL 0` — the spelling that looks equivalent — flips only two of the four. Leaving the
macro *undefined* is the one form all four readers agree on.

**The build now measures the object it just compiled rather than trusting the flag.** After the pool
step, `bsd_kern_kern_cs.o` is read twice: the symbol's `nm` type must not be `R` (the `const`), and the
`cs_enforcement_disable` string must be present. `R` is the secure kernel, `B` is the writable default,
and the two readings cannot disagree about which compile this is:

    cs_enforcement_enable is B (writable) and cs_enforcement_disable is a boot arg:
      SECURE_KERNEL is undefined, so an unsigned executable can be activated

`B` rather than `D` because the writable definition is
`SECURITY_READ_ONLY_LATE(int) cs_enforcement_enable = DEFAULT_CS_ENFORCEMENT_ENABLE;`
(`kern_cs.c:94`) and `DEFAULT_CS_ENFORCEMENT_ENABLE` is 0 — `CONFIG_ENFORCE_SIGNED_CODE` is not in this
configuration's generated headers — so the initialized word is zero and lands in `.bss`.

**Measured on hardware: the OS console is unchanged and the stop has moved off the exec path.** The
console block is byte-identical to 470's and 471's through `load_init_program: attempting to load
/sbin/launchd` — the same twelve lines, `md0`, `VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS` — and what
changed is that the image no longer ends in a panic:

    xnu_entry_stub_hit_count   = 1
    xnu_live_stub_hit_name_ptr = 0x804b3da9
    xnu_live_stub_hit_name_w0  = 0x67617473        "stag"
    xnu_live_stub_hit_name_w1  = 0x5f303965        "e90_"
    xnu_live_stub_hit_caller   = 0x8029e764
    xnu_entry_panic_entered    = 0

`0x804b3da9` reads out of `out/stage90/xnu_arm_entry.elf`'s `.text` as the string
**`stage90_pthread_functions.workqueue_mark_exiting`** — one of the 39 slots of the pthread function
table this image supplies in place of `pthread.kext` (432). And `0x8029e764` resolves in the same image
to **`load_machfile + 0x354`**: the return address of the `bl <workqueue_mark_exiting>` at
`0x8029e760`, with `load_machfile` at `0x8029e410`.

**The caller key names `load_machfile` rather than the table's own shim, and that is the shim's shape
rather than an anomaly.** `bsd/kern/pthread_shims.c:337-340` is a one-line forwarder —
`pthread_functions->workqueue_mark_exiting(p)` — which the compiler emits as a load of the table and then
`bx`, with no `bl` to write `lr`. So the stand-in is entered with `load_machfile`'s return address still
in the register, exactly as 433 measured for `pthread_init`'s slot.

**Where that call is, and why the stop's position is the result.** `mach_loader.c:511` is inside the
block that opens at `:493` with *"If this is an exec, then we are going to destroy the old task, and
it's correct to halt it; if it's spawn, the task is not yet running, and it makes no sense"*:

    if (in_exec) {
        kret = task_start_halt(task);
        if (kret != KERN_SUCCESS) { ...; return (LOAD_FAILURE); }
        proc_transcommit(p, 0);
        workqueue_mark_exiting(p);
        task_complete_halt(task);
        workqueue_exit(p);
        task_rollup_accounting_info(get_threadtask(thread), task);
    }
    *mapp = map;
    return (LOAD_SUCCESS);

— the block immediately before `load_machfile` returns `LOAD_SUCCESS`. 471's run returned
`LOAD_FAILURE` from inside `parse_machfile` and never reached a line of it; in this run
`parse_machfile` completed and `load_machfile` walked into the task transition. **The Mach-O was
accepted**: the magic, the CPU type and subtype, the two segments, the entry point and the code-signature
gate all passed, and the exec got as far as the code that tears down the old task. The stop is the walk's
ordinary kind — a stand-in that names itself — and not an error path.

**`xnu_entry_lmf_ret = 0` in this log is not a reading, and the key that says so is the counter beside
it.** 471's two instruments are non-terminal: they call the real function and record what comes back. A
stop *inside* `load_machfile` is therefore a stop before either wrapper's record exists, and this log
says so three times over —

    xnu_entry_lmf_calls = 0     the wrapper was never entered
    xnu_entry_lmf_caller = 0
    xnu_entry_osr_calls = 0

— where a zero in `lmf_ret` alone would have been indistinguishable from `LOAD_SUCCESS`, which is also
zero. That is the same distinction 461's record needed: **a slot whose zero is a legitimate value needs a
second key to say whether it was written**, and for these two the second key is the call counter. 473 is
the step that reads the return, because it retires the slot that stops the run before the return happens.

**What would have falsified the step, named before it ran.** A stop still at the signature site — a
`lmf_ret = 0x04` with `osr_ns = 9` and `osr_code = 1`, which is what 471 measured — would have said the
cancellation did not reach the compile. A `panic_entered = 1` with the `SIGKILL` format string would have
said the file was still refused. Neither happened, and the position of the stop is a third, independent
statement: `mach_loader.c:511` is unreachable unless `parse_machfile` has already returned success.

**Measured:** device run exit 0, `No errors detected`, the device back on Android by itself; log
`/tmp/cancro-472-last_kmsg.txt`, 479736 bytes. Pool 708 objects (C 625 of 626 compiled — the one failure
is `osfmk/kperf/kperfbsd.c`, which has been off this path since 440 — C++ 83 of 83). Entry image `.text`
5223520, image bytes 5438068, `.bss` `0x8052fa80 .. 0x805874c0` (358976 bytes, unchanged from 471),
undefined 26, `stage90-qcdt.img` 8458240 bytes.
