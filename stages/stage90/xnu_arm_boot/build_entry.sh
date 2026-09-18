#!/usr/bin/env bash
#
# Build the XNU entry image: XNU's real osfmk/arm/start.s, linked with the symbols it needs, at a
# base the payload can jump to.
#
#   ./build_entry.sh            # build into out/stage90/
#   ./build_entry.sh --verbose  # show the commands
#
# The result is:
#   out/stage90/xnu_arm_entry.bin    the raw image, to be copied to ENTRY_BASE
#   out/stage90/xnu_arm_entry.elf    for inspection
#   out/stage90/xnu_arm_entry.h      base, size, entry and bss bounds, for the payload
#
# Why an image rather than objects in the payload. XNU's `_start` is position-dependent: it
# converts its link-time addresses to physical ones with `addr - virtBase + physBase`, builds its
# own page tables and switches TTBR0/TTBR1 to them. So it has to be linked at a base of its own,
# with a `boot_args` describing that base, and entered at that base. See xnu_arm_boot/entry.ld and
# entry_stubs.c for the layout and for what the symbols do.
#
# Two toolchains, deliberately. start.s is assembled by clang (nothing else will do - see
# xnu_arm_assemble.sh's header), and the Stage-owned stubs are compiled by the payload's
# arm-none-eabi-gcc, which is what the rest of the project uses. Both produce ARM EABI objects and
# the link is where they meet.

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$(cd .. && pwd)
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
BOOT_DIR=$STAGE_DIR/xnu_arm_boot
OUT=$REPO_ROOT/out/stage90
mkdir -p "$OUT"

VERBOSE=0
[[ ${1:-} == --verbose ]] && VERBOSE=1

ENTRY_BASE=0x00200000

# The image's own size decides everything that sits above it, so the layout is *computed* after the
# link and written into xnu_arm_entry.h, rather than being a constant here that the payload repeats.
# These numbers have moved twice already (experiments 168 and 169) and the second time it was
# because two copies of one of them disagreed; from here the payload reads the same header, so a
# disagreement is not expressible. In order, from the base:
#
#   [ENTRY_BASE        .. bss_end)             the image: text, data, bss, stacks
#   [ENTRY_ARGS_OFFSET .. + one page)          the boot_args copy `_start` reads
#   [DATA_LIMIT        .. + ENTRY_TABLE_BYTES) `_start`'s own page tables
#   [ENTRY_DT_OFFSET   .. + ENTRY_DT_MAX)      the device tree
#   [ENTRY_BASE        .. + ENTRY_SIZE)        the window, mapped by the payload and by XNU
#
# `topOfKernelData` is DATA_LIMIT: `osfmk/arm/start.s:149` loads it out of boot_args into TTBR0 and
# TTBR1 and then writes ENTRY_TABLE_BYTES (ten pages) of entries starting there. So the image has to
# end below it, the tree has to begin above it, and both are checked before anything is written.
ENTRY_TABLE_BYTES=0x0000A000   # `(PGBYTES/4 + PGBYTES/4*4) * 2` words, from start.s's loop
ENTRY_DT_MAX=0x00020000        # the tree buffer; the tree is 0x7294 today
ARGS_BYTES=0x00001000          # one page, which is what `boot_args` needs to fit in

# `STAGE90_ENTRY_REAL_ARM_INIT=1` links XNU's own compiled objects - `arm_init.o`, `data.o`,
# `bcopy.o`, `bzero.o` - in place of the stand-ins that name themselves and return, and generates a
# reporting stub for everything still missing. Default off, so the proven image stays the default
# and the two are one variable apart.
REAL_ARM_INIT=${STAGE90_ENTRY_REAL_ARM_INIT:-0}
STUB_DEFINES=()
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES=(-DSTAGE90_ENTRY_REAL_ARM_INIT=1)
# `osfmk_arm_pmap.o` is linked in every real-`arm_init` build, so `pmap_bootstrap` is defined by
# XNU's own object and the probe that used to stand on it - a second definition - is compiled out
# here rather than left to be a link error. Same mechanism as `arm_init`'s stand-in above, and the
# same consequence: that probe's twelve values were experiment 195's measurement and cannot be
# taken again, because the function they were taken from now runs for real.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_PMAP_BOOTSTRAP=1)
# ...and likewise for `_consume_kprintf_args`, which `osfmk_kern_printf.o` also defines.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_KPRINTF=1)

# The one thing in this image that is neither XNU's nor this project's: the compiler's own runtime.
# Experiment 182's run stopped at `__aeabi_uldivmod`, which is the ARM EABI helper for 64-bit
# division and comes from libgcc - measured then, not assumed: no file in the XNU tree mentions the
# symbol, no compiled XNU object defines it, and no compiled XNU object defines any `__aeabi_*`
# symbol at all. So there is nothing here for `entry_arm_rtabi.s` to alias it to, the way it aliases
# `__aeabi_memcpy` to XNU's own `bcopy`.
#
# libgcc rather than a hand-written division routine, deliberately. `__aeabi_uldivmod` in libgcc is
# a tested implementation of a fiddly algorithm, and a second one written here would be a number
# this project cannot check against anything - the shape of mistake the probe mechanism exists to
# avoid. `--start-group` because `__aeabi_uldivmod` in that archive references `__udivmoddi4` in the
# same archive, and a single pass over an archive can miss a member it pulls late.
#
# The group is added at the *end* of the link, so it supplies only what nothing else did; whatever
# it brings beyond the named symbol shows up in the undefined list the same way any object's would.
LIBGCC=${STAGE90_ENTRY_LIBGCC:-$(arm-none-eabi-gcc -print-libgcc-file-name)}

say() { printf '%s\n' "$*"; }
run() { [[ $VERBOSE -eq 1 ]] && printf '  %s\n' "$*"; "$@"; }

say "== assembling XNU's entry point =="
run "$STAGE_DIR/xnu_arm_assemble.sh" > "$OUT/xnu_arm_assemble.log" 2>&1 || {
    tail -5 "$OUT/xnu_arm_assemble.log"; exit 1; }
say "  osfmk/arm/start.s: $(grep -c . "$OUT/xnu_arm_assemble.log") lines of report"

say "== compiling the symbols start.s needs =="
run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
    -O2 -Wall -Wextra -Werror -std=gnu11 "${STUB_DEFINES[@]}" \
    -c "$BOOT_DIR/entry_stubs.c" -o "$OUT/xnu_arm_entry_stubs.o"
run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding \
    -c "$BOOT_DIR/entry_vectors.s" -o "$OUT/xnu_arm_entry_vectors.o"

