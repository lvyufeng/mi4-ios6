# Experiment 224 — `bsd_init` Links, a Hundred Stubs Arrive, and the Frontier Is `kernel_debug_string_early`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000024
 xnu_entry_kv_in_dram=0x00000024
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kernel_debug_string_early

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` against
`required_mask=0x0000ffff`, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x24 = 36 = strlen("kernel_debug_string_early") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=kernel_debug_string_early` and it held - the tenth consecutive
prediction made from the disassembly rather than from a relocation list.

## What ran: `bsd_exec_setup`'s switch, two stores, and nothing else

`bsd_exec_setup` is a leaf, and `kernel_bootstrap` calls it with a known argument, so the whole of
what this run executed is readable. In the linked image:

```
0020d600: mov  r0, #0
0020d604: bl   bsd_scale_setup       ; -> b  bsd_exec_setup, tail call

0023aa18 <bsd_exec_setup>:           ; r0 = 0
  23aa18: cmp  r0, #7
  23aa1c: bhi  23aa3c                ; not taken for 0
  23aa20: movw r1, #0x6838 / movw r2, #0x6818   ; the two switch tables
  23aa30: ldr  r1, [r1, r0, lsl #2]  ; table at 0x00246838, index 0
  23aa34: ldr  r0, [r2, r0, lsl #2]  ; table at 0x00246818, index 0
  23aa48: movw r2, #0xd2cc / movt r2, #0x26     ; bsd_pageable_map_size
  23aa50: str  r1, [r2]
  23aa54: movw r1, #0xcdb8 / movt r1, #0x26     ; bsd_simul_execs
  23aa5c: str  r0, [r1]
  23aa60: bx   lr
```

The tables, read out of the image rather than computed from the macros:

```
0x00246818: 21 00 00 00 | 21 00 00 00 | 41 00 00 00 | 41 00 00 00   bsd_simul_execs
            81 00 00 00 | 81 00 00 00 | 01 01 00 00 | 01 01 00 00   {33,33,65,65,129,129,257,257}
0x00246838: 00 20 88 00 | 00 20 88 00 | 00 20 0c 01 | 00 20 14 02   bsd_pageable_map_size
            = scale 0: 0x00882000 = 8921088 = 33 * 0x42000
```

So this run wrote `bsd_simul_execs = 33` and `bsd_pageable_map_size = 8921088` into this object's own
`.bss`, exactly as `bsd/kern/bsd_init.c:1274` says for `scale = 0`
(`BSD_SIMUL_EXECS = 33`, `BSD_PAGEABLE_SIZE_PER_EXEC = NCARGS + 2 pages = 0x42000`), and returned.
Control came back to `kernel_bootstrap` at `0x20d608`; between there and `0x20d67c` there is **no call
and no branch** - only the cluster and scale arithmetic - so the next thing executed was the stub:

```
  20d674: movw r0, #0x44a6
  20d678: movt r0, #0x24
  20d67c: bl   23b0c0 <kernel_debug_string_early>   <-- the stop
  20d680: bl   23bb88 <vm_mem_bootstrap>            <-- the next call, not reached
```

`r0` at the stop is `0x002444a6`, and the bytes there are
`76 6d 5f 6d 65 6d 5f 62 6f 6f 74 73 74 72 61 70 00` - the string `"vm_mem_bootstrap"`, immediately
followed by `"cs_init"`, `"vm_mem_init"`, `"telemetry_init"` at `0x002444b7`, `bf`, `cb`. The object
names the routine it is about to call, in order, and the string at the stop is the name of the next
one.

Nothing else of `bsd_kern_bsd_init.o` ran. `bsd_init`, `bsd_early_init`, `bsd_autoconf`,
`bsdinit_task` and `bsd_utaskbootstrap` are linked and not reached.

## The largest step this link has taken, and an accounting that closes

