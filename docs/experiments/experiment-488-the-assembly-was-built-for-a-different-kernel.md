# Experiment 488 — the assembly was built for a different kernel

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `tools/check_asm_config.py` (new), `tools/xnu_config/select_master.sh`,
`tools/xnu_config/expand.sh`, `tools/xnu_config/arm_asm_defines.sh`, `tools/gen_assym.sh`,
`tools/assemble_arm_layer.sh`, `tools/build_xnu_arm_kernel.sh`,
`stages/stage90/xnu_arm_assemble.sh`, `stages/stage90/xnu_arm_boot/build_entry.sh`

**Result: process 1 runs again.** Two runs on hardware reach the same frontier 486 measured — a
user-mode abort at PC `0x00001118`, then 23 `getpid` records ending `0x00800000` — and **no stub is hit
anywhere in either log**. 487's single run reached neither: it stopped at the first return to user mode,
with `xnu_live_stub_hit_name_ptr = 0x804fe3c4` resolving to the string
`timer_state_event_kernel_to_user` and `xnu_live_stub_hit_caller = 0x80017f94` = `return_to_user_now + 4`.

The cause was not in the entry image. It was in **how a configuration is expanded**, and it had been
there since the ARM layer got its own option list (466).

## The 10, and why ten was a plausible answer

`tools/xnu_config/select_master.sh` (and Apple's `doconf` before it) selects an option line by matching
the tag list in its `<...>` comment against the configuration's attributes. A configuration the tree
does not declare gets the `+` attribute — and `+` is what every *untagged* `options` line matches, while
no tagged one does. Measured this session, by declaring a fragment with an empty attribute list and
asking `make_defines.sh` for it:

    -DCONFIG_MACH_APPROXIMATE_TIME=1   -DCONFIG_MAX_THREADS=64        -DCONFIG_MSG_BSIZE=CONFIG_MSG_BSIZE_REL
    -DICMP_BANDLIM=1                   -DMACH_FASTLINK=1              -DMULTICAST=1
    -DNO_DIRECT_RPC=1                  -DOLD_SEMWAIT_SIGNAL=1         -DSERIAL_CONSOLE=1
    -DVIDEO_CONSOLE=1

**Ten.** None of them is `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME` and none is `CONFIG_TELEMETRY`, and
that is the whole defect: `osfmk/arm/locore.s` consults both, and with neither defined it keeps
`return_to_user_now`'s `bl EXT(timer_state_event_kernel_to_user)` (locore.s:1966-1970) and drops the
`LOAD_ADDR(r2, telemetry_needs_record)` branch from all five of its telemetry sites.

Ten defines is not a broken build. The kernel links, the boot runs, the console prints BSD's copyright
and `BSD root: md0`, and the driver layer comes up — which is why it survived a whole session. It is a
*plausible* answer to the wrong question, and the difference it makes is visible only later, in a place
that reads as a different problem.

`XNU_MASTER_LOCAL` was the caller's business. `tools/build_xnu_arm_kernel.sh` takes it as an argument
and passes it through, so the kernel's C objects were always built against the 110-flag expansion;
`tools/gen_assym.sh`, `tools/assemble_arm_layer.sh` and `stages/stage90/xnu_arm_assemble.sh` did not,
so the ARM assembly — including both copies of `locore.o` — was built against the 10. Measured, the two
copies:

    STAGE90_XNU with the options   37 undefined = 35 + telemetry_needs_record + telemetry_mark_curthread
    STAGE90_XNU without them       37 undefined = 35 + timer_state_event_kernel_to_user
                                                     + timer_state_event_user_to_kernel

The *count* is the same and the *sets* are disjoint in the two names that matter, which is why the
image's own undefined list (26 names) is 26 before and after and still has to be read by name.

## The four places it is now refused, and the one that reads the result

    select_master.sh    finds tools/xnu_config/<dir>/<CONFIG>.local by convention when the variable is unset
    expand.sh           refuses an undeclared configuration instead of giving it the `+` attribute
    arm_asm_defines.sh  refuses an empty or failed expansion instead of handing its caller nothing
    gen_assym.sh        the same, for the constants file
    assemble_arm_layer.sh / xnu_arm_assemble.sh / build_xnu_arm_kernel.sh   the same, at the consumer

and `tools/check_asm_config.py` is the one that reads the *result*, because a file list can be right
while an object is stale. It checks four things and refuses 21 mutations:

1. **The expansion is the declaration.** `STAGE90_XNU = [ RELEASE mockfs development ]` means every
   option *name* RELEASE has must be in STAGE90_XNU's, plus what the fragment names. Over **names**, not
   over whole flags: `config/MASTER:288,289` declares `CONFIG_MSG_BSIZE` twice, once `<!development,debug>`
   and once `<development,debug>`, so its *value* is expected to change with the tag and a claim written
   over flags would fail on a correct build. Measured: RELEASE 108, `STAGE90_XNU` 110,
   `STAGE90_BOOT` 74, `DEVELOPMENT` 124.
2. **The assembly's list is the configuration's, minus the names `--exceptions` prints, and nothing
   else.** 109 of 110, SLIDABLE removed.
3. **The option's two sides agree in the objects.** `locore.s`'s nine `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME`
   sites are read out of Apple's file; with the option at 1 neither assembled `locore.o` may carry either
   timer function as *undefined*, and — the positive control, so the claim is not an argument from
   absence — both must carry `telemetry_needs_record` **and** `telemetry_mark_curthread`.
4. **The offsets the assembly is given are the ones the kernel's C uses.** 487 asked for this: `assym.s`
   was checked against the assembler that reads it and against nothing that compiles C. `TH_KSTACKPTR`
   = 1480 must appear inside `osfmk_arm_model_dep.o`'s `DebuggerXCall` (`model_dep.c:826`) and
   `osfmk_arm_trap.o`'s `sleh_undef` (`trap.c:237`); `TH_CTH_SELF` = 1496 inside
   `osfmk_arm_machdep_call.o`'s `thread_get_cthread_self` (`machdep_call.c:85`, `ldr r0, [r0, #1496]`) and
   `osfmk_arm_pcb.o`'s `machine_thread_create` (`pcb.c:133`). **A named function, not "the object contains
   the number"**: an immediate anywhere in a 900-line object would agree by coincidence.
   `TH_CTH_DATA` is deliberately not compared and the reason is measured — `pcb.c:133`'s two adjacent
   zero stores are one 16-byte NEON store at 1496, so the field 8 bytes above it has no immediate of its
   own to compare against.

Three of the session's own defects are worth recording. `the_object_loses_the_control` was **accepted**
by the first version of the selftest, because the control was written as "neither of" a pair and an
object referencing one of the two satisfied it — the same "a mutation that removes one of a set"
mistake 487's selftest found, in a check written after it. `one_of_releases_options_is_missing` aborted
with `ValueError` (the flag it dropped was a RELEASE-only one no longer in the list), which is a
selftest that raises instead of mutating. And `symbol_immediates`' first draft read `#-?\d+` from the
whole object rather than from `--disassemble=<symbol>`'s body, which is the 487 class exactly: a
measurement of the wrong scope.

## What was not done, and is owed

**The flag lists are still two lists.** 487's instruction was to *derive* `gen_assym.sh`'s tuple from
`build_xnu_arm_kernel.sh`'s. It is not derived: the option source is single now (`make_defines.sh`, both
sides), and claim 4 pins the only values whose divergence was ever measured to matter, but the
force-include set, the header paths and the per-component slice are still written twice. What a
derivation would buy is bounded by that claim; what it would cost is an edit to the most load-bearing
script in the project, whose failure mode is a whole-kernel re-baseline with nothing to measure it
against. Left undone deliberately, and it is the second time this item has been deferred.