# The part of the entry image that runs XNU's own code. clang, because the XNU objects it links
# against were built by clang (see xnu_arm_assemble.sh for why that is not a preference), and with
# one clang warning suppressed: XNU's EXTERNAL_HEADERS/stddef.h defines ptrdiff_t as a null-pointer
# subtraction, which clang flags under -Werror and gcc does not.
say "== compiling the in-kernel probe (XNU's own DT and boot-arg code) =="
XNU=$REPO_ROOT/external/xnu-4570.1.46

LINK_OBJS=(
    "$OUT/xnu_arm_start.o"
    "$OUT/xnu_arm_entry_vectors.o"
    "$OUT/xnu_arm_entry_stubs.o"
)

if [[ $REAL_ARM_INIT -eq 1 ]]; then
    # --- XNU's own objects, and a generated stub for everything they still need -------------------
    #
    # Each object below is the *compiled* XNU object, taken from the same builds that produced the
    # measurement image - so this is XNU's own code and data, at the same optimization, with the
    # same flags, not a recompilation. They link into this image because the objects are
    # relocatable and the addresses are fixed by this link, which is what makes the 0x00200000 base
    # a parameter rather than a recompile.
    #
    # Every path is checked, because each one is produced by a different tool and a missing file
    # would otherwise be a link error naming a symbol rather than a build that says which step to
    # run. The choice of *which* objects is deliberate and small: `data.o` is the per-CPU data and
    # the boot stacks, and `bcopy.o`/`bzero.o` are the memory routines the compiler's EABI calls
    # are aliased to (see entry_arm_rtabi.s). Nothing is added "because it looks relevant" - the
    # closure of `arm_init` is the whole kernel (see tools/entry_closure.py, experiment-158), so a
    # rule like that would not stop anywhere.
    require() {
        [[ -f $1 ]] || { say "no $1 - $2" >&2; exit 2; }
    }
    ARM_INIT_OBJ=${STAGE90_ENTRY_ARM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_init.o}
    ARM_DATA_OBJ=${STAGE90_ENTRY_DATA_OBJ:-$REPO_ROOT/out/xnu_asm_obj/data.o}
    ARM_BCOPY_OBJ=${STAGE90_ENTRY_BCOPY_OBJ:-$REPO_ROOT/out/xnu_asm_obj/bcopy.o}
    ARM_BZERO_OBJ=${STAGE90_ENTRY_BZERO_OBJ:-$REPO_ROOT/out/xnu_asm_obj/bzero.o}
    # `osfmk/arm/cpu.c`, and it is here because the device named it rather than because it looked
    # relevant: experiment-159's run reached `cpu_data_init()` and stopped there, and that function
    # is a field-by-field initialization of a `cpu_data_t` (`cpu.c:332-401`) whose only references
    # outside itself are the storage symbols `ExceptionVectorsTable` and `RTClockData` - both of
    # which this image already carries. So it is a leaf, and linking it is what turns the measured
    # `stub_hit=cpu_data_init` into the *next* thing `arm_init` asks for. The rest of `cpu.o` is
    # stubbed as usual: the closure of `arm_init` is the whole kernel, so the image grows one object
    # at a time and each run reports the next edge.
    ARM_CPU_OBJ=${STAGE90_ENTRY_CPU_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpu.o}
    # `pexpert/arm/pe_init.c`, again named by the previous run rather than by inspection:
    # experiment-168's `stub_hit=PE_init_platform` is `arm_init.c:159`, and this object defines it.
    # It is not a leaf the way `cpu.o` is - `PE_init_platform` pulls in `DTInit` and the rest of
    # XNU's device-tree reader, which is the point of the run: it makes XNU's own tree reader walk
    # the tree this project built, from inside XNU's own `arm_init`, with XNU's own page tables.
    # It is also 34927 bytes of text against experiment-168's 26744 bytes of headroom, which is why
    # `topOfKernelData` moved in the same experiment.
    ARM_PE_INIT_OBJ=${STAGE90_ENTRY_PE_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_init.o}
    # `osfmk/arm/strlcpy.c`, named by experiment-169's run: with `pe_init.o` in the image,
    # `PE_init_platform` ran its body and stopped at `stub_hit=strlcpy`, which is its first call
    # that this image does not provide (`pe_init.c:302`, the pixel-format assignment - eight lines
    # before the `DTInit` call the previous experiment predicted). 68 bytes of text and no external
    # references of its own, so it is a leaf, and linking it is what puts the `DTInit` probe added
    # in entry_stubs.c in reach.
    ARM_STRLCPY_OBJ=${STAGE90_ENTRY_STRLCPY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_strlcpy.o}
    # `osfmk/arm/strlen.s`, and the one place this build links two objects in one experiment on
    # purpose rather than one. `nm -u osfmk_arm_strlcpy.o` is `memcpy` and `strlen`; `memcpy` is
    # already in the image, and `strlen` is a leaf in the assembly pool. Linking `strlcpy.o` alone
    # would spend a hardware run reporting a name the host already printed, and the object after it
    # - the one whose call the `DTInit` probe in entry_stubs.c is waiting for - would still not be
    # reached. So both are here: the edge the device named, and the one symbol that edge needs.
    ARM_STRLEN_OBJ=${STAGE90_ENTRY_STRLEN_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strlen.o}
    # `osfmk/arm/strncpy.c`, named by experiment-185's `stub_hit=strncpy` - which is `locks.c:169`,
    # the fourth statement of `lck_mod_init`, reached now that the lock subsystem's own code runs.
    # 104 bytes of text, and its `nm -u` is `memcpy`, `memset` and `strnlen`: `memcpy` and `memset`
    # are already in the image (bcopy.o/bzero.o, aliased by entry_arm_rtabi.s), so it is a leaf
    # apart from `strnlen`, which is in the assembly pool. Same shape as the strlcpy/strlen pair
    # above and linked the same way - the edge the device named, plus the one symbol that edge needs.
    #
    # It is also worth noting what this symbol is: it has been in the undefined list since the image
    # was first assembled and only became the *edge* when the code calling it became real. The
    # closure is not a queue.
    ARM_STRNCPY_OBJ=${STAGE90_ENTRY_STRNCPY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_strncpy.o}
    # `osfmk/arm/strnlen.s`, the leaf `strncpy.c` needs - 184 bytes of text, no undefined references.
    ARM_STRNLEN_OBJ=${STAGE90_ENTRY_STRNLEN_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strnlen.o}
    # XNU 4570's own device-tree reader, `pexpert/gen/device_tree.c`, and this is the object whose
    # *absence* made `DTInit` a symbol experience-170's probe reported. It defines `DTInit`,
    # `DTFindEntry`, `DTGetProperty`, `DTLookupEntry`, `DTInitEntryIterator` and `DTIterateEntries`
    # - five of which are also in `nm -u` on the object below, so it is named twice over.
    #
    # It is XNU 4570's, not the 2050 reader the *payload* links behind `STAGE90_XNU_REAL_DT`
    # (`out/stage90/xnu-objects/device_tree.o`, 3880 bytes, `DTCreateEntryIterator`). Exp-106 found
    # from the other side that the two are not interchangeable; this is the side where it matters,
    # because the caller here is 4570's own `pe_init.o` and the iterator API it uses
    # (`DTInitEntryIterator`) does not exist in 2050.
    ARM_DEVICE_TREE_OBJ=${STAGE90_ENTRY_DEVICE_TREE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_device_tree.o}
    # `pexpert/arm/pe_identify_machine.c`. Strictly this is the call `PE_init_platform` makes after
    # `DTInit` (`pe_init.c:311`), and the one this project's method would let a run name - but the
    # object above cannot produce a run worth doing on its own: 4570's `DTInit` is
    # `DTRootNode = base; DTInitialized = (DTRootNode != 0);`, two statements with no calls in them,
    # so the only outcome of linking it alone is the stub for this object's function firing. It is
    # linked here because it is what makes the reader above do something measurable: its first act
    # is `pe_arm_get_soc_base_phys()`, which finds `arm-io` in the tree, reads `device_type` and
    # `ranges`, and returns `ranges[1]` - so this is the object that turns "the reader is present"
    # into "the reader read this project's device tree".
    ARM_PE_IDENTIFY_OBJ=${STAGE90_ENTRY_PE_IDENTIFY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_identify_machine.o}
    # `osfmk/device/subrs.c`, named by experiment-171's `stub_hit=strcmp`. The reader above compares
    # property names and values to walk a node (`device_tree.c:375` and `:144`) and `pe_identify_machine`
    # compares the SoC device type against Apple's board names (`:58` onward), so either of the two
    # ways that run could have got there needs this object to get any further.
    ARM_SUBRS_OBJ=${STAGE90_ENTRY_SUBRS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_device_subrs.o}
    # `osfmk/arm/strncmp.s`, named by experiment-172. It is the comparison the reader makes on the
    # `state` property of the cpu node it just found (`pe_identify_machine.c:117`), and it is the
    # last thing between the walk and the probe in entry_stubs.c: if `state` matches "running", the
    # reader goes on to read the frequencies out of the same node and then returns.
    ARM_STRNCMP_OBJ=${STAGE90_ENTRY_STRNCMP_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strncmp.o}
    # `pexpert/gen/pe_gen.c`, named by experiment-173's `stub_hit=pe_init_debug` - the last
    # statement of `PE_init_platform`, so the one symbol between that function and its return, and
    # therefore the one between the image and the `ml_parse_cpu_topology` probe that has been
    # waiting since experiment 171. 508 bytes of text, and it also defines `PE_putc`,
    # `PE_init_printf`, `PE_enter_debugger` and `PE_get_random_seed`, all of which are stubs today.
    ARM_PE_GEN_OBJ=${STAGE90_ENTRY_PE_GEN_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_pe_gen.o}
    # `pexpert/gen/bootargs.c`, and this is `nm -u` on the object above, not a guess: `pe_init_debug`
    # is four `PE_parse_boot_argn` calls and a bitmask. Linking pe_gen.o alone would therefore
    # produce a run whose one possible outcome the host can already name, which is the same reason
    # strlcpy and strlen travelled together in experiment 170.
    ARM_BOOTARGS_OBJ=${STAGE90_ENTRY_BOOTARGS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_bootargs.o}
    # `pexpert/arm/pe_bootargs.c`, named by experiment-174's `stub_hit=PE_boot_args`: the whole file
    # is one accessor, `(char *)((boot_args *)PE_state.bootArgs)->CommandLine`. It travels alone,
    # and that is measured rather than assumed - `nm -u` on the object names `PE_state` and nothing
    # else, and `pexpert/arm/pe_init.o` (in this link since experiment 172) defines it as real
    # storage. So this object adds a definition and no obligation, which is the opposite of the
    # pe_gen/bootargs pair above.
    ARM_PE_BOOTARGS_OBJ=${STAGE90_ENTRY_PE_BOOTARGS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_bootargs.o}
    # `osfmk/arm/machine_routines.c`, named by experiment-176's `stub_hit=ml_parse_cpu_topology` -
    # which `osfmk/arm/arm_init.c:217` calls as the first thing after the platform expert is up.
    #
    # This is the first object in the frontier that is not small. It is 4135 bytes of text across 71
    # functions and references 79 undefined symbols, 56 of them new to this image; every object
    # before it added three to five. The linker resolves an object's references whether or not the
    # function making them ever runs, so linking it whole is what pulls those 56 in - and what the
    # run then measures is which of them XNU's own code reaches next.
    ARM_MACHINE_ROUTINES_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_routines.o}
    # `osfmk/arm/cpu_common.c`, named by experiment-177's `stub_hit=cpu_processor_alloc`. Small
    # again: 2428 bytes of text, 36 symbols, 31 references of which 17 are new. It defines
    # `cpu_processor_alloc` (`:472`), which for the boot CPU is `return &BootProcessor;`, and it
    # also defines `current_processor` and `cpu_number` - which is why the probe now stands one edge
    # further on, at `thread_bootstrap`, where the addresses `arm_init` just wrote can be read.
    ARM_CPU_COMMON_OBJ=${STAGE90_ENTRY_CPU_COMMON_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpu_common.o}
    # `osfmk/kern/thread.c`, named by experiment-178's `stub_hit=thread_bootstrap`. This is the
    # first object whose size changes the character of the step: 16944 bytes of text, 77 functions,
    # 161 references of which 135 are new. Whether "one object" is still the right unit at that size
    # is what this link measures.
    ARM_KERN_THREAD_OBJ=${STAGE90_ENTRY_KERN_THREAD_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_thread.o}
    # `osfmk/kern/timer.c`, named by experiment-179's `stub_hit=timer_init` - the first call any
    # function in `thread.o` makes. Small: 320 bytes of text across 7 functions, 3 references, 1 of
    # them new. Needed because `thread_bootstrap` ends with three `timer_init()` calls; the object
    # after this one is `machine_routines_asm.o`, which defines `machine_set_current_thread` - the
    # last call in `thread_bootstrap`, and the one that writes the TPIDRPRW that `current_thread()`
    # reads back at `arm_init.c:241`.
    ARM_KERN_TIMER_OBJ=${STAGE90_ENTRY_KERN_TIMER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_timer.o}
    # `osfmk/arm/machine_routines_asm.s`, named by experiment-180's `stub_hit=machine_set_current_thread` -
    # the last statement of `thread_bootstrap`. Assembled rather than compiled (`tools/assemble_arm_layer.sh`),
    # because it is Apple's assembly. It is where the thread really becomes current: the first
    # instruction of `machine_set_current_thread` writes TPIDRPRW, which is the register
    # `current_thread()` (`osfmk/arm/cpu_data.h:52`) reads back - so linking this object is what makes
    # `arm_init.c:241`'s `thread = current_thread()` return the thread `thread_bootstrap` just made.
    # 2280 bytes of text, 76 symbols, 12 references of which 4 are new.
    ARM_MACHINE_ROUTINES_ASM_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_ASM_OBJ:-$REPO_ROOT/out/xnu_asm_obj/machine_routines_asm.o}
    # `osfmk/arm/rtclock.c`, named by experiment-181's `stub_hit=rtclock_early_init`. 2136 bytes of
    # text, 19 functions, 18 references of which 3 are new. `rtclock_early_init` is one call -
    # `PE_register_timebase_callback(timebase_callback)` - and that accessor is already real
    # (`pexpert/arm/pe_init.o`), and it *invokes* the callback immediately with
    # `gPEClockFrequencyInfo.timebase_frequency_hz` over 1. So linking this object makes
    # `timebase_callback` real, and it is the first code in the image to divide.
    ARM_ARM_RTCLOCK_OBJ=${STAGE90_ENTRY_ARM_RTCLOCK_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_rtclock.o}
    # `osfmk/kern/startup.c`, named by experiment-183's `stub_hit=kernel_early_bootstrap`. 2728 bytes
    # of text across 7 functions and 98 references, 72 of them new - the largest step since
    # `thread.o`, and the one that brings in the names of the kernel's own startup sequence:
    # `lck_mod_init`, `timer_call_init`, `ipc_init`, `task_init`, `thread_call_initialize`,
    # `vm_mem_bootstrap`, `sched_init`, `machine_load_context`, `console_init`, `bsd_init`.
    # `kernel_early_bootstrap` itself (`startup.c:226`) is four statements: one boot-arg parse, then
    # `lck_mod_init()` and `timer_call_init()`.
    ARM_KERN_STARTUP_OBJ=${STAGE90_ENTRY_KERN_STARTUP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_startup.o}
    # `osfmk/kern/timer_call.c`, named by experiment-184's `stub_hit=lck_mod_init` - not because
    # `lck_mod_init` is in it, but because it is the *next* thing `kernel_early_bootstrap`
    # (`startup.c:238`) asks for, one statement after the symbol the probe answered. 11321 bytes of
    # text across 32 functions and 33 references, so this is the larger half of the same function.
    # `timer_call_init` (`timer_call.c:248`) is five statements, and the first three are locks:
    # `lck_attr_setdefault`, `lck_grp_attr_setdefault`, `lck_grp_init`, then `timer_longterm_init`
    # and `timer_call_init_abstime`. It is the first edge in this image that lands in the lock
    # subsystem - none of which has run - which is why the step is measured before it is taken.
    ARM_KERN_TIMER_CALL_OBJ=${STAGE90_ENTRY_KERN_TIMER_CALL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_timer_call.o}
    # `osfmk/kern/locks.c`, named by experiment-184's `stub_hit=lck_mod_init`. 5943 bytes of text
    # across 47 functions. `lck_mod_init` (`locks.c:140`) is short and has no missing callees:
    # one boot-arg parse, then `queue_init`, a `bzero`, a `strncpy` of "Compatibility APIs",
    # `enqueue_tail`, and `lck_grp_attr_setdefault`/`lck_attr_setdefault`/`lck_mtx_init_ext` - all
    # of which this object also defines. It is the first code in this image that initializes a lock,
    # and it is linked with `timer_call.o` because the two are the same statement pair in
    # `kernel_early_bootstrap` and each was sized on its own before either was added.
    ARM_KERN_LOCKS_OBJ=${STAGE90_ENTRY_LOCKS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_locks.o}
    # `osfmk/arm/locks_arm.c`, named by experiment-186's `stub_hit=lck_mtx_init_ext` - the last
    # statement of `lck_mod_init`. 9868 bytes of text across 64 functions and 37 references, so this
    # is a step the size of `thread.o` in exp-179 rather than the size of the last three, and it is
    # measured before it is taken. It is also where the mutex itself lives: `lck_mtx_lock`,
    # `lck_mtx_unlock`, `lck_mtx_ilk_unlock`, `mutex_pause`, `MutexSpin`, and the six `hw_atomic_*`
    # functions this file's neighbour `locks.o` referenced.
    ARM_LOCKS_ARM_OBJ=${STAGE90_ENTRY_LOCKS_ARM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_locks_arm.o}
    # `osfmk/arm/arm_timer.c`, named by experiment-187's `stub_hit=timer_call_get_priority_params`.
    # 1044 bytes of text across 11 functions and 168 bytes of data, 11 references - the cheapest
    # step since exp-186, and the first whose object is a *driver* rather than more of the kernel
    # initializing itself. `timer_call_get_priority_params` is one line returning
    # `&tcoal_prio_params_init`, and that 168-byte table is the whole of the object's `.data`: the
    # struct is `timer_coalescing_priority_params_ns_t` (`timer_queue.h:114`), whose member offsets
    # (0, 4, 8, 12..32, 32/40/48/56/64, 72, 96, 144) add up to exactly 168, which is how the image
    # knows the table it links and the struct `timer_call.c` reads it through are the same shape.
    # The object also carries `timer_queue_assign`, `timer_call_cpu`, `timer_intr`,
    # `timer_resort_threshold` and `quantum_timer_set_deadline`, all of which the image already
    # references.
    ARM_ARM_TIMER_OBJ=${STAGE90_ENTRY_ARM_TIMER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_timer.o}
    # `osfmk/arm/cpuid.c`, named by experiment-188's `stub_hit=do_cpuid` - the first symbol
    # `cpu_init` asks for, and the statement in `arm_init` right after `kernel_early_bootstrap`.
    # 850 bytes of text across 9 functions and 10 references, and it resolves five things at once:
    # `do_cpuid`, `do_cacheid`, `do_mvfpid`, `do_debugid`, `cpuid_info` and `cache_info`. This is
    # the first object in the image whose work is reading the CPU - MIDR, CLIDR, CCSIDR and the
    # MVFR registers - rather than reading what XNU or the device tree computed.
    ARM_ARM_CPUID_OBJ=${STAGE90_ENTRY_ARM_CPUID_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpuid.o}
    # `osfmk/arm/machine_cpuid.c`, which is what the line above is missing: all nine of cpuid.o's
    # `machine_*` references are here, and this object's own `nm -u` is empty. 220 bytes of text
    # across 9 functions and 24 bytes of `.bss`, so it is self-contained and costs nothing beyond
    # its size. It is linked in the same step rather than discovered in the next one because a
    # frontier that is only reachable through stubs is not a frontier.
    ARM_ARM_MACHINE_CPUID_OBJ=${STAGE90_ENTRY_ARM_MACHINE_CPUID_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_cpuid.o}
    # `osfmk/kern/processor.c`, named by experiment-189's `stub_hit=processor_bootstrap`. 4784 bytes
    # of text, 96 of data and 1616 of `.bss` across 42 references, and it defines both
    # `processor_bootstrap` (`processor.c:120`) and `processor_init` (`processor.c:135`), so one
    # object covers two statements of `arm_init`'s path. What it brings is the scheduler's own
    # state: `processor_array`, `pset0`, `master_processor`, `master_cpu`, `processor_count`,
    # `processor_list` - the objects `sched_init` and the scheduler are about to start using. The
    # new obligation to watch is `processor_data_init`, which is in `processor_data.c` and is the
    # first symbol `processor_init` reaches that nothing defines, which is why the probe is there.
    ARM_KERN_PROCESSOR_OBJ=${STAGE90_ENTRY_KERN_PROCESSOR_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_processor.o}
    # `osfmk/kern/processor_data.c`, the first symbol experiment-190's `stub_hit=processor_data_init`
    # named. 72 bytes of text and two references (`memset`, `timer_init`), both already satisfied in
    # this image, so this is the smallest non-empty step in the sequence: it resolves
    # `processor_data_init` and adds nothing at all. The object is the `processor_data_t` at the end
    # of `struct processor` - `timer_init` over the idle, system and user states, and the debugger
    # state's `db_current_op`.
    ARM_KERN_PROCESSOR_DATA_OBJ=${STAGE90_ENTRY_KERN_PROCESSOR_DATA_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_processor_data.o}
    # `osfmk/arm/machine_routines_common.c`, named by experiment-191's
    # `stub_hit=ml_set_interrupts_enabled`. 2923 bytes of text, 44 of data and 108 of `.bss` across
    # 18 references. Five of the eighteen are undefined in this image and become stubs -
    # `ast_taken_kernel`, `get_threadtask`, `kernel_task`, `proc_get_effective_thread_policy` and
    # `thread_get_perfcontrol_class` - and all five belong to the perfcontrol and AST paths, none of
    # which is on this boot path. The one symbol that matters is `ml_set_interrupts_enabled`
    # (`machine_routines_common.c:507`): not a register access but twenty lines of scheduler state
    # inspection, with the compiled `cpsid if` at the end of its disable path. `processor_init`
    # calls it twice, both times with 0, so only that path runs.
    #
    # The probe had to move off `ml_set_interrupts_enabled` before this object could be measured:
    # the object defines the symbol the exp-191 probe defined, and a link that sees two definitions
    # leaves a partial undefined-reference list - the failure that produced exp-190's wrong
    # measurement of 11 and 11.
    ARM_MACHINE_ROUTINES_COMMON_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_COMMON_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o}
    # `osfmk/arm/arm_vm_init.c`, named by experiment-192's `stub_hit=arm_vm_init` and measured
    # before the run that consumed it. 8176 bytes of text, 8 of data and 216 of `.bss` across 23
    # references - the largest object since `thread.o` in exp-179, and the first thing in this
    # sequence the original goal statement is about: `arm_vm_init` is where the kernel starts
    # setting up memory for the drivers above it.
    #
    # Six of its twenty-three references are the Mach-O section readers (`getsegdatafromheader`,
    # `getsegbynamefromheader`, `getsectbynamefromheader`, `getlastaddr`, plus `firstseg` and
    # `nextsect`), which are all in one object with two references of its own, so the two are
    # linked as one step the way `cpuid.o` and `machine_cpuid.o` were in exp-189. The header those
    # readers walk is defined in this image by `entry_macho.s`; see that file for why an ELF needs
    # one and why a zeroed stub for `_mh_execute_header` is a data abort rather than a slow path.
    ARM_ARM_VM_INIT_OBJ=${STAGE90_ENTRY_ARM_VM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o}
    LIBKERN_KERNEL_MACH_HEADER_OBJ=${STAGE90_ENTRY_KERNEL_MACH_HEADER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/libkern_kernel_mach_header.o}
    # `osfmk/vm/vm_resident.c`, named by experiment-194's `stub_hit=vm_set_page_size`. The function
    # is twelve statements and 52 bytes with no calls in it, but its *object* is the largest this
    # sequence has linked: 31692 bytes of text, 104 of data and 5772 of `.bss` across 113
    # references, and it is the first object from the VM proper rather than the ARM layer. The 113
    # become stubs, and that is the reason this step is measured before it is taken.
    #
    # Nothing between `vm_set_page_size` and the next undefined symbol calls into any of them:
    # `set_mmu_ttb`, `set_mmu_ttb_alternate` and `flush_mmu_tlb` are real, and what follows is a
    # block of stores into `arm_vm_init.o`'s own globals. So the run reaches `pmap_bootstrap`, which
    # is `osfmk/arm/pmap.c`, and that is where the probe stands.
    VM_VM_RESIDENT_OBJ=${STAGE90_ENTRY_VM_RESIDENT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_resident.o}
    # `osfmk/arm/pmap.c`, named by experiment-195's `stub_hit=pmap_bootstrap`. This is the pmap
    # proper - 45052 bytes of text, 72 of data, 1032 of `.bss` and 93 references - and the point at
    # which the kernel starts building page tables for memory that is not this image.
    #
    # It is linked for a reason that the call graph does not show. `pmap_bootstrap`'s twenty-six
    # `bl` sites all name symbols this image already provides (and `__aeabi_uldivmod`, which libgcc
    # supplies), so the walk reports no stop inside it - but it also reads and writes `kernel_pmap`,
    # `kernel_pmap_store`, `pmap_stamp`, the io-region descriptors and the four bookkeeping tables,
    # and every one of those is a *generated storage stub* today. A storage stub is zero, and the
    # first statement of `pmap_bootstrap` is `kernel_pmap->tte = cpu_tte` - a store through a null
    # pointer. Its size is the other half: the generator takes storage sizes from `nm -S` over this
    # project's object pool, which is the right size for the array and the wrong *content* for the
    # pointer. So the object is what makes this function's data real, and the object is the step.
    #
    # Two collisions follow from linking it, and both are settled rather than discovered: the
    # `pmap_bootstrap` probe in `entry_stubs.c` is compiled out (it is a second definition), and
    # the run no longer stops inside `pmap_bootstrap` at all - it stops at whatever the image
    # reaches next, which is the measurement.
    ARM_PMAP_OBJ=${STAGE90_ENTRY_PMAP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_pmap.o}
    # `osfmk/arm/lowmem_vectors.c`, named by experiment-197's `stub_hit=patch_low_glo_static_region`.
    # 72 bytes of text and 988 of data, and the data is the point again: `lowGlo` is an
    # *initialized* page-aligned structure full of self-referential pointers (`&version`, `&kmod`,
    # `&osversion`, `&pmap_object_store.memq`, and the `vm_page` offsets a debugger reads) that a
    # zero-filled stand-in cannot stand in for at all. It also defines `patch_low_glo`, which this
    # image's boot args reach: `debug=0x144` is on the payload's command line, `arm_init.c:323-325`
    # tests `(debug & MIN_LOW_GLO_MASK) == MIN_LOW_GLO_MASK` with `MIN_LOW_GLO_MASK = 0x144`, and
    # the call follows `arm_vm_init`'s return.
    ARM_LOWMEM_VECTORS_OBJ=${STAGE90_ENTRY_LOWMEM_VECTORS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o}
    # `osfmk/kern/printf.c`, named by experiment-198's `stub_hit=printf_init`. 5607 bytes of text,
    # 296 of `.bss` and 26 references - the largest step since `pmap.o`, and the first object this
    # project links whose subject is output.
    #
    # It is the object entry_stubs.c spent a paragraph arguing *against* linking, and the argument
    # still holds: it was about buying four bytes (`_consume_kprintf_args` is an empty variadic
    # function, and the hand-written stand-in for it is the real one). What changed is the reason.
    # `printf_init` is three statements and lives here and nowhere else, and it is the next thing
    # the device reaches, so the object is the step. Two consequences, both settled: the
    # hand-written `_consume_kprintf_args` is now a second definition and is compiled out under
    # `STAGE90_ENTRY_REAL_KPRINTF`, and the console stack this brings - `cnputc`, `PE_kputc`,
    # `debug_putc`, `os_log_with_args`, `paniclog_flush` - becomes 24 stubs that stop the run if
    # reached. `printf_init` reaches none of them: its two `simple_lock_init` calls are
    # `arm_usimple_lock_init`, which the image has had since the lock subsystem came in, and its
    # third statement is `bsd_log_init`.
    ARM_KERN_PRINTF_OBJ=${STAGE90_ENTRY_KERN_PRINTF_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_printf.o}
    # `bsd/kern/subr_log.c`, named by experiment-199's `stub_hit=bsd_log_init` - the first object in
    # this sequence from `bsd/` rather than `osfmk/`. 5851 bytes of text, 4316 of data, 12496 of
    # `.bss` and 42 references, which makes it the largest `.bss` and the widest reference set of
    # any step so far, for a function whose entire body is a comment:
    #
    #     void bsd_log_init(void) { /* After this point, we must be ready to accept characters */ }
    #
    # That is the shape of step the method produces at a boundary: the frontier names the next
    # missing *symbol*, and the symbol here is a no-op. Nothing of the 42 is reachable from it -
    # `tsleep`, `kernel_map`, `mach_vm_map_kernel`, `kalloc_canblock`, `selwakeup` and the two
    # `__firehose_*` calls all sit behind other functions in the file - so the run should pass
    # through and stop at `panic_init`, `arm_init.c:330`, back in `osfmk/`.
    BSD_KERN_SUBR_LOG_OBJ=${STAGE90_ENTRY_BSD_SUBR_LOG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_subr_log.o}
    require "$ARM_INIT_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_DATA_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BCOPY_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BZERO_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_CPU_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLCPY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLEN_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_STRNCPY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRNLEN_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_DEVICE_TREE_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_IDENTIFY_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_SUBRS_OBJ"        "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRNCMP_OBJ"      "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_PE_GEN_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_BOOTARGS_OBJ"     "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_BOOTARGS_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_CPU_COMMON_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_THREAD_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_TIMER_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_ASM_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_ARM_RTCLOCK_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_STARTUP_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_TIMER_CALL_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_LOCKS_OBJ"        "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_LOCKS_ARM_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_TIMER_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_CPUID_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_MACHINE_CPUID_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PROCESSOR_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PROCESSOR_DATA_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_COMMON_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_VM_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$LIBKERN_KERNEL_MACH_HEADER_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$VM_VM_RESIDENT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PMAP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_LOWMEM_VECTORS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PRINTF_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_SUBR_LOG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    LINK_OBJS+=("$ARM_INIT_OBJ" "$ARM_DATA_OBJ" "$ARM_BCOPY_OBJ" "$ARM_BZERO_OBJ" "$ARM_CPU_OBJ" \
                "$ARM_PE_INIT_OBJ" "$ARM_STRLCPY_OBJ" "$ARM_STRLEN_OBJ" "$ARM_STRNCPY_OBJ" "$ARM_STRNLEN_OBJ" "$ARM_DEVICE_TREE_OBJ" \
                "$ARM_PE_IDENTIFY_OBJ" "$ARM_SUBRS_OBJ" "$ARM_STRNCMP_OBJ" "$ARM_PE_GEN_OBJ" \
                "$ARM_BOOTARGS_OBJ" "$ARM_PE_BOOTARGS_OBJ" "$ARM_MACHINE_ROUTINES_OBJ" "$ARM_CPU_COMMON_OBJ" "$ARM_KERN_THREAD_OBJ" "$ARM_KERN_TIMER_OBJ" "$ARM_MACHINE_ROUTINES_ASM_OBJ" "$ARM_ARM_RTCLOCK_OBJ" "$ARM_KERN_STARTUP_OBJ" "$ARM_KERN_TIMER_CALL_OBJ" "$ARM_KERN_LOCKS_OBJ" "$ARM_LOCKS_ARM_OBJ" "$ARM_ARM_TIMER_OBJ" "$ARM_ARM_CPUID_OBJ" "$ARM_ARM_MACHINE_CPUID_OBJ" "$ARM_KERN_PROCESSOR_OBJ" "$ARM_KERN_PROCESSOR_DATA_OBJ" "$ARM_MACHINE_ROUTINES_COMMON_OBJ" "$ARM_ARM_VM_INIT_OBJ" "$LIBKERN_KERNEL_MACH_HEADER_OBJ" "$VM_VM_RESIDENT_OBJ" "$ARM_PMAP_OBJ" "$ARM_LOWMEM_VECTORS_OBJ" "$ARM_KERN_PRINTF_OBJ" "$BSD_KERN_SUBR_LOG_OBJ")

    # The RTABI aliases. Assembly, and assembled by the payload's toolchain like the vectors are,
    # since it is plain ARM with no XNU macros in it.
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm \
        -c "$BOOT_DIR/entry_arm_rtabi.s" -o "$OUT/xnu_arm_entry_rtabi.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_rtabi.o")

    # The Mach-O header `_mh_execute_header` points at. Assembly rather than C because the fields
    # are linker-script symbols, and the sizes among them are differences the linker has to fold.
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm \
        -c "$BOOT_DIR/entry_macho.s" -o "$OUT/xnu_arm_entry_macho.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_macho.o")

    require "$LIBGCC" "install the arm-none-eabi toolchain (arm-none-eabi-gcc -print-libgcc-file-name)"

    say "== pass 1: which symbols do XNU's own objects need? =="
    # The library group is in *this* link as well as the final one, and that is not a detail: pass 1
    # is what produces the undefined set the stubs are generated from, so a symbol libgcc can supply
    # is only left un-stubbed if pass 1 can see libgcc. Adding the group to the final link alone
    # changes nothing - the stub is an ordinary object definition and the linker never looks in the
    # archive for a symbol something already defines. Experiment 183 found that by doing it.
    arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" -nostdlib --no-demangle \
        -o "$OUT/xnu_arm_entry_pass1.elf" "${LINK_OBJS[@]}" \
        --start-group "$LIBGCC" --end-group 2> "$OUT/xnu_arm_entry_pass1.err" || true
    grep -o "undefined reference to \`[^']*'" "$OUT/xnu_arm_entry_pass1.err" |
        sed "s/.*\`//; s/'//" | sort -u > "$OUT/xnu_arm_entry_undef.txt"
    undef=$(wc -l < "$OUT/xnu_arm_entry_undef.txt")
    if (( undef == 0 )); then
        say "  XNU's own objects link with nothing missing - no stubs needed"
    else
        say "  $undef symbol(s) undefined - see $OUT/xnu_arm_entry_undef.txt"
    fi

    # Data or function, and how much data, decided by the kernel objects rather than by a
    # hand-written list: `nm -S` over the pool gives each symbol's type and its real size.
    #
    # The size is the part that is worth this much trouble. A stand-in for a symbol has to be at
    # least as big as the thing it stands for, and the first version of this generator gave every
    # storage symbol 64 bytes - while `EntropyData` is 68 (`entropy_data_t`, random.h:47) and
    # `BootCpuData` is a whole per-CPU data area (data.s). Neither is a size this image may guess:
    # an undersized stand-in is silently overwritten by the first real code that uses it, and what
    # it overwrites is whatever the linker put next. So: size from the object that defines the
    # symbol, and if that size is not knowable, fail the build and make it a decision.
    arm-none-eabi-nm -A -S -P --defined-only \
        "$REPO_ROOT"/out/xnu_kernel_obj/*.o "$REPO_ROOT"/out/xnu_asm_obj/*.o 2>/dev/null |
        sed 's/^[^:]*: //' | awk 'NF>=2 {print $1, $2, ($4 == "" ? "-" : $4)}' |
        sort -u > "$OUT/xnu_arm_entry_kernsyms.txt"

    {
        echo '/*'
        echo ' * Generated by xnu_arm_boot/build_entry.sh from pass 1'"'"'s undefined set.'
        echo ' *'
        echo ' * One stub per symbol XNU'"'"'s own objects need and this image does not provide.'
        echo ' * Reaching any of them stops the run and writes its name, which is the measurement.'
        echo ' * Storage is sized from the defining object (`nm -S`), never guessed.'
        echo " * $undef symbol(s), from: ${LINK_OBJS[*]##*/}"
        echo ' */'
        echo '#include <stdint.h>'
        echo
        echo 'void entry_stub_hit(const char *name);'
        echo
        : > "$OUT/xnu_arm_entry_stubnames.txt"
        n_data=0
        n_func=0
        while IFS= read -r sym; do
            [[ -n $sym ]] || continue
            t=$(awk -v n="$sym" '$1==n {print $2}' "$OUT/xnu_arm_entry_kernsyms.txt" | head -1)
            sz=$(awk -v n="$sym" '$1==n {print $3}' "$OUT/xnu_arm_entry_kernsyms.txt" | head -1)
            case "$t" in
                D|B|R|S|G|C)
                    if [[ -z $sz || $sz == - || $sz == 0 ]]; then
                        say "FAIL: '$sym' is storage (nm says '$t') and its size is not in the" >&2
                        say "      objects this project builds. A stand-in of the wrong size is" >&2
                        say "      overwritten by the first real user of it, so this has to be a" >&2
                        say "      decision: add a hand-written definition with the size taken from" >&2
                        say "      its source, or link the object that defines it. See entry_stubs.c" >&2
                        say "      for how EntropyData and the stacks are handled." >&2
                        exit 1
                    fi
                    echo "/* storage: nm says '$t', size 0x$sz from the object that defines it */"
                    echo "uint8_t $sym[0x$sz] __attribute__((aligned(64)));"
                    echo
                    printf 'data %s %s 0x%s\n' "$sym" "$t" "$sz" >> "$OUT/xnu_arm_entry_stubnames.txt"
                    n_data=$((n_data + 1))
                    ;;
                *)
                    echo "void $sym(void) { entry_stub_hit(\"$sym\"); }"
                    printf 'func %s %s\n' "$sym" "${t:--}" >> "$OUT/xnu_arm_entry_stubnames.txt"
                    n_func=$((n_func + 1))
                    ;;
            esac
        done < "$OUT/xnu_arm_entry_undef.txt"
    } > "$OUT/xnu_arm_entry_realstubs.c"

    say "  stubs: $n_func function(s), $n_data storage"
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
        -O2 -Wall -Wextra -Werror -std=gnu11 \
        -c "$OUT/xnu_arm_entry_realstubs.c" -o "$OUT/xnu_arm_entry_realstubs.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_realstubs.o")