```
resolved (4):  bsd_early_init  bsd_exec_setup  bsd_init           (3 function stubs)
               mb_map                                             (1 storage stub)

added  (104):  acct_init act_set_astbsd aio_init allproc audit_default_aia_p cfil_init
               chgproccnt cloneproc current_proc devfs_kernel_mount dlil_init domaininit
               eventhandler_init execargs_cache_lock file_lock_init flow_divert_init
               get_bsdthread_info host_priv_self host_set_exception_ports init_system_override
               inittodr IOFindBSDRoot IOKitBSDInit IOKitInitializeTime IOSecureBSDRoot
               ipsec_register_control iptap_init kauth_cred_create kauth_cred_getgid
               kauth_cred_getrgid kauth_cred_getruid kauth_cred_getsvgid kauth_cred_getsvuid
               kauth_cred_getuid kauth_cred_ref kauth_init kmeminit kminit kmstartup knote_init
               load_init_program mac_cred_label_associate_kernel mac_cred_label_associate_user
               mac_policy_initbsd maxproc maxprocperuid mbinit mcache_init memorystatus_init
               microtime_with_abstime mountlist mountroot mptcp_control_register nc_disabled
               necp_init netagent_init net_init_run netsrc_init net_str_id_init nstat_init
               nwk_wq_init os_reason_init pgrphashtbl pipeinit posix_cred_label proc_find
               procinit proc_list_lock proc_list_unlock proc_signalend proc_transend
               proc_uuid_policy_init proto_kpi_init psem_cache_init psem_lock_init pseudo_inits
               pshm_cache_init pshm_lock_init pthread_init rootvnode select_waitq_init
               sesshashtbl set_bsdtask_info siginit sigrestrict_arg socketinit sysctl_early_init
               sysctl_mib_init task_clear_return_wait tcp_cc_init throttle_init
               time_zone_slock_init tty_init ubc_init ulock_initialize utun_register_control
               ux_exception_port ux_handler_init vfsinit vfs_mountroot VFS_ROOT
               vnode_pager_bootstrap vnode_put vnode_ref

371 -> 471 undefined
```

`371 + 104 - 4 = 471`, and the same step's stub counters move `304 -> 392` functions and
`67 -> 79` storage. Those four numbers are not independent, and checking that they agree is what
makes the two lists above trustworthy rather than merely plausible:

- **functions:** 304 - 3 (the three resolved function stubs) + 91 new = 392
- **storage:**  67 - 1 (`mb_map`) + 13 new = 79
- **added:**    91 + 13 = 104
- **resolved:** 3 + 1 = 4

So of the hundred and four new obligations, **91 are 12-byte function stubs and 13 are storage**, and
the three "resolved" symbols of the first kind are the three the run itself just proved by executing
through them.

## Cost

`out/xnu_kernel_obj/bsd_kern_bsd_init.o` (`bsd/kern/bsd_init.c`) - **3653 bytes of text, 80 of data,
2720 of `.bss`, 112 definitions, 104 references**:

| | exp-223 | now |
| --- | --- | --- |
| entry objects linked | 63 | 64 (`bsd_kern_bsd_init.o`) |
| entry text | 290456 B | **296600 B** |
| entry image | 388000 B | **404464 B** |
| entry `.bss` | 0x0025e6f8–0x00277608 (102160 B) | 0x00262730–0x0027c408 (**105688 B**) |
| undefined | 371 | **471** |
| stubs | 304 functions, 67 storage | **392 functions, 79 storage** |
| boot_args offset | +495616 | **+516096** |
| headroom below `topOfKernelData` | 1608184 B | **1588216 B** |
| payload text | 880186 B | **896650 B** |

The image grew by 16464 bytes - the second step in a row past the window it had reserved, and by far
the largest move in this sequence. The headroom below `topOfKernelData` is still 1.5 MB, so nothing is
near a limit; but the boot_args offset moved for the first time (`+495616 -> +516096`, one more 4 KB
page), which follows from `.bss` growing past its previous page.

## The three-term decomposition, on a step with a hundred moving parts

