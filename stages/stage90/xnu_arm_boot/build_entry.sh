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
# ...and likewise for `_consume_kprintf_args`, which `osfmk_kern_printf.o` also defines, and for
# `panic`, which `osfmk_kern_debug.o` defines - the first retired stand-in whose replacement is not
# equivalent to it. See entry_stubs.c.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_KPRINTF=1)
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_PANIC=1)
# ...and for `EntropyData`, which `osfmk_prng_random.o` defines as an *initialized* struct -
# `entropy_data_t EntropyData = { .index_ptr = EntropyData.buffer }`, which is why that object's
# `.data` is 488 bytes rather than empty. The stand-in here was the right size and the wrong value;
# experiment 211 is what linked the object and found out, and the link error that said so was
# `multiple definition of 'EntropyData'`, not any measurement of the value.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_ENTROPY_DATA=1)

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
    # `osfmk/kern/debug.c`, named by experiment-200's `stub_hit=panic_init`. 5549 bytes of text, 40
    # of data, 721 of `.bss` and 61 references. Only four statements of `panic_init` run, and the
    # interesting one is the first: `getuuidfromheader(&_mh_execute_header, &uuidlen)` walks *this
    # image's own Mach-O header* - the one experiment 194 added - looking for `LC_UUID`. The header
    # has `ncmds = 2` and two `LC_SEGMENT` commands, so the search returns NULL and the
    # `uuid_unparse_upper` call behind it is not reached; `entry_frontier.py` reports it anyway,
    # because it does not model branches.
    ARM_KERN_DEBUG_OBJ=${STAGE90_ENTRY_KERN_DEBUG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_debug.o}
    # `pexpert/arm/pe_consistent_debug.c`, named by experiment-201's
    # `stub_hit=PE_consistent_debug_inherit`. 394 bytes of text, 4 of `.bss`, 4 references, three of
    # them already satisfied - the smallest object this sequence has linked. Its function looks up
    # `/chosen`'s `consistent-debug-root` and, if it finds one, hands it to `ml_map_high_window`.
    #
    # It will not find one. `stage90_main.c:647-661` writes that property only under
    # `#if STAGE90_XNU_REAL_DT`, whose default is 0 (`stage90.h:6922-6923`), so `/chosen` has four
    # properties and not five, `DTGetProperty` fails and the function returns -1. The run should
    # therefore stop at the next statement of `arm_init`, which is `PE_init_kprintf` - a symbol in
    # no object linked here, and the same shape as experiment 201's prediction: a call that exists
    # in the source and is not reachable on this configuration.
    PEXPERT_PE_CONSISTENT_DEBUG_OBJ=${STAGE90_ENTRY_PE_CONSISTENT_DEBUG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_consistent_debug.o}
    # `pexpert/arm/pe_kprintf.c`, named by experiment-202's `stub_hit=PE_init_kprintf`. 536 bytes of
    # text, 4 of data, 48 of `.bss` and 16 references. It also defines `PE_kputc` and
    # `disable_serial_output`, which this image has been carrying as generated *storage* stubs
    # (4 bytes each, sized from this very object) - so linking it turns both into the real variable,
    # and the generator stops stubbing them because pass 1 now sees them defined.
    PEXPERT_PE_KPRINTF_OBJ=${STAGE90_ENTRY_PE_KPRINTF_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_kprintf.o}
    # `pexpert/arm/pe_serial.c`, named by experiment-203's `stub_hit=serial_init`. 421 bytes of
    # text and **six references - every one of them already satisfied** by objects this image has
    # had since experiments 168-171 (`PE_parse_boot_argn`, `pe_arm_get_soc_base_phys`, `DTFindEntry`,
    # `DTGetProperty`, `ml_io_map`, `arm_debug_read_dscr`). So this step adds no obligation at all,
    # which is the first time that has been true since `cpu.o` in experiment 168.
    #
    # Its compiled body, read from the object rather than from the source, is: `dcc` is not a boot
    # arg; `pe_arm_get_soc_base_phys()` is non-zero; then `DTFindEntry("boot-console", NULL, ...)`,
    # `DTFindEntry("name","uart0",...)` and `DTFindEntry("name","uart1",...)` in turn - and this
    # project's tree has no serial node of any of those three names (`grep` finds none in
    # `stage90_main.c` or `apple_dt.c`), so all three fail and the function returns 0. The
    # `S3CUART` and `ARM_BOARD_CONFIG_MV88F6710` branches are not compiled in - the object carries
    # no `strcmp` reference and none of their strings - so what is left is the
    # `else return 0` at `pe_serial.c:803`.
    #
    # It is linked anyway, because the point of the step is the *lookup*: `serial_init` is the first
    # XNU code to ask this project's device tree for a serial device, and the answer it gets is the
    # one that decides `PE_kputc` (`serial_putc` or `cnputc`).
    PEXPERT_PE_SERIAL_OBJ=${STAGE90_ENTRY_PE_SERIAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_serial.o}
    # `osfmk/console/video_console.c`, named by experiment-204's `stub_hit=initialize_screen`.
    # 27079 bytes of text, 4376 of data, 1360 of `.bss` and 31 references - the largest step since
    # `pmap.o`, and the object the goal statement puts out of scope ("ignore graphics for now").
    # It is linked anyway, because XNU's console *is* the video console until something replaces it:
    # `PE_create_console` (`pe_init.c:379`) calls `PE_initialize_console(info, kPETextMode)`, whose
    # `default:` case calls `initialize_screen(info, op)` unconditionally, and `initialize_screen`
    # lives only here. The console is a basic driver and it is on the boot path; the *graphics* the
    # goal defers are the framebuffer, not this.
    #
    # What should run is one branch of it. `initialize_screen`'s first act, with `boot_vinfo`
    # non-null, is to look for a framebuffer, and this payload's `boot_args` leaves `Video` zeroed
    # (`boot_args.c` sets twelve fields and `Video` is not among them), so:
    #
    #     if (!newVideoVirt && !new_vinfo.v_physaddr) {
    #         kprintf("initialize_screen: No video - forcing serial mode\n");
    #         new_vinfo.v_depth = 0;
    #         (void)switch_to_serial_console();      <-- the stop
    #         ...
    #
    # which is the same symbol experiment 204 predicted and did not get, one call frame deeper and
    # by a different route. Which of the two routes is worth being explicit about: if the stop is
    # `switch_to_serial_console`, XNU has decided this device has no framebuffer.
    OSFMK_CONSOLE_VIDEO_OBJ=${STAGE90_ENTRY_VIDEO_CONSOLE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_console_video_console.o}
    # `osfmk/console/serial_general.c`, named by experiment-205's `stub_hit=switch_to_serial_console`.
    # 824 bytes of text and 14 references - but `switch_to_serial_console` itself is three statements
    # and calls nothing at all:
    #
    #     int old_cons_ops = cons_ops_index;
    #     cons_ops_index = SERIAL_CONS_OPS;
    #     return old_cons_ops;
    #
    # (`osfmk/console/serial_general.c` - it is the whole function.) So this step does not stop
    # inside the function it exists for: `initialize_screen`'s no-video branch calls it, it runs, it
    # returns the old console index, and `initialize_screen` finishes the branch
    # (`gc_graphics_boot = FALSE; disableConsoleOutput = FALSE; gc_acquired = TRUE;`). `cons_ops_index`
    # is the one new obligation and it is storage (4 bytes, `osfmk_console_serial_console.o`).
    #
    # The frontier therefore moves to `arm_init`'s tail, and the prediction is written in
    # experiment 206's log rather than here: the calls after PE_create_console are `PE_init_printf`
    # (a tail call to `vcattach`, which video_console.o provides), `cpu_machine_idle_init` (whose
    # `cpu.c:562` maps the low vectors and calls `ml_io_map`), `PE_init_platform(TRUE, &BootCpuData)`,
    # `cpu_timebase_init`, `fiq_context_init`, `early_random` (still a stub) and `machine_startup`.
    # Experiment 206's run stopped at `stub_hit=io_map` - inside `cpu_machine_idle_init`, NOT inside
    # `PE_init_platform`'s `pe_arm_init_interrupts`, which `arm_init.c` calls two calls later and
    # which that run never reached. `pe_arm_map_interrupt_controller` could not have got there in any
    # case: the tree has no `interrupt-controller`/`master` node on purpose.
    OSFMK_CONSOLE_SERIAL_GENERAL_OBJ=${STAGE90_ENTRY_SERIAL_GENERAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_console_serial_general.o}
    # `osfmk/arm/io_map.c`, named by experiment-206's `stub_hit=io_map`. 360 bytes of text, no data
    # and no `.bss`, seven references, two definitions (`io_map` and `io_map_spec`). Five of the
    # seven are already real here - `panic` (exp-201), `pmap_map` / `pmap_map_bd` /
    # `pmap_map_bd_with_options` (pmap.o, exp-197), `virtual_space_start` (vm_resident.o, exp-195) -
    # and the other two, `kernel_map` and `kmem_alloc_pageable`, are already undefined names in this
    # image, so the step introduces no new obligation: 2 resolved, 0 added.
    #
    # This is the first time this kernel creates a mapping for itself. `ml_io_map` is called from
    # `cpu.c:562` with `ml_vtophys(gPhysBase)` and `PAGE_SIZE`, and `gVirtBase == gPhysBase ==
    # 0x00200000` here, so the call is `io_map(0x00200000, 4096, VM_WIMG_IO)`. `kernel_map` is still
    # the generated 4-byte zero stand-in - which is the *right* value: XNU creates the real one in
    # `kmem_init`, long after `arm_init` - so `io_map` takes its "VM is not initialized" branch and
    # carves the page off `virtual_space_start`.
    #
    # `virtual_space_start` is `0x40000000`: `pmap_bootstrap` sets it to
    # `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` (`arm_vm_init.c:505`, with
    # `MEM_SIZE_MAX` = `0x40000000` at `:134`), and `arm_vm_init.c:517` builds the page tables for
    # that same VA with the same expression - the 1280-page loop of experiment 197. So
    # `pmap_pte(kernel_pmap, 0x40000000)` finds a real PT entry and `pmap_map_bd` writes one PTE
    # into it. `flags` is 7, which is `VM_WIMG_IO`, not the 6 (`VM_WIMG_WCOMB`) that selects the
    # `pmap_map_bd_with_options` leg; the `assert` that would have checked that is `((void)0)` in
    # this RELEASE kernel (`assert.h:106`), so the only `panic`s left in the compiled `io_map` are
    # `round_page`'s overflow checks.
    #
    # The write that follows lands on this image's own first physical page, and that is XNU's design
    # rather than an accident: PA `0x00200000` is `gPhysBase`, in a real kernel that page *is* the
    # low-vectors page, and here it holds `_start` and the Mach-O header `entry_macho.s` writes -
    # both already consumed. So the prediction, read off the object's call order rather than the
    # source's, is `bcopy_phys` (`osfmk/arm/loose_ends.c`, not linked): two real `bcopy`s into the
    # new mapping and three real `ml_static_vtop` calls come first, and `CleanPoC_DcacheRegion` and
    # the `bcopy(running_signature, IOS_STATE, 8)` come after it.
    OSFMK_ARM_IO_MAP_OBJ=${STAGE90_ENTRY_IO_MAP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_io_map.o}
    # `osfmk/arm/loose_ends.c`, named by experiment-207's `stub_hit=bcopy_phys`. 3671 bytes of text
    # and one function of it is the step: `bcopy_phys` at offset 0. The rest is a grab bag this image
    # gets for free - `bzero_phys`, the `ml_phys_read*`/`ml_phys_write*` family, `ml_probe_read`,
    # `ffs`/`fls`/`bcmp`/`memcmp`, `setbit`/`clrbit`/`testbit`, `copypv`, `copyin_validate`/
    # `copyout_validate`, `ml_thread_policy`.
    #
    # Cost: 7 resolved (`bcopy_phys bzero_phys copyin_validate copyout_validate ffs setbit testbit`)
    # and 1 added - `flush_dcache64`, which is `osfmk/arm/caches_asm.s` and which only `copypv`
    # references. Nothing on the boot path calls `copypv`, so the one new stub is a stub the run
    # will not reach.
    #
    # The prediction, again read off the object's call order rather than the source's: with
    # `bcopy_phys` real, `cpu_machine_idle_init` continues from call 13 to call 16 (the second
    # `bcopy_phys`, for `CpuDataEntries_paddr`) and stops at call 17,
    # `CleanPoC_DcacheRegion((vm_offset_t)phystokv((char *)gPhysBase), PAGE_SIZE)` - `caches_asm.s`
    # too, and also not linked. The two calls after it are a real `ml_static_vtop` and
    # `bcopy(running_signature, IOS_STATE, 8)` into the mapping experiment 207 created, so this
    # should be the run that finishes `cpu_machine_idle_init`'s exception-vector work. Its last call
    # is `clean_dcache`, which is `osfmk/arm/caches.o` (`osfmk_arm_caches.o`) - in the link *closure*
    # but not in this link, which are different things and this comment blurred them once.
    OSFMK_ARM_LOOSE_ENDS_OBJ=${STAGE90_ENTRY_LOOSE_ENDS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_loose_ends.o}
    # `osfmk/arm/caches_asm.s`, named by experiment-208's `stub_hit=CleanPoC_DcacheRegion`. 596 bytes
    # of text, ten references, and nineteen globals - the whole cache-maintenance surface:
    # `CleanPoC_Dcache*`, `CleanPoU_Dcache*`, `FlushPoC_Dcache*`, `FlushPoU_Dcache`, `clean_dcache64`,
    # `flush_dcache64`, `clean_mmu_dcache` and the `invalidate_*` / `InvalidatePoU_Icache*` family.
    #
    # Cost: 7 resolved (`CleanPoC_Dcache CleanPoC_DcacheRegion CleanPoC_DcacheRegion_Force
    # CleanPoU_Dcache CleanPoU_DcacheRegion flush_dcache64 InvalidatePoU_Icache`) and 0 added. The
    # object's ten references are `EntropyData ExceptionVectorsBase fiqstack_top gPhysBase gPhysSize
    # gVirtBase intstack_top kdebug_enable` - all already real, from data.o and the asm layer - plus
    # `clean_dcache` and `flush_dcache`, which are already undefined names in this image and
    # therefore already stubs. This is assembled by the same clang pipeline as
    # `machine_routines_asm.o`, so it links the same way.
    #
    # The one function in it that will execute is `CleanPoC_DcacheRegion`, and it is worth having
    # read: its trip count comes from its `length` argument, not from `CTR`/`CCSIDR`:
    #
    #     mov  r1, r1, LSR #MMU_CLINE     // set cache line counter
    # ccdr_loop:
    #     mcr  p15, 0, r0, c7, c10, 1     // clean dcache line to PoC
    #
    # So with `CACHE_MODE = NONE` (caches off, as in every stage so far) it is 128 no-op `mcr`s. That
    # matters because the alternative shape - a loop whose count comes from a cache-geometry register
    # - is exactly experiment 196's defect. `phystokv((char *)gPhysBase)` is `0x00200000` here
    # (`gVirtBase == gPhysBase`), so what it cleans is the low-vectors page the two `bcopy_phys`
    # calls of experiment 208 just wrote.
    #
    # Prediction for the run: `clean_dcache`, which is `osfmk_arm/caches.o` and is not linked. It is
    # the *last* call of `cpu_machine_idle_init` (`cpu.c:594`), so this is the step that takes that
    # function to its final call - and the step after it is the one where it returns and `arm_init`
    # reaches `PE_init_platform(TRUE, &BootCpuData)`.
    OSFMK_ARM_CACHES_ASM_OBJ=${STAGE90_ENTRY_CACHES_ASM_OBJ:-$REPO_ROOT/out/xnu_asm_obj/caches_asm.o}
    # `osfmk/arm/caches.c`, named by experiment-209's `stub_hit=clean_dcache` - which is the *last*
    # call of `cpu_machine_idle_init` (`cpu.c:594`), so this is the object that lets that function
    # return. 2744 bytes of text, no data, no `.bss`, 26 references, 17 definitions: the
    # `platform_cache_*` family, `cache_xcall`/`cache_xcall_handler`/`cache_sync_page`,
    # `dcache_incoherent_io_flush64`/`_store64`, `flush_dcache_syscall`, and `clean_dcache`/
    # `flush_dcache` themselves.
    #
    # Cost: 11 resolved, 0 added - every one of its 26 references is already a real definition in
    # this image (the cache-maintenance functions experiment 209 linked, the pmap and machine-routines
    # symbols earlier steps paid for, and `kvtophys`/`flush_core_tlb`/`cpu_signal`/
    # `up_style_idle_exit`, which the asm layer brings). A free step.
    #
    # What `clean_dcache` does when it runs: it is the `phys == FALSE` leg and `cpu_cache_dispatch` is
    # still `(cache_dispatch_t) NULL` (`cpu_data_init` zeroed `BootCpuData` in experiment 168 and
    # nothing has set that field), so it is one `CleanPoC_DcacheRegion(addr, length)` over the
    # `cpu_data_t` `getCpuDatap()` returns from `TPIDRPRW`. No branch of it reaches `kvtophys` or the
    # dispatch callback.
    #
    # **The prediction is not `clean_dcache` - it is `early_random`, four calls later.** With this
    # object linked, `cpu_machine_idle_init` returns; `if (arm_diag & 0x8000)` is not taken
    # (`arm_diag` is only assigned from the `diag` boot arg at `arm_init.c:278`, and the command line
    # in `boot_args.c` has none); `PE_init_platform(TRUE, &BootCpuData)` runs *and completes*;
    # `cpu_timebase_init` and `fiq_context_init` are both call-free and real; and
    # `__stack_chk_guard = early_random()` is the first stub. That `PE_init_platform` completes is the
    # part that matters: its `else` branch is `pe_arm_init_interrupts(args); pe_arm_init_debug(args);`
    # and both bodies finish on this tree without a new symbol - `pe_arm_map_interrupt_controller`
    # returns 0 at its `gPicBase == 0` check because this tree deliberately has no
    # `interrupt-controller`/`master` node, and `pe_arm_init_debug` returns at its
    # `cpu-debug-interface` lookup. So the run after this one should be the first where **Phase 3's
    # `pe_arm_init_interrupts` is entered by XNU itself**.
    OSFMK_ARM_CACHES_OBJ=${STAGE90_ENTRY_CACHES_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_caches.o}
    # `osfmk/prng/random.c`, named by experiment-210's `stub_hit=early_random`. 2488 bytes of text,
    # 488 of data, 20 of `.bss`, 26 references.
    #
    # One of its definitions retires a *storage stand-in*, and it is the right-size-wrong-value kind
    # ([[mi4-stand-in-size-is-not-value]]): `entropy_data_t EntropyData = { .index_ptr =
    # EntropyData.buffer }` is an initialized struct, so a zero-filled array of the same 68 bytes is
    # not a neutral substitute. `erandom` - the other half of the same `static struct` - is *not* a
    # stand-in of any kind: `grep -c '^erandom$'` against the pre-step undefined list is 0, because
    # nothing in this image referenced it while `early_random` was a stub. Both are the object's own
    # symbols once the object is linked, at `EntropyData 0x0025e4f8` and `erandom 0x0025e53c`.
    #
    # **The prediction is `ccdrbg_factory_nisthmac`, and it is a *clean* stub hit.**
    # `early_random`'s disassembly, read rather than the source's statement order, is:
    #
    #       ldr  r0, [r4, #16]      ; erandom.seedset
    #       cmp  r0, #0
    #       beq  <slow path>        ; TAKEN - BSS is zero
    #       mov  r1, #64            ; sizeof(EntropyData.buffer)
    #       bl   PE_get_random_seed
    #       cmp  r0, #63
    #       bhi  <carry on>         ; NOT TAKEN
    #       bl   panic              ; "EntropyData needed %lu bytes, but got %u.\n"
    #
    # and `PE_get_random_seed` (`pexpert/gen/pe_gen.c:119`, real here since experiment 173) returns 0
    # unless `/chosen` has a `random-seed` property - this project's tree had none until this
    # experiment added one. So the *first* version of this step, which linked the object without
    # touching the tree, walked straight into the panic.
    #
    # **Where that panic actually goes was written down wrongly the first time and is corrected
    # here, because the wrong version would have made the run unreadable.** It was predicted as
    # `stub_hit=PEHaltRestart` on the reasoning that `panic`'s path is all real until
    # `PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o`. Disassembling
    # `panic_trap_to_debugger` rather than reading `debug.c` top to bottom says otherwise:
    #
    #       96c: bl   ml_wants_panic_trap_to_debugger     ; returns FALSE, the `beq` is taken
    #       9a0: bl   current_processor                   ; CPUDEBUGGERCOUNT++ -> 1
    #       9b8: cmp  r0, #6
    #       9bc: bcc  a14                                 ; TAKEN - 1 < NESTEDDEBUGGERENTRYMAX+1
    #       9c4: bl   PEHaltRestart                       ; NOT REACHED
    #
    # `PEHaltRestart` sits behind `CPUDEBUGGERCOUNT > NESTEDDEBUGGERENTRYMAX`, and that count is
    # `PROCESS_DATA(...).db_entry_count` - zeroed `.bss`, so 1 after the increment, so the guard
    # skips it. `write_trace_on_panic` and `kdebug_enable` are both `B` in this image - zero - so
    # `kdbg_dump_trace_to_file`, the other stub on the path, is skipped too, and
    # `PE_arm_debug_panic_hook` is `B` - NULL - so its branch is not taken either. `panic` passes
    # `ctx = NULL` (`mov r3, #0` at 0x244), so the `handle_debugger_trap` branch is skipped. What
    # is left on the path is `DebuggerTrapWithState`, which after `DebuggerSaveState` executes
    # `TRAP_DEBUGGER` - `debug.c:121`, `#define TRAP_DEBUGGER __asm__ volatile("trap")`, an ARM
    # `udf` - and then `panic_stop()`.
    #
    # `panic_stop()` is `panic_spin_forever()` on ARM, not `pmCPUHalt`: `debug.c:133-137` defines
    # the `pmCPUHalt` form only `#if defined(__i386__) || defined(__x86_64__)` and everything else
    # gets `#define panic_stop() panic_spin_forever()`. Both it and `paniclog_append_noflush` are
    # real in this image - `for (;;) { }` after one write into the panic log.
    #
    # **So there is no undefined symbol anywhere on the panic path, and no stub could have been
    # hit.** The trap would have gone through VBAR - installed by the payload's `start.S` and not
    # affected by MMU state - into the payload's own `stage90_undef_c_handler`, which logs
    # `MI4IOS6_STAGE90 undef: addr=... lr=...` and returns to the next instruction, after which
    # XNU spins in `panic_spin_forever` forever. That is a hang, not a stub hit: the run would have
    # produced no `stub_hit` line at all, and the log's last XNU-side entry would have been the
    # `undef` breadcrumb. The device would have been recovered by the payload's watchdog and
    # dead-man, which are armed whether or not the boot is expected to hang.
    #
    # That is the reason this experiment does two things and says so plainly. The object is linked
    # because that is the rule - the previous run named `early_random` and this object defines it -
    # and the `/chosen` `random-seed` property is added in the *same* step because the object's own
    # code path is designed to be fatal without it: `random.c` says "Insufficient entropy is fatal.
    # We must fill the entire entropy buffer during initialization." Aiming a device run at a branch
    # whose stated purpose is to stop the machine is not a measurement, and the forward version
    # settles it just as well - if the run reports a stub past `early_random`, then
    # `PE_get_random_seed` returned 64 and `entropy_readall` ran, which is only possible if the seed
    # was there. The finding behind it is a gap in the *simulated handoff contract*, the third of
    # its kind after `state` on the cpu nodes (experiment 193) and `device_type = "timer"`.
    OSFMK_PRNG_RANDOM_OBJ=${STAGE90_ENTRY_PRNG_RANDOM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_prng_random.o}
    # `osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c`, named by experiment-211's
    # `stub_hit=ccdrbg_factory_nisthmac`. 1620 bytes of text, 24 of data, no `.bss`, 9 references,
    # 7 definitions.
    #
    # **What the stub skipped is not a return value but four function pointers and a size**, and the
    # step after this one is the first in fifty where the next call is *indirect*:
    #
    #     void ccdrbg_factory_nisthmac(struct ccdrbg_info *info, const struct ccdrbg_nisthmac_custom *custom)
    #     {
    #         info->size = sizeof(struct ccdrbg_nisthmac_state) + sizeof(struct ccdrbg_nisthmac_custom);
    #         info->init = init; info->generate = generate; info->reseed = reseed; info->done = done;
    #         info->custom = custom;
    #     };
    #
    # `ccdrbg_init` is not a symbol in this image and does not need to be - it is inlined, and in
    # `early_random` it is the `blx r7` at 0x214 where `r7 = drbg_info.init`. So linking this object
    # makes the factory real and the very next thing that happens is a call through the pointer it
    # just wrote.
    #
    # **The prediction is `cchmac_init`, read from `init`'s disassembly rather than from the source's
    # nesting, because the first call in the function is not the one that runs:**
    #
    #       8: ldr  r0, [r0, #20]   ; info->custom        18: ldr r2, [r0]   ; custom->di
    #      1c: strd r0, [r4]                             20: ldr r2, [r2]  ; di->output_size
    #      24: cmp  r2, #64                              28: bhi cc       ; NOT TAKEN
    #      3c: bls  50            ; success             48: bl  cc_clear   ; error path only
    #      88: bl   memset        ; keysize 0           9c: bl  memset     ; vsize 0
    #      b8: bl   hmac_dbrg_update  -> whose first call is cchmac_init (0x3b4)
    #
    # `cc_clear` is on the error path, and `hmac_dbrg_update`'s own body reaches `cc_clear` only
    # after `cc_cmp_safe` (0x5f8, against `cchmac_init` at 0x3b4). `cchmac_init` is defined by nothing
    # in this tree's linked set and referenced by nothing, so it does not exist as a stub today and
    # arrives as a new one in exactly this step - along with `cchmac_update`, `cchmac_final`,
    # `cchmac`, `cc_cmp_safe` and `cc_try_abort`, all six absent from the image entirely.
    #
    # **The thing to watch, and it is [[mi4-stand-in-size-is-not-value]] in its sharpest form so
    # far.** `erandom`'s initializer is `.drbg_custom = { .di = &ccsha1_eay_di, .strictFIPS = 0 }`,
    # and `ccsha1_eay_di` is, as of experiment 211, a *storage stub*. `init` reaches the digest
    # through it - `state->custom->di`, then `di->output_size`, `di->state_size`, `di->block_size` -
    # and a zeroed `ccdigest_info` reads 0 for all of them. The consequence is visible in the
    # disassembly above: `cmp r2, #64; bhi <error>` is **not taken**, so the code proceeds as if the
    # input were valid and the boot stops at some later missing symbol rather than at a failure that
    # names this one. So linking this object is not by itself enough for a working DRBG; the step
    # that links `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` is the one that gives `ccsha1_eay_di` a
    # value, and until then every field read through it is zero.
    OSFMK_CCDRBG_NISTHMAC_OBJ=${STAGE90_ENTRY_CCDRBG_NISTHMAC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_init.c`, named by experiment-212's `stub_hit=cchmac_init`.
    # 444 bytes of text, no data, no `.bss`, 4 references, 1 definition.
    #
    # **The prediction written here was `ccdigest_init`, and experiment 213's run disproved it.** It
    # is left in place rather than deleted because the way it was wrong is the point - see below -
    # and the run's actual result was a prefetch abort, not a stub hit:
    #
    #     exception: prefetch abort    ifar=0x00000000    ifsr=0x0000000f
    #
    # The old reasoning: `cchmac_init` calls `ccdigest_init` at 0x28 and `ccdigest_update` at 0x3c,
    # both before anything else, and both are absent from the image, so whichever comes first is the
    # stop. The *order* was right and the conclusion was still wrong, because neither call is
    # reached. What decides it is three instructions at the top, and they read the zeroed
    # `ccsha1_eay_di` - the stand-in this step's `di` argument still is:
    #
    #        8: ldr  r0, [r0, #8]   ; di->block_size = 0
    #       18: cmp  r0, r2          ; r2 = key_len = state->keysize = di->output_size = 0
    #       1c: bcs  94              ; TAKEN - 0 >= 0, so 0x28 and 0x3c are SKIPPED
    #       ...
    #      11c: ldr  r3, [r5, #24]   ; di->compress = NULL
    #      130: blx  r3              ; -> jump to address 0
    #
    # `ccdigest_info` is `{output_size, state_size, block_size, oid_size, oid, initial_state,
    # compress, final}`, so offset 24 is `compress` - and `di->initial_state` (offset 20) is NULL
    # too, which is why the `memcpy` at 0x10c was handed a NULL source and survived on its zero
    # length. `IFSR = 0x0F` is a *permission* fault at page granularity, not a translation fault, so
    # VA 0 is mapped and not executable rather than unmapped; `IFAR = 0` is the branch target.
    #
    # **The reason the prediction could be made at all is a method defect, and it is the useful part
    # of this step.** Every prediction in this project has come from an object's relocation table -
    # `objdump -dr | grep R_ARM_CALL`, or `nm -u` - and an indirect call has **no relocation**, so it
    # is invisible to both. This object has three (`blx r3` at 0x58, 0x130, 0x198) and they are not
    # in the list that was read. Experiments 207-212 were not wrong because the objects' stops
    # happened at direct calls reached first; `cchmac_init` is the first object where a call through
    # a pointer was reachable before any direct call, and the gap showed. The object's call graph is
    # `objdump -d` and a grep for `blx` / `ldr pc`, and - because this branch is taken on *data* -
    # the register values feeding its branches have to be read too.
    #
    # **The frontier is therefore a value, not a symbol: `ccsha1_eay_di` needs its definition.**
    # `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` - 4932 bytes, defining `ccsha1_eay_di` and
    # `sha1_compress`, referencing `ccdigest_final_64be` and `ccsha1_initial_state` - is what turns
    # the zeroed stand-in into an initialized `ccdigest_info`. That step is taken deliberately rather
    # than reached, which is what experiment 212's doc said it should be, and it ends the stretch of
    # runs that pass on empty parameters: three fields of the same stand-in have now been observed to
    # be zero, one per experiment (`output_size`, `initial_state`, `compress`).
    OSFMK_CCHMAC_INIT_OBJ=${STAGE90_ENTRY_CCHMAC_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o}
    # `osfmk/corecrypto/ccsha1/src/ccsha1_eay.c`, **not** named by a stub hit - experiment 213's run
    # produced none, so this step is the first whose frontier is a *value* rather than a symbol. It is
    # the one experiment 212's doc said should be taken deliberately: 4932 bytes of text, defining
    # `ccsha1_eay_di` (`R`) and `sha1_compress` (`t`), referencing `ccdigest_final_64be` and
    # `ccsha1_initial_state`.
    #
    # **What it fixes, field by field.** `ccsha1_eay_di` is a `ccdigest_info`, whose layout is
    # `{output_size, state_size, block_size, oid_size, oid, initial_state, compress, final}`. Its
    # `.rodata` relocations say exactly what goes in the four pointer slots:
    #
    #     00000010 R_ARM_ABS32  .rodata.str1.1         ; oid         - the OID string
    #     00000014 R_ARM_ABS32  ccsha1_initial_state   ; initial_state - NEW, becomes a stub
    #     00000018 R_ARM_ABS32  .text                  ; compress      - sha1_compress, same object
    #     0000001c R_ARM_ABS32  ccdigest_final_64be    ; final         - NEW, becomes a stub
    #
    # Three of those six fields have each been observed to be zero by one run: `output_size` by
    # experiment 212's `bhi` guard, `initial_state` by the NULL source of a zero-length `memcpy` in
    # experiment 213, and `compress` by experiment 213's `blx r3` at 0x130 - the prefetch abort at
    # address 0. After this step `compress` is `sha1_compress`, which is a **leaf with no calls and
    # no branches to check** (`objdump -dr` on it shows zero R_ARM_* entries), so `cchmac_init`'s
    # indirect call lands in real code for the first time.
    #
    # **The prediction is `stub_hit=cchmac_update`**, made the corrected way - disassembly plus the
    # register values, not the relocation list. With `di` non-zero, `cchmac_init`'s first branch goes
    # the other way (`block_size` 64 vs `key_len` 20, so `bcs 0x94` is taken and the 0x94 arm runs
    # instead of the 0x28 arm), it reaches `blx r3` twice with `di->compress` now real, and returns.
    # `hmac_dbrg_update` then executes exactly four instructions before its next call:
    #
    #     3b4: bl   cchmac_init       ; real, returns
    #     3b8: ldr  r2, [r4, #16]     ; no branch and no indirect call anywhere between
    #     3c8: bl   cchmac_update     ; <-- STUB, the stop
    #
    # `cchmac_update` is still undefined: 4 bytes of object, a tail call to `ccdigest_update`, and
    # nothing has linked it. `ccdigest_final_64be` and `ccsha1_initial_state` arrive as new stubs in
    # this step and are *not* reached first - the first is `di->final`, which `cchmac_init` does not
    # use, and the second is only ever a `memcpy` source.
    OSFMK_CCSHA1_EAY_OBJ=${STAGE90_ENTRY_CCSHA1_EAY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccsha1_eay.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_update.c`, named by experiment-214's
    # `stub_hit=cchmac_update`. **Four bytes of text**, the smallest object this link will ever carry,
    # and its whole content is one instruction:
    #
    #     cchmac_update:  eafffffe  b  <ccdigest_update>       (R_ARM_JUMP24)
    #
    # So it resolves `cchmac_update` and adds nothing at all: `ccdigest_update` is *already* a stub in
    # this image, added by experiment 213. Checked the corrected way as well - a tail call is not a
    # `blx`, and the object has no other branch or indirect call in it.
    #
    # **The prediction is `stub_hit=ccdigest_update`**, and the `b` lands in the stub, so nothing past
    # it matters for this run. What is worth recording is what that stub will do once it is real,
    # because it is the first object in this sequence to make a *second* call through a pointer this
    # project has just repaired - 280 bytes, two indirect calls, and every input it reads is real as
    # of experiment 214:
    #
    #       28: udiv r1, r5, r2     ; 32-bit hardware divide, real on this core
    #       30: ldr  r3, [r7, #24]  ; di->compress
    #       3c: blx  r3             ; -> sha1_compress, real and a leaf
    #       5c: ldmib r7, {r0, r2}  ; di->state_size = 20, di->block_size = 64
    #
    # So the step after next should be the first in which a `ccdigest_*` helper runs to completion on
    # the device, and its prediction is to be made from that disassembly rather than from the
    # relocation list - which is the rule experiment 213 paid for.
    OSFMK_CCHMAC_UPDATE_OBJ=${STAGE90_ENTRY_CCHMAC_UPDATE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_update.o}
    # `osfmk/corecrypto/ccdigest/src/ccdigest_update.c`, named by experiment-215's
    # `stub_hit=ccdigest_update` - which is also where the `b` of the step above landed. **280 bytes
    # of text, two indirect calls, one definition, one reference** (`memcpy`).
    #
    # This is the first object in the sequence whose correctness rests on a value this project
    # repaired in an earlier step rather than on a symbol it is linking now: `di` is the real
    # `ccsha1_eay_di` from experiment 214, so `di->compress` is `sha1_compress` (a leaf, no calls, no
    # branch), `di->state_size` is 20, `di->block_size` is 64, and the `udiv` at 0x28 is a hardware
    # divide that this core really has:
    #
    #       28: udiv r1, r5, r2     ; 32-bit hardware divide, real on this core
    #       30: ldr  r3, [r7, #24]  ; di->compress
    #       3c: blx  r3             ; -> sha1_compress, real and a leaf
    #       5c: ldmib r7, {r0, r2}  ; di->state_size = 20, di->block_size = 64
    #
    # Its one reference is `memcpy`, real since long before this sequence, and its indirect calls go
    # through `di` - so this is the first object since 212 with **no stubbed symbol on its own path at
    # all**. Every input it reads is real; it is the value argument from experiment 214 that made it
    # so, which is why that step was taken deliberately rather than waited for.
    #
    # **The prediction is `stub_hit=cchmac_final`**, made the corrected way - from the disassembly of
    # the *caller*, plus the register values feeding its branches, not from a relocation list. With
    # `ccdigest_update` real, `hmac_dbrg_update` continues past its call at 0x3c8, and the arms
    # converge: `cchmac_final` is reached unconditionally, whichever of the `da`/`db`/`dc` emptiness
    # tests at 0x3e0/0x404 comes out. Both arms are `bl`, so this is visible as a direct call - the
    # two are `bl 0x4b4` after the fall-through and `bl 0x4c0` on the `bne 0x49c` arm:
    #
    #     3dc: bl   cchmac_update     ; real now (a tail call into ccdigest_update)
    #     3e0: cmp  r9, #0            ; the da/db/dc emptiness tests
    #     404: bne  420               ; -> 418: bl cchmac_update   (real)
    #     49c: bne  4bc               ; -> 4c0: bl cchmac_final    (STUB)
    #     4a4: bl   cchmac_update     ; real
    #     4b4: bl   cchmac_final      ; <-- STUB, the stop, on the fall-through arm
    #
    # The reason the two arms are worth writing out is that the *previous* step's answer would have
    # been wrong had it been read off a relocation list, and there is no way to tell the two cases
    # apart except by reading the disassembly: here both arms happen to lead to the same symbol, so
    # the old method would have got this one right by luck.
    #
    # `cchmac_final` is 120 bytes and references `memcpy`; it is the last of the four `cchmac_*`
    # functions. After it, `di->final` is `ccdigest_final_64be` - which `nm` has reported as a function
    # stub since experiment 214 - and that is the prediction after this one, subject to the same rule.
    OSFMK_CCDIGEST_UPDATE_OBJ=${STAGE90_ENTRY_CCDIGEST_UPDATE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccdigest_src_ccdigest_update.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_final.c`, named by experiment-216's `stub_hit=cchmac_final`.
    # **120 bytes of text, no data, no `.bss`, 1 definition, 1 reference (`memcpy`), 2 indirect calls**
    # - 0x24, and a tail `bx r3` at 0x74 that is unreachable while the first one is a stub.
    #
    # The step it took to get here was the first in this sequence whose object had *no* stubbed symbol
    # on its own path: `ccdigest_update` ran to completion, made an indirect call into real code
    # (`sha1_compress`) and returned, and did a `udiv` on the way. Its own stop was therefore in its
    # *caller*, which is the same shape this step has.
    #
    # **The prediction is `stub_hit=ccdigest_final_64be`**, and unlike the last two this one needs no
    # arm analysis at all, because the object has no conditional branch before its first indirect
    # call:
    #
    #      0: push  {r4, r5, r6, lr}
    #      4: mov   r6, r0            ; r6 = di     (cchmac_final(di, ctx, out) - di first)
    #      8: ldr   r0, [r0, #4]      ; di->state_size = 20
    #      c: ldr   r3, [r6, #28]     ; di->final   = ccdigest_final_64be
    #     14: add   r0, r1, r0        ; ctx + state_size: the state area inside the hmac ctx
    #     1c: add   r2, r0, #8
    #     20: mov   r0, r6            ; -> di
    #     24: blx   r3               ; <-- STUB, the stop
    #
    # The argument order is checked against the header rather than the call site, because this project
    # has been wrong about argument order before: `void cchmac_final(const struct ccdigest_info *di,
    # cchmac_ctx_t ctx, unsigned char *digest)` (`EXTERNAL_HEADERS/corecrypto/cchmac.h:84`). So `r6` is
    # `di`, `[r6, #28]` is `di->final`, and it is not a field of the context. The caller agrees:
    # `hmac_dbrg_update` sets `r0 = r5, r1 = r8` at 0x4ac/0x4b0 immediately before `bl cchmac_final`,
    # and `cchmac_init` reads `di->block_size` and `di->state_size` out of the same `r5`.
    #
    # `cchmac_final` resolves one symbol and adds nothing - `memcpy` is long since real. The step after
    # it will be `osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o` (500 bytes, **zero undefined
    # symbols**, two indirect calls both through `di->compress`), the object that finally makes
    # `di->final` real; its stop will be past itself rather than inside itself, so its prediction will
    # come from `hmac_dbrg_update` again.
    OSFMK_CCHMAC_FINAL_OBJ=${STAGE90_ENTRY_CCHMAC_FINAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_final.o}
    # `osfmk/corecrypto/ccsha1/src/ccdigest_final_64be.c`, named by experiment-217's
    # `stub_hit=ccdigest_final_64be` - the object that finally makes `di->final` real. **500 bytes of
    # text, no data, no `.bss`, 1 definition, 0 undefined symbols, 2 indirect calls** (0xb4 and 0x19c),
    # both through `di->compress`, which is `sha1_compress`.
    #
    # It lives in the `ccsha1` directory although what it defines is a generic digest helper; the
    # frontier rule names the object, not the directory, which is why the object name is spelled out
    # here rather than derived.
    #
    # The step that got here was the first in this sequence with **nothing new executing**: with
    # `cchmac_final` real and `ccdigest_final_64be` still a stub, the run stopped five instructions
    # into `cchmac_final` - its `blx r3` at 0x24, with `r3 = [di + 28]` - so that step's measurement
    # was its own accounting and nothing else. Worth remembering when reading a future cost table: a
    # clean stub hit one symbol further along does not by itself mean anything new ran.
    #
    # **The prediction is `stub_hit=cchmac`.** After `cchmac_final` returns, `hmac_dbrg_update`
    # resumes at 0x4b8 or 0x4c4, and the two arms differ - but they call the *same* symbol, so this
    # prediction needs the arm analysis and does not care what its outcome is:
    #
    #     4b4: bl   cchmac_final      ; real now, returns
    #     4b8: b    4d0               ; ---- arm A ----
    #     4c0: bl   cchmac_final      ; real now, returns
    #     4c4: orr  r0, r6, r9        ; ---- arm B: (da_len | db_len)
    #     4c8: cmp  r0, #1
    #     4cc: bne  578               ; if not 1 -> arm B's own path
    #     4f8: bl   cchmac            ; <-- STUB, arm A's stop
    #     578: mov  r7, #0
    #     5a8: bl   cchmac            ; <-- STUB, arm B's stop
    #
    # `cchmac` is the one-shot init/update/final wrapper (`osfmk_corecrypto_cchmac_src_cchmac.o`, 128
    # bytes), still a 12-byte stub at 0x0023967c. This is the only kind of "safe" that is not the same
    # as experiment 216's: there, both arms converged and the arm did not matter; here the arms are
    # different code and happen to end at the same symbol. If this prediction is wrong, the mistake is
    # in reading the disassembly, not in a guess about an argument.
    #
    # The step after this one is the one that matters more: with `ccdigest_final_64be` real,
    # `cchmac_final` runs to completion for the first time - its `memcpy`, its arithmetic block, and
    # both of its calls through `di->final`, the second a tail call that returns straight to
    # `hmac_dbrg_update`. That is the first time the whole HMAC finalisation executes on this device.
    # The qualifier from experiment 214 still stands: the state it finalises started from
    # `ccsha1_initial_state`, still a zeroed stub, so the *machinery* completes and the *value* is not
    # SHA-1's.
    OSFMK_CCDIGEST_FINAL_64BE_OBJ=${STAGE90_ENTRY_CCDIGEST_FINAL_64BE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o}
    # `osfmk/corecrypto/cchmac/src/cchmac.c`, named by experiment-218's `stub_hit=cchmac`. **128
    # bytes of text, no data, no `.bss`, 1 definition, 4 references** - `cchmac_init`, `cchmac_update`
    # and `cchmac_final`, all three real as of experiment 217, and `cc_clear`, still a 12-byte stub.
    #
    # The step that got here is the first in this stretch whose stop is *evidence of completion*
    # rather than merely of arrival: `hmac_dbrg_update` can only reach `cchmac` if `cchmac_final`
    # returned, and `cchmac_final` returns only if both of its calls through `di->final` entered real
    # code and returned. Each of those is a full generic digest finalisation with two `blx r3` calls
    # into `sha1_compress`. So that run executed the complete HMAC-SHA1 finalisation - inner digest,
    # the `memcpy` into the outer context, outer digest - while `ccsha1_initial_state` was still a
    # zeroed stub, which is the qualifier that keeps the claim to the *machinery* and not the value.
    #
    # **The prediction is `stub_hit=cc_clear`**, and it needs nothing beyond the object's shape,
    # because `cchmac` has no branches at all:
    #
    #      0: push  {r4, r5, r6, sl, fp, lr}
    #     2c: sub   r5, sp, r0        ; the HMAC context, VLA-sized on the stack
    #     34: mov   r0, r4            ; di
    #     38: mov   r1, r5            ; ctx
    #     3c: bl    cchmac_init       ; real
    #     50: bl    cchmac_update     ; real (a tail call into ccdigest_update)
    #     60: bl    cchmac_final      ; real since 217
    #     70: add   r0, r0, #12       ; cchmac_ctx_size, for the wipe
    #     74: bl    cc_clear          ; <-- STUB, the stop
    #     7c: pop   {r4, r5, r6, sl, fp, pc}
    #
    # Straight-line code means no arm analysis and no data dependence, which makes this the least
    # interesting prediction in the sequence and the most certain. The step after it will be
    # `osfmk_corecrypto_cc_src_cc_clear.o`, the first object in this stretch that is not in the
    # `cchmac`/`ccdigest` family.
    OSFMK_CCHMAC_OBJ=${STAGE90_ENTRY_CCHMAC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac.o}
    # `osfmk/corecrypto/cc/src/cc_clear.c`, named by experiment-219's `stub_hit=cc_clear`. **20 bytes
    # of text, no data, no `.bss`, 1 definition, 1 reference**, and the whole of it is an argument
    # shuffle into a tail call:
    #
    #      0: mov  r3, r0        ; n
    #      4: mov  r0, r1        ; p     cc_clear(n, p) -> memset_s(p, n, 0)
    #      8: mov  r1, r3
    #      c: mov  r2, #0
    #     10: b    <memset_s>    ; (R_ARM_JUMP24)
    #
    # The step that got here is the first in which a whole one-shot HMAC ran: the stop was inside
    # `cchmac`, at its closing wipe, so `cchmac_init`, `cchmac_update` and `cchmac_final` had all
    # returned - a complete HMAC-SHA1 from key to digest with every function on the path real, and
    # with `ccsha1_initial_state` still a 20-byte zeroed stub, so the machinery complete and the value
    # not SHA-1's.
    #
    # **The prediction is `stub_hit=memset_s`.** This is the case the frontier rule was built for: the
    # object named by the previous run reaches for a *new* symbol the moment it executes, and that
    # symbol is not in the image - `grep -w memset_s out/stage90/xnu_arm_entry_undef.txt` is empty
    # today. So this step **adds** an undefined symbol for the first time since experiment 214, and the
    # count stays at 364: one resolved, one added.
    #
    # `memset_s` is not `memset`. The image already has `memset` and `secure_memset` at the *same*
    # address 0x00202dac - one body, two names, from `osfmk/arm/bzero.s` - and `memset_s` is a
    # different function in `osfmk/kern/memset_s.c`: C11 Annex K shape, clamps `n` to `smax`, returns
    # EINVAL/E2BIG/EOVERFLOW, and exists so the wipe cannot be optimised away. Its own reference is
    # `secure_memset`, which is already real, so linking it in the step after this resolves one symbol
    # and adds none, and the stop after that moves on to `cc_cmp_safe` at 0x5e4.
    #
    # Worth writing out, because the run will not stop where a reading of the call list suggests: with
    # `cc_clear` real, control returns to `cchmac` at 0x78, and `hmac_dbrg_update` continues at 0x4fc
    # with two further `cchmac` invocations (0x5a8 and 0x5cc) *before* `cc_cmp_safe` - and each of
    # those calls `cc_clear` again and therefore `memset_s` again.
    OSFMK_CC_CLEAR_OBJ=${STAGE90_ENTRY_CC_CLEAR_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o}
    # `osfmk/kern/memset_s.c`, named by experiment-220's `stub_hit=memset_s` - reached not from the
    # caller's next missing call but from inside `cchmac`, whose closing wipe is `cc_clear` and whose
    # `cc_clear` is a tail call to this. **80 bytes of text, no data, no `.bss`, 1 definition, 1
    # reference**, and that reference - `secure_memset` - is already real at 0x00202dac, where plain
    # `memset` shares the address. So this object has nothing missing on its own path and cannot stop
    # anywhere itself.
    #
    # It is the C11 Annex K shape, and its whole point is that the wipe cannot be optimised away:
    #
    #     int memset_s(void *s, size_t smax, int c, size_t n)
    #     {
    #             if (s == NULL) return EINVAL;
    #             if (smax > RSIZE_MAX) return E2BIG;
    #             if (n > smax) { n = smax; err = EOVERFLOW; }
    #             secure_memset(s, c, n);      /* osfmk/arm/bzero.s - real */
    #             return err;
    #     }
    #
    # **The prediction is `stub_hit=cc_cmp_safe`**, and unlike the last one this is a plain list-walk:
    # the object resolves cleanly, so the stop is the caller's next *missing* call, not a call the
    # object makes. After `memset_s` returns, the first `cc_clear` (inside the first `cchmac`)
    # completes, `cchmac` pops at 0x78, `hmac_dbrg_update` resumes at 0x4fc, and every remaining call
    # on that path is real - `cchmac_init` 0x510, `cchmac_update` 0x524/0x538/0x564, `cchmac_final`
    # 0x574, `cchmac` 0x5a8 and 0x5cc - until `cc_cmp_safe` at 0x5e4.
    #
    # **Two things to have written down before the step after that**, because it is the first in this
    # sequence that could end somewhere other than a stub hit, and the first that could execute NEON:
    #
    #   1. `cc_cmp_safe` is called with `state->vsize` (20) and two 20-byte V buffers, and its result
    #      forks at 0x5e8. On the *equal* arm the code wipes the state and calls `cc_try_abort`, whose
    #      one reference is `panic` - real since the panic object was linked - and XNU's ARM `panic`
    #      reaches `TRAP_DEBUGGER` (a `udf`) and then `panic_spin_forever`. That would show up as an
    #      `exception:` line rather than a `stub_hit=`, and it is the DRBG's own FIPS 140-2 4.9.2
    #      conditional test failing on purpose (`ccdrbg_nisthmac.c:211`, `:440`), not a defect.
    #   2. `cc_cmp_safe.o` contains NEON (`vmov.i32 q8`, `vld1.8`, `veor`, `vorr`) on the path taken
    #      when the length is 32 or more (`cmp r0,#32; bcc 0xc4`). The call here passes 20, so the
    #      scalar path runs and no NEON instruction executes - but a later call with 32 or more would
    #      be the first NEON code in this project's history to run, and whether CPACR/FPEXC permit it
    #      in this CPU state is a question nothing has asked yet.
    OSFMK_MEMSET_S_OBJ=${STAGE90_ENTRY_MEMSET_S_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_memset_s.o}
    # `osfmk/corecrypto/cc/src/cc_cmp_safe.c`, named by experiment-221's `stub_hit=cc_cmp_safe`.
    # **376 bytes of text, no data, no `.bss`, 1 definition, 0 undefined symbols** - so linking it
    # leaves *nothing* missing anywhere in the DRBG, and this is the last object of the corecrypto
    # stretch. It is a constant-time compare: the XOR-accumulate loop, then a single test of the
    # accumulator, with NEON used only for lengths of 32 or more (`cmp r0,#32; bcc 0xc4`). Both calls
    # in play pass `state->vsize` = 20, so the scalar path runs and no NEON instruction executes.
    #
    # The step that got here ran the whole HMAC_DRBG instantiate sequence up to this compare: five or
    # more complete HMAC-SHA1 computations in one boot, with the stop one call past them. What is left
    # of the DRBG after this is the compare, a return, and the same compare in `generate`.
    #
    # **The prediction is `stub_hit=bsd_scale_setup`** - the longest prediction in this sequence, and
    # the first that says the PRNG is *finished*. Every step was read rather than assumed:
    #
    #   1. cc_cmp_safe returns non-zero -> 0x5ec bne 0x608
    #   2. hmac_dbrg_update returns r7 = 0 (0x578 mov r7,#0; 0x60c mov r0,r7) = CCDRBG_STATUS_OK
    #   3. `init` returns OK - 0xb8 is its last call, only a pop follows
    #   4. early_random's two `if (rc != CCDRBG_STATUS_OK) panic(...)` checks pass
    #   5. early_random -> ccdrbg_generate -> `generate`; addl_len = 0 so no hmac_dbrg_update, and
    #      bytesLeft = 0, so 0x210 -> 0x254 -> 0x25c cchmac (real) -> 0x290 cc_cmp_safe (real now)
    #      -> copies 8 bytes out -> 0x2b4 return
    #   6. early_random returns the 8 bytes; arm_init stores them in `__stack_chk_guard`:
    #          328: bl  early_random
    #          330: bic r0, r0, #0xff00     ; the stack canary comes from early_random
    #          338: str r0, [r1]            ; __stack_chk_guard
    #          340: bl  machine_startup
    #   7. machine_startup (real, 0x206b84): four PE_parse_boot_argn (real), then
    #          206c6c: bl kernel_bootstrap - unconditional, the last thing it does
    #   8. kernel_bootstrap (real, 0x20d560): _consume_printf_args (real, no calls),
    #      PE_parse_boot_argn x4 and PE_parse_boot_arg_str (all real - the last calls DTLookupEntry
    #      and DTGetProperty, both real), and then the first missing symbol:
    #          20d604: bl bsd_scale_setup   <-- STUB at 0x00239900
    #
    # The only conditional branch between `kernel_bootstrap`'s entry and that call skips a store, not
    # a call, so the stop is not data-dependent.
    #
    # **The caveat, and it is new: this prediction's alternative outcome is a panic.** Both FIPS
    # compares branch to `cc_clear` + `cc_try_abort` on the *equal* result, and `cc_try_abort`'s single
    # reference is `panic`, which is real in this image; XNU's ARM `panic` reaches `TRAP_DEBUGGER` (a
    # `udf`) and then `panic_spin_forever`. So a run ending in an `exception:` line rather than a
    # `stub_hit=` would mean the DRBG's own health check fired - a finding, not a defect - and a run
    # stopping at some *other* symbol in the DRBG would mean the reading above is wrong, with the
    # symbol naming where.
    OSFMK_CC_CMP_SAFE_OBJ=${STAGE90_ENTRY_CC_CMP_SAFE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o}
    # `bsd/dev/unix_startup.c`, named by experiment-222's `stub_hit=bsd_scale_setup` - the first
    # object in this sequence that is not XNU's ARM or corecrypto layer, and the first that is reached
    # from `kernel_bootstrap` rather than from `arm_init`.
    #
    # **1291 bytes of text, 96 of data, 48 of `.bss`, 4 definitions, 21 references**, and the whole of
    # `bsd_scale_setup` is one instruction:
    #
    #     0000045c <bsd_scale_setup>:
    #      45c: eafffffe  b  0 <bsd_exec_setup>        (R_ARM_JUMP24)
    #
    # The step that got here is the one that finished the PRNG: with `cc_cmp_safe` real, everything
    # from `hmac_dbrg_update`'s FIPS compare up to `kernel_bootstrap` returned, and the stop is inside
    # `kernel_bootstrap`. `arm_init` now completes; `__stack_chk_guard` is set from `early_random`'s
    # return value; `machine_startup` runs. The qualifier still holds and is not weakened by any of it:
    # `ccsha1_initial_state` is 20 zero bytes, so the canary is a deterministic function of the device
    # tree and the timebase, not of entropy - the machinery is real and the value is not SHA-1's.
    #
    # **The prediction is `stub_hit=bsd_exec_setup`**, and it needs no arm analysis: the tail call is
    # unconditional and `bsd_exec_setup` is defined by `bsd_kern_bsd_init.o` (`bsd/kern/bsd_init.c`),
    # which is not linked yet. So this step **adds** the symbol the next run stops on, the same shape
    # as experiment 220's `cc_clear` -> `memset_s`.
    #
    # The cost table for this step will not look like the last seven, and deliberately: this object
    # defines **four** symbols (`bsd_startupearly`, `bsd_mbuf_cluster_reserve`, `bsd_bufferinit`,
    # `bsd_scale_setup`) and references 21, of which five (`__aeabi_uldivmod`, `bzero`, `panic`,
    # `PE_get_default`, `PE_parse_boot_argn`) are already real and the other sixteen are either already
    # stubs from earlier objects or arrive now - among them `bsd_exec_setup`, `bufinit`,
    # `kernel_memory_allocate`, `kmem_suballoc`, `sysctl_handle_int`, and the storage the BSD side
    # needs (`kernel_map`, `mb_map`, `mbutl`, `desiredvnodes`, `sane_size`, `nmbclusters`,
    # `mbuf_default_ncl`, `buf_headers`, `sysctl__kern_children`, `tcp_recvspace`, `tcp_sendspace`).
    # The build reports 300 -> 304 function stubs and 61 -> 67 storage stubs, and the entry image's
    # file size moves for the first time since experiment 214. The undefined count goes **up**, which
    # is expected and is not a regression.
    OSFMK_BSD_DEV_UNIX_STARTUP_OBJ=${STAGE90_ENTRY_BSD_DEV_UNIX_STARTUP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_dev_unix_startup.o}
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
    require "$ARM_KERN_DEBUG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_CONSISTENT_DEBUG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_KPRINTF_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_SERIAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CONSOLE_VIDEO_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CONSOLE_SERIAL_GENERAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_IO_MAP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_LOOSE_ENDS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_CACHES_ASM_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$OSFMK_ARM_CACHES_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_PRNG_RANDOM_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDRBG_NISTHMAC_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCSHA1_EAY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_UPDATE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDIGEST_UPDATE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_FINAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDIGEST_FINAL_64BE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CC_CLEAR_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_MEMSET_S_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CC_CMP_SAFE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_BSD_DEV_UNIX_STARTUP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    LINK_OBJS+=("$ARM_INIT_OBJ" "$ARM_DATA_OBJ" "$ARM_BCOPY_OBJ" "$ARM_BZERO_OBJ" "$ARM_CPU_OBJ" \
                "$ARM_PE_INIT_OBJ" "$ARM_STRLCPY_OBJ" "$ARM_STRLEN_OBJ" "$ARM_STRNCPY_OBJ" "$ARM_STRNLEN_OBJ" "$ARM_DEVICE_TREE_OBJ" \
                "$ARM_PE_IDENTIFY_OBJ" "$ARM_SUBRS_OBJ" "$ARM_STRNCMP_OBJ" "$ARM_PE_GEN_OBJ" \
                "$ARM_BOOTARGS_OBJ" "$ARM_PE_BOOTARGS_OBJ" "$ARM_MACHINE_ROUTINES_OBJ" "$ARM_CPU_COMMON_OBJ" "$ARM_KERN_THREAD_OBJ" "$ARM_KERN_TIMER_OBJ" "$ARM_MACHINE_ROUTINES_ASM_OBJ" "$ARM_ARM_RTCLOCK_OBJ" "$ARM_KERN_STARTUP_OBJ" "$ARM_KERN_TIMER_CALL_OBJ" "$ARM_KERN_LOCKS_OBJ" "$ARM_LOCKS_ARM_OBJ" "$ARM_ARM_TIMER_OBJ" "$ARM_ARM_CPUID_OBJ" "$ARM_ARM_MACHINE_CPUID_OBJ" "$ARM_KERN_PROCESSOR_OBJ" "$ARM_KERN_PROCESSOR_DATA_OBJ" "$ARM_MACHINE_ROUTINES_COMMON_OBJ" "$ARM_ARM_VM_INIT_OBJ" "$LIBKERN_KERNEL_MACH_HEADER_OBJ" "$VM_VM_RESIDENT_OBJ" "$ARM_PMAP_OBJ" "$ARM_LOWMEM_VECTORS_OBJ" "$ARM_KERN_PRINTF_OBJ" "$BSD_KERN_SUBR_LOG_OBJ" "$ARM_KERN_DEBUG_OBJ" "$PEXPERT_PE_CONSISTENT_DEBUG_OBJ" "$PEXPERT_PE_KPRINTF_OBJ" "$PEXPERT_PE_SERIAL_OBJ" "$OSFMK_CONSOLE_VIDEO_OBJ" "$OSFMK_CONSOLE_SERIAL_GENERAL_OBJ" "$OSFMK_ARM_IO_MAP_OBJ" "$OSFMK_ARM_LOOSE_ENDS_OBJ" "$OSFMK_ARM_CACHES_ASM_OBJ" "$OSFMK_ARM_CACHES_OBJ" "$OSFMK_PRNG_RANDOM_OBJ" "$OSFMK_CCDRBG_NISTHMAC_OBJ" "$OSFMK_CCHMAC_INIT_OBJ" "$OSFMK_CCSHA1_EAY_OBJ" "$OSFMK_CCHMAC_UPDATE_OBJ" "$OSFMK_CCDIGEST_UPDATE_OBJ" "$OSFMK_CCHMAC_FINAL_OBJ" "$OSFMK_CCDIGEST_FINAL_64BE_OBJ" "$OSFMK_CCHMAC_OBJ" "$OSFMK_CC_CLEAR_OBJ" "$OSFMK_MEMSET_S_OBJ" "$OSFMK_CC_CMP_SAFE_OBJ" "$OSFMK_BSD_DEV_UNIX_STARTUP_OBJ")

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