fi


say "== linking at $ENTRY_BASE =="
# start.o first, so `_start` is the first thing in .text and the image base is the entry point -
# not required (the payload jumps to an explicit address) but it makes the map readable.
run arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" -nostdlib -Map "$OUT/xnu_arm_entry.map" \
    -o "$OUT/xnu_arm_entry.elf" \
    "${LINK_OBJS[@]}" \
    --start-group "$LIBGCC" --end-group

entry=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="_start"{print "0x"$1}')
bss_start=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_start"{print "0x"$1}')
bss_end=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_end"{print "0x"$1}')
text_end=$(arm-none-eabi-size "$OUT/xnu_arm_entry.elf" | awk 'NR==2{print $1}')

run arm-none-eabi-objcopy -O binary "$OUT/xnu_arm_entry.elf" "$OUT/xnu_arm_entry.bin"

bin_size=$(stat -c%s "$OUT/xnu_arm_entry.bin")
bss_bytes=$((bss_end - bss_start))

# --- the layout, derived from the image that was just linked ------------------------------------
#
# `_start`'s tables and the device tree go above the image, and the window has to cover all of it,
# because the window is what the payload maps and what `memSize` tells XNU to map for itself. Each
# of the four numbers below is the smallest value that satisfies the one before it, so growing the
# image moves them and shrinking it moves them back - nothing here can drift out of step with the
# link, which is the failure this whole block replaced.
align_up() { local v=$1 a=$2; echo $(( ((v + a - 1) / a) * a )); }
# The boot_args copy: the first page above the image, so it cannot overlap it however it grows.
ENTRY_ARGS_OFFSET=$(align_up $((bss_end - ENTRY_BASE)) 4096)
ENTRY_ARGS_OFFSET=$((ENTRY_ARGS_OFFSET + 0x1000))
# topOfKernelData: 1 MB aligned, and at least 1 MB clear of the arguments so the image has room to
# grow into before this number moves again.
ENTRY_DATA_LIMIT=$(align_up $((ENTRY_ARGS_OFFSET + ARGS_BYTES + 0x100000)) 0x100000)
# The tree: 2 MB above the tables, which is 50 times what they need and keeps the two apart in any
# map or disassembly anyone reads.
ENTRY_DT_OFFSET=$((ENTRY_DATA_LIMIT + 0x200000))
# The window: the smallest power of two that covers the tree's whole buffer, and never smaller than
# 2 MB - `_start` maps it as sections and a smaller window would put the tree outside its own map.
ENTRY_SIZE=0x00200000
while (( ENTRY_SIZE < ENTRY_DT_OFFSET + ENTRY_DT_MAX )); do ENTRY_SIZE=$((ENTRY_SIZE * 2)); done