Experiment 218's rule was `.text` = symbol region + gaps + an aligned tail that carries the generated
stubs' name strings. It was derived from three steps whose symbol deltas were +264, +112 and +488.
This step has 96 new text symbols, so it is the first real test of it:

```
symbol region    263588 -> 267988          +4400
.text            290456 -> 296600          +6144
                                    gaps + tail  +1744    (91 new name strings and their alignment)
```

And the symbol region itself decomposes exactly, with every term measured:

```
bsd_early_init   0xc -> 0x4         -8      ; three symbols that were 12-byte stubs
bsd_exec_setup   0xc -> 0x4c       +64      ; and are now the object's real code
bsd_init         0xc -> 0xae0    +2772
bsd_autoconf      new  0x44        +68      ; five symbols the object defines that the
bsd_utaskbootstrap new 0x84       +132      ; image had never mentioned at all
bsdinit_task      new  0xa0       +160
copyright         new  0x70       +112
netboot_root      new  0x8          +8
91 new stubs    91 * 12          +1092
                                 -----
                                 +4400
```

The 91 is the same 91 that the stub counters produced by an independent route above. That agreement
between a symbol-size sum over two ELF symbol tables and a difference of two generated-stub counts is
the strongest form of the decomposition available in this project, and it is worth stating plainly:
the two ways of counting the step agree to the byte.

## What is next: `kernel_debug_string_early`, and the prediction is `vm_mem_bootstrap`

The frontier is `kernel_debug_string_early`, defined by `out/xnu_kernel_obj/bsd_kern_kdebug.o`
(`bsd/kern/kdebug.c`) - **21729 bytes of text, 312 of data, 136 of `.bss`, 112 definitions, 104
references**, much the largest object this link has taken on. It is also the only object in the build
that defines the symbol: a scan of `out/xnu_kernel_obj/*.o` finds it in this one file.

Its source is four lines (`bsd/kern/kdebug.c:1328`):

```c
void
kernel_debug_string_early(const char *message)
{
	uintptr_t arg[4] = {0, 0, 0, 0};

	/* Stuff the message string in the args and log it. */
	strncpy((char *)arg, message, MIN(sizeof(arg), strlen(message)));
	KERNEL_DEBUG_EARLY(
		TRACE_INFO_STRING,
		arg[0], arg[1], arg[2], arg[3]);
}
```

and the compiled body is 76 bytes that call `strlen` (twice) and `strncpy` - **both already real**
(`0x00204668` and `0x002046c4`) - and then return. `KERNEL_DEBUG_EARLY` compiles to nothing in this
configuration: there is no call after the `strncpy`, only `add sp, sp, #16` and `pop {r4, pc}`. So the
function has no stubbed dependency of its own and cannot stop anywhere inside itself:

```
00000dc8 <kernel_debug_string_early>:
     dc8: push  {r4, lr}
     dcc: sub   sp, sp, #16
     dd0: vmov.i32 q8, #0                    <-- NEON
     dd4: mov   r4, r0
     dd8: mov   r0, sp
     ddc: vst1.64 {d16-d17}, [r0]            <-- NEON
     de0: mov   r0, r4 / de4: bl strlen
     de8: mov   r2, #16 / dec: cmp r0, #16 / df0: bhi e00
     df4: mov   r0, r4 / df8: bl strlen / dfc: mov r2, r0
     e00: mov   r0, sp / e04: mov r1, r4 / e08: bl strncpy
     e0c: add   sp, sp, #16
     e10: pop   {r4, pc}
```

**This is the first NEON code in this project's history that will execute**, and it is worth its own
paragraph because it is unconditional - unlike `cc_cmp_safe`'s vector path, which needs a length of 32
and has never been taken. Two instructions, `vmov.i32 q8, #0` and `vst1.64 {d16-d17}`, run on the
first instruction pair of the function.