**And the two things 487 left standing**, unchanged: a third census of the platform expert instance
`0xc0591740`'s own service children (`createNubs` attaches the 21 nubs to `this`, so it should have 21 —
the difference between "21 nubs exist" and "these 21 nubs came from that pass"); the release as a
reading (`OSObject::release` / the allocator under the tree root's block); and, owed since 480, whether
the copies that build process 1's arguments are serviced demand faults — `vm_fault`
(`osfmk/arm/trap.c:443` user, `:563` kernel) is the decisive wrapper, not `arm_fast_fault`.

## The runs

Two runs, through the gate, exit 0, device back on Android on its own: 515 332 / 515 764 bytes. Both:

| reading | run 1 | run 2 |
| --- | --- | --- |
| `stub_hit=` lines | **0** | **0** |
| `xnu_live_getpid_count` records / last | 23 / `0x00800000` | 23 / `0x00800000` |
| first user-mode abort PC | `0x00001118` | `0x00001118` |
| OS console block (967 bytes) sha256 | `980921c683e3980e…` | `980921c683e3980e…` |
| `panic_entered` / `entry_checks` / `entry_failures` | 0 / 5 / 0 | 0 / 5 / 0 |
| `xnu_entry_checksum` | `0x9071ec79` | `0x9071ec79` |
| `cls_calls` / `svc_kids` / `sleh_storm` | `0x20` / 2 / 9 | `0x20` / 2 / 9 |
| `persistent_write_attempted=0` / `failure_mask=0` | 25 / 87 | 25 / 87 |

The console block is byte-identical to 486's and 487's — `load_init_program: attempting to load
/usr/local/sbin/launchd.development`, `failed loading … errno 2`, `attempting to load /sbin/launchd`,
and then nothing, which is Apple's own control flow saying the exec is still running
(`bsd/kern/kern_exec.c:5119-5168`: neither the "failed loading" line nor the `panic("Process 1 exec of
%s failed")` exists in either log).

**And the artefacts are the ones the runs are of.** The four-step build was re-run end to end *after*
every script edit — step 1 (3 m 53 s, 703 objects, C++ 83/83), step 2 (`gen_assym.sh` now completes:
"generated 266 defines", where at 487 it exited 2; `assemble_arm_layer.sh`: 17 ok, 0 failed), step 3
(`xnu_entry_488` green, selftest 21/21), step 4 — and it produced a **byte-identical** entry image
(`759c49f1b3096991fbcb9bba1cf3485354fab5fc8e3b99204790b677dc1df664`) and payload
(`4b80513fcf2c97d6272e7af46abd7033006af6060e1520f9cd755163dc468e41`), so the runs are readings of the
shipped build rather than of an intermediate.

Layout: `.text` 5254568 -> **5254504**, image bytes **5470836** (unchanged), `.bss` `0x80537a80 ..
0x8058fcc8` (361032, unchanged), headroom 1508152, undefined 26 / wraps 59 — the same 26, with
`xnu_arm_entry_undef.txt` containing no `timer_state_event_*` and `xnu_arm_entry_stubnames.txt` the same.
Entry image `.elf` sha256
`959b0a6932a75f1675decb1614fc4f2ed5e6d7b2a221a1a564d87850177e785d`; payload `stage90-qcdt.img` 8491008
bytes; `kernel_size = 5964890`, `dt_size = 2521088`.

**489:** the third census of the platform expert instance's service children; the release as a reading;
and, owed since 480, `vm_fault` as the wrapper that attributes the exec path's copies.