# --- the invariants that make the layout safe ---------------------------------------------------
#
# Both failure modes are silent corruption with no cause in the log, so both are refused here. An
# image reaching past topOfKernelData is overwritten by XNU's own tables a few instructions into the
# boot; a limit set so high that the tables reach the tree is the same corruption done to the tree.
layout_fail() { say "FAIL: $*" >&2; exit 1; }
(( bss_end - ENTRY_BASE <= ENTRY_DATA_LIMIT )) ||
    layout_fail "the image ends $((bss_end - ENTRY_BASE)) bytes from the base, past topOfKernelData at $ENTRY_DATA_LIMIT"
(( ENTRY_ARGS_OFFSET + ARGS_BYTES <= ENTRY_DATA_LIMIT )) ||
    layout_fail "the boot_args at $ENTRY_ARGS_OFFSET do not fit below topOfKernelData at $ENTRY_DATA_LIMIT"
(( ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES <= ENTRY_DT_OFFSET )) ||
    layout_fail "the $ENTRY_TABLE_BYTES bytes of tables at $ENTRY_DATA_LIMIT reach the tree at $ENTRY_DT_OFFSET"
(( ENTRY_DT_OFFSET + ENTRY_DT_MAX <= ENTRY_SIZE )) ||
    layout_fail "the tree buffer at $ENTRY_DT_OFFSET (+$ENTRY_DT_MAX) is outside the $ENTRY_SIZE window"

