#!/usr/bin/env bash
# Park 990 and append its block to records/revert-set.txt.
#
# 990 = 989's switch set + STAGE90_XNU_SP_ALIGN=1: the D13 tree hands C 8-byte-aligned stacks at its two
# measured gates. TWO tree objects move (unlike 988/989): the ASSEMBLY layer's machine_routines_asm.o
# (astloop's `sub sp,#4`/`add sp,#4` around `blx _ast_taken`) and the POOL's osfmk_arm_pcb.o
# (`machine_stack_attach`/`machine_stack_handoff` round the saved-state block down to 8). The ENTRY BIN
# moves (2e0eebf5 -> ?) and build_entry.sh gains the ENTRY_ARM_KEY, the record emission and a by-value
# refusing clause (tools/check_sp_alignment.py). A fresh checkout needs BOTH stage scripts:
# tools/stage_d13_kextb_login.sh (988's) and tools/stage_d13_sp_align.sh (990's).
#
# WHY (989's press, 2026-10-12, arm armed-d13-2e0eebf5, commit 1bc3fdb, evidence /tmp/press989.log):
# a DECISIVE SUCCESS in the pre-registered sense - the abort MOVED again. 988's dt-census guard HELD
# (xnu_live_dtref_calls=1, ok=0 -> the probe walked only the tree root), the HFSX mount stayed up, and
# the boot ran the WHOLE mount + bsd_init: `Added memory device md0/rmd0` -> `BSD root: md0` ->
# `bsd_init: done` -> `attempting to start init of /sbin/launchd` -> NEW first abort:
#     Fatal exception: sleh_abort: alignment fault; pc=0x8021b6a8 = parse_machfile+0x60c,
#     instruction `f461279f vld1.32 {d18}, [r1 :64]`, r1 = sp+168 = 0xcfdbbb24, sp = 0xcfdbba7c.
# 168 is 8-aligned, so the 8-byte-qualified NEON pair load faults iff sp is 4 (mod 8) - and
# 0xcfdbba7c is 4 (mod 8). Three further aborts on the way (copyout 0x80494970, copyinstr 0x80494d9c,
# bcopy) decode to the same 4-mod-8 C island; parse_machfile's own entry frame (36+268=304) preserves
# the residue, so the exec path was entered at 4.
#
# ROOT CAUSE (990). The D13 tree hands C a 4-mod-8 stack at TWO gates; 4570 has neither:
#   1. `osfmk/arm/pcb.c`: `iss = STACK_IKS(stack) = stack_top - 68`, `sizeof(arm_saved_state_t) = 68 =
#      4 (mod 8)`. `Call_continuation` (cswitch.s:46) loads the continuation's sp straight from
#      `machine.iss`; `fleh_swi` runs syscall handlers on the same value. 4570's thread_kernel_state
#      was 16-aligned (80) - one value, two definitions.
#   2. `osfmk/arm/machine_routines_asm.s:133`: astloop's `stmfd sp!,{r0-r7,lr}` = 9 words = 36 = 4
#      (mod 8) immediately before `blx _ast_taken` - and bsdinit's thread first ran C in ast_taken,
#      entered from thread_return_join (no push of its own), whose sp came from gate 1. The exec chain
#      that died is exactly that: ast_taken -> bsd_ast -> bsdinit_task -> load_init_program -> execve
#      -> __mac_execve -> exec_activate_image -> exec_mach_imgact -> load_machfile -> parse_machfile.
#   Measured by value: pool object osfmk_arm_pcb.o carries movw #16316 (0x3fbc = iss) and movw #16248
#   (0x3f78 = iss->sp); tools/check_sp_alignment.py walks the LINKED elf and shows astloop's
#   cumulative sp delta = -36 = 4 (mod 8) at its bl ast_taken, while __preempt/thread_return_join/
#   preempt_kernel/thread_terminate are all 0.
#
# THE FIX (`tools/stage_d13_sp_align.sh`, gated `#if STAGE90_XNU_SP_ALIGN` with the original text in
# the #else, so switch-OFF is byte-identical): pcb.c rounds BOTH `STACK_IKS(stack)` and
# `kstack - sizeof(arm_saved_state_t)` down to 8 (`& ~7u`; top 4 stack bytes unused), and astloop
# gained `sub sp, sp, #4` / `add sp, sp, #4` in plain (flag-preserving) form. The switch threads
# through build_xnu_arm_kernel.sh (pool DEFINES), assemble_arm_layer.sh (ASFLAGS) and build_entry.sh
# (validation + ENTRY_ARM_KEY + record + the by-value clause that REFUSES a linked image whose sites
# or stack functions do not carry the compiled fix - the same walker that refuses 989's image).
#
# READ: past the pid-1 exec (no vld1.32 {d18} alignment fault; the boot continues into launchd's own
# C) => the 4-mod-8 stack was the wall, and the AST/continuation surface is now clean.
# FALSIFICATION: the SAME fault recurs (`vld1.32 {d18},[r1 :64]`-class at a 4-mod-8 sp) => either the
# switch did not take (the checker should have refused this image - check it ran) or another producer
# of a 4-mod-8 sp exists outside the two gates this walker models.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible. fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule). Derive it, and refuse to park the same
# bytes twice or to re-park 989's arm under a new name.
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-2e0eebf5 ]] && { echo "REFUSING: $LIVE holds 989's arm (2e0eebf5); build 990 first"; exit 1; }
PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  990: THE D13 TREE HANDS C 8-BYTE-ALIGNED STACKS AT ITS TWO MEASURED GATES.\n' "$ARM"
  printf '# Switch set is 989-s + STAGE90_XNU_SP_ALIGN=1; the ENTRY BIN moved (2e0eebf5 -> %s) and TWO tree\n' "${ARM#armed-d13-}"
  printf '# objects move: the assembly layer-s machine_routines_asm.o and the pool-s osfmk_arm_pcb.o (a fresh\n'
  printf '# checkout needs BOTH tools/stage_d13_kextb_login.sh and tools/stage_d13_sp_align.sh).\n#\n'
  printf '# 989 (pressed 2026-10-12, arm armed-d13-2e0eebf5, commit 1bc3fdb, evidence /tmp/press989.log) is a\n'
  printf '# DECISIVE SUCCESS in the pre-registered sense - the abort MOVED. 988-s dt-census guard HELD\n'
  printf '# (xnu_live_dtref_ok=0; the probe walked only the tree root), and the boot ran the WHOLE mount +\n'
  printf '# bsd_init: Added memory device md0/rmd0 -> BSD root: md0 -> bsd_init: done -> attempting to start\n'
  printf '# init of /sbin/launchd -> NEW first abort: sleh_abort: alignment fault at pc=0x8021b6a8 =\n'
  printf '# parse_machfile+0x60c, instruction f461279f vld1.32 {d18}, [r1 :64], r1 = sp+168 = 0xcfdbbb24,\n'
  printf '# sp = 0xcfdbba7c (4 mod 8; 168 is 8-aligned, so the qualified NEON pair load faulted on the sp).\n#\n'
  printf '# ROOT CAUSE (990): the D13 tree has TWO gates 4570 lacks. (1) osfmk/arm/pcb.c: iss =\n'
  printf '# STACK_IKS(stack) = stack_top - 68 and sizeof(arm_saved_state_t) = 68 = 4 (mod 8), and\n'
  printf '# Call_continuation (cswitch.s:46) loads the continuation-s sp straight from machine.iss -\n'
  printf '# fleh_swi runs syscall handlers on the same value; 4570-s thread_kernel_state was 16-aligned (80)\n'
  printf '# - one value, two definitions. (2) machine_routines_asm.s:133: astloop saves 9 words (36 = 4 mod\n'
  printf '# 8) immediately before blx _ast_taken, and bsdinit-s thread first ran C in ast_taken (entered\n'
  printf '# from thread_return_join, no push of its own), whose sp came from gate 1; the exec chain that\n'
  printf '# died is ast_taken -> bsd_ast -> bsdinit_task -> load_init_program -> execve -> __mac_execve ->\n'
  printf '# exec_activate_image -> exec_mach_imgact -> load_machfile -> parse_machfile. By value:\n'
  printf '# osfmk_arm_pcb.o carries movw #16316 (iss) / movw #16248 (iss->sp); check_sp_alignment.py walks\n'
  printf '# the linked elf and shows astloop-s delta -36 = 4 (mod 8) while __preempt (-8), thread_return_join\n'
  printf '# (0), preempt_kernel (0), thread_terminate (16) are all 0 (mod 8).\n#\n'
  printf '# 990: tools/stage_d13_sp_align.sh (gated #if STAGE90_XNU_SP_ALIGN, original text in the #else -\n'
  printf '# switch-OFF byte-identical) rounds STACK_IKS(stack) AND kstack - sizeof(arm_saved_state_t) down to\n'
  printf '# 8 in BOTH pcb.c functions (top 4 stack bytes unused), and adds plain sub sp,#4 / add sp,#4 around\n'
  printf '# astloop-s blx _ast_taken. The switch threads through build_xnu_arm_kernel.sh:772 (pool DEFINES),\n'
  printf '# assemble_arm_layer.sh (ASFLAGS) and build_entry.sh (validation at :1063, ENTRY_ARM_KEY, record\n'
  printf '# emission, and a by-value clause that runs tools/check_sp_alignment.py and REFUSES a linked image\n'
  printf '# whose ast call sites are not 0 mod 8, whose stack functions lack the compiled bic rN,rN,#7, or\n'
  printf '# whose Call_continuation no longer loads sp from TH_PCB_ISS).\n#\n'
  printf '# READ: past the pid-1 exec (no vld1.32 {d18} alignment fault; launchd-s own C runs) => the 4-mod-8\n'
  printf '# stack was the wall. FALSIFICATION: the same fault recurs => the switch did not take (the 990\n'
  printf '# clause should have refused such an image) or another 4-mod-8 producer exists beyond the two\n'
  printf '# gates the walker models.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=990-d13-stack-alignment-at-the-two-measured-gates;-%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3