They will not trap, and that is read out of the image rather than assumed. `osfmk/arm/start.s`'s
`join_start` enables CP10 and CP11 in CPACR, and `join_start_1` sets `FPEXC.EN` - the source's own
comment says why ("VFP is enabled for the arm_init path as we may execute VFP code before we can
handle an undef"). Both are in the entry image on the path the run has already taken:

```
00200354: mrc  p15, 0, r2, c1, c0, 2      ; read CPACR
00200358: mov  r3, #15
0020035c: orr  r2, r2, r3, lsl #20        ; enable coprocessors 10 and 11
00200360: mcr  p15, 0, r2, c1, c0, 2      ; write CPACR
00200368: cmp  r1, #0
0020036c: beq  2003a0 <join_start_1>      ; taken when invoked from _start
002003a0: vmrs r2, fpexc
002003a4: orr  r2, r2, #0x40000000        ; FPEXC_EN
002003a8: vmsr fpexc, r2
002003ac: mov  r2, #0x3000000 / vmsr fpscr, r2
```

`_start` reaches `join_start` by a direct branch (`200070: b 2002e4 <join_start>`), and `arm_init` -
which this run has demonstrably reached, in exp-222 - is called from `start.s` after `join_start`
returns. So the setup above ran before `kernel_bootstrap` did.

**The prediction is `stub_hit=vm_mem_bootstrap`.** `kernel_debug_string_early` returns to
`kernel_bootstrap`, whose next statement is `bl vm_mem_bootstrap` with nothing in between, and
`vm_mem_bootstrap` is a 12-byte stub at `0x0023bb88` in the image this run used. `bsd_kern_kdebug.o`
does not define it - its 104 references do not mention it - so linking that object leaves it a stub.
The argument the stub receives is the string at `0x002444a6`, `"vm_mem_bootstrap"`, which is a second,
independent statement of the same prediction: the log line at the stop names the next routine.

What `vm_mem_bootstrap` will cost is not predicted here, beyond its size - it lives in
`osfmk/vm/vm_init.c`, which this project has not linked any of yet.

## Reproduce

```bash
# the step: 4 resolved, 104 added, 371 -> 471, stubs 304fn/67st -> 392fn/79st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_KERN_BSD_INIT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 4 resolved
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 104 added
wc -l /tmp/A.txt /tmp/B.txt                      # 371 and 471

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=kernel_debug_string_early

# what ran, and why the next stop is vm_mem_bootstrap
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -w bsd_exec_setup
arm-none-eabi-objdump -d --start-address=0x0023aa18 --stop-address=0x0023aa64 out/stage90/xnu_arm_entry.elf
arm-none-eabi-objdump -s --start-address=0x00246818 --stop-address=0x00246858 out/stage90/xnu_arm_entry.elf
arm-none-eabi-objdump -d --start-address=0x0020d670 --stop-address=0x0020d684 out/stage90/xnu_arm_entry.elf
arm-none-eabi-objdump -s --start-address=0x002444a0 --stop-address=0x002444d0 out/stage90/xnu_arm_entry.elf
arm-none-eabi-objdump -dr out/xnu_kernel_obj/bsd_kern_kdebug.o | sed -n '/<kernel_debug_string_early>:/,/^$/p'
arm-none-eabi-size out/xnu_kernel_obj/bsd_kern_kdebug.o
arm-none-eabi-nm -u out/xnu_kernel_obj/bsd_kern_kdebug.o | grep -cw vm_mem_bootstrap   # 0

# the three-term decomposition, over two symbol tables
arm-none-eabi-nm -S -P /tmp/A.elf | awk '$2=="T"||$2=="t"{s+=strtonum("0x"$4)} END{print s}'  # 263588
arm-none-eabi-nm -S -P /tmp/B.elf | awk '$2=="T"||$2=="t"{s+=strtonum("0x"$4)} END{print s}'  # 267988

# the NEON that will run, and the CPACR/FPEXC setup that makes it legal
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -nE "vmrs.*fpexc|vmsr.*fpexc|mcr\s+15, 0, r[0-9]+, cr1, cr0, \{2\}" | head
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