cat > "$OUT/xnu_arm_entry.h" <<EOF
/* Generated by xnu_arm_boot/build_entry.sh. The XNU entry image, as data for the payload.
 *
 * Everything the payload needs to place, jump to and describe this image - including the layout
 * above it, which used to be constants in stage90.h and in xnu_entry_jump.c that had to agree.
 */
#define STAGE90_XNU_ENTRY_BASE       $ENTRY_BASE
#define STAGE90_XNU_ENTRY_SIZE       $ENTRY_SIZE
#define STAGE90_XNU_ENTRY_ENTRY      $entry
#define STAGE90_XNU_ENTRY_BSS_START  $bss_start
#define STAGE90_XNU_ENTRY_BSS_END    $bss_end
#define STAGE90_XNU_ENTRY_BIN_BYTES  $bin_size
/* The layout above the image, as offsets from STAGE90_XNU_ENTRY_BASE. */
#define STAGE90_XNU_ENTRY_ARGS_OFFSET             $ENTRY_ARGS_OFFSET
#define STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET     $ENTRY_DATA_LIMIT
#define STAGE90_XNU_ENTRY_DT_OFFSET               $ENTRY_DT_OFFSET
#define STAGE90_XNU_ENTRY_TABLE_BYTES             $ENTRY_TABLE_BYTES
#define STAGE90_XNU_ENTRY_DT_MAX                  $ENTRY_DT_MAX
#define STAGE90_XNU_ENTRY_ARGS_BYTES              $ARGS_BYTES
EOF

say
say "entry base   $ENTRY_BASE"
say "entry point  $entry"
say "text size    $text_end bytes"
say "image bytes  $bin_size"
say "bss          $bss_start .. $bss_end ($bss_bytes bytes, zeroed by the payload)"
say "layout       args +$ENTRY_ARGS_OFFSET, topOfKernelData +$ENTRY_DATA_LIMIT, tree +$ENTRY_DT_OFFSET, window $ENTRY_SIZE"
say "headroom     $((ENTRY_BASE + ENTRY_DATA_LIMIT - bss_end)) bytes below topOfKernelData"
say "above it     $ENTRY_TABLE_BYTES bytes of page tables, then the tree buffer at ENTRY_BASE + $ENTRY_DT_OFFSET"

say "wrote $OUT/xnu_arm_entry.bin, .elf, .h, .map"
say "the payload build reads the .bin from there directly; nothing to install"

