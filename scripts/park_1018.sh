#!/usr/bin/env bash
# Park 1018 and append its block to records/revert-set.txt.
#
# 1018 = 1015b's switch set VERBATIM (STAGE90_XNU_GLOBAL_FIX=1 and all 11 of its members stay) + exactly
# ONE new switch, STAGE90_XNU_UNDEF_DIRECT=1.  Design: docs/1018-design.md.  Recon: records/1018-recon.txt
# (a7bafd7).
#
# WHY.  1017 (`docs/1017-shared-cache-naming.md`) named, statically and by value, the instruction pid 1's
# frame sits on: un-slid VA 0x3823aa54 = libsystem_platform.dylib `__os_alloc_slow`+0x108 = the Thumb
# `udf #254`, reached ONLY when `_mach_vm_map` returns non-zero.  1017 wrote `# UNMEASURED` for the
# mechanism (return-and-re-trap vs outer-retry), and the RECON found why the capture could not answer it:
# the recorder that would have named the udf (`__wrap_sleh_undef`, entry_trace.c:5537) writes
# `entry_live_write`, which is CAPPED (ENTRY_LIVE_CAP 24576u, entry_stubs.c:4254).  On the recovered 1015b
# capture the cap published `xnu_live_capped` at :29963 and every capped record after it was dropped,
# while the frontier sits ~4000 records later at :33977.  So `xnu_live_undef_seq` is ZERO there not
# because no udf happened but because the channel was exhausted first (records/1018-recon.txt).
#
# THE ARM (one switch, one key move).  STAGE90_XNU_UNDEF_DIRECT=1 routes `__wrap_sleh_undef`'s six keys
# through `entry_write_kv` (the DIRECT channel, entry_stubs.c:4476) instead of `entry_live_write`, so the
# witness survives the cap; UNDEF_LIVE_MAX widens 4u -> 64u on the direct path only, bounded by the
# console's own 0x00200000u - 12u (entry_stubs.c:4019).  OFF is byte-identical (#if/#else around the two
# macro definitions, never a widened constant).
#
# **ENTRY-TOOLCHAIN ONLY.**  The recorder and its one call site are both in entry_trace.c, so there is
# NO kernel-pool edit (unlike 1015b's GLOBAL_FIX, which MOVES osfmk/arm/pmap.c) and NO tree edit.  The
# TREE does NOT move; the ENTRY BIN and the PAYLOAD do (the entry blob is embedded).
#
# THE 1018 VERDICT CELLS (docs/1018-design.md §2/§3/§4; read by tools/check_1018_capture.py):
#   U-FIRES-DIES  - the udf fires at the frontier AND the run stops   => the trap TERMINATED pid 1
#   U-FIRES-LOOPS - the udf fires and the sampler pc stays put         => the handler returns, re-traps
#   U-FIRES-ONCE  - the udf fires once and the sampler pc MOVES        => the trap is a TRANSITION
#   U-SILENT      - the witness is LIVE yet fires at no sampled pc     => 1017's static id is CONTRADICTED
#   U-CTL         - the witness never fired / no boot self-test row    => INSTRUMENT_UNSOUND, WITHHOLD
# The reader REQUIRES BOTH control halves (the boot self-test row AND a `_seq` row), so a pre-arm capture
# whose witness is still capped WITHHOLDS (U-CTL) rather than reading as U-SILENT.
#
# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, the base is never
# written - fully reversible.  fastboot boot only, NEVER flash.
set -euo pipefail
cd "$(dirname "$0")/.."

LIVE=out/stage90
REC=records/revert-set.txt
MEMBERS=(xnu_arm_entry.bin xnu_arm_entry.elf xnu_arm_entry.h xnu_arm_entry-config.txt \
         xnu_arm_entry-sources.txt stage90.bin stage90.elf stage90.img stage90-qcdt.img \
         stage90_fixture.macho stage90-build-config.txt SHA256SUMS.txt)

# The arm name IS the entry bin's sha8 (the project's rule).  Derive it, and refuse to re-park the
# RECENT PRESSED arms under a new name (build 1018 first).
ARM=armed-d13-$(sha256sum "$LIVE/xnu_arm_entry.bin" | cut -c1-8)
[[ $ARM == armed-d13-16fe8885 ]] && { echo "REFUSING: $LIVE holds 1015b's arm (16fe8885), the LIVE base this rung builds on; build 1018 first"; exit 1; }
[[ $ARM == armed-d13-e51d14fa ]] && { echo "REFUSING: $LIVE holds 1015's arm (e51d14fa); build 1018 first"; exit 1; }
[[ $ARM == armed-d13-29ec9e48 ]] && { echo "REFUSING: $LIVE holds 1014's arm (29ec9e48); build 1018 first"; exit 1; }
[[ $ARM == armed-d13-9bf25f8f ]] && { echo "REFUSING: $LIVE holds 1013b's arm (9bf25f8f); build 1018 first"; exit 1; }
[[ $ARM == armed-d13-ffdc8ba1 ]] && { echo "REFUSING: $LIVE holds 1012's arm (ffdc8ba1); build 1018 first"; exit 1; }

# 1018's NEW key MUST be in the record ([[mi4-one-value-two-definitions]]): the park refuses an arm whose
# record does not carry STAGE90_XNU_UNDEF_DIRECT=1 (the gate reads THIS line).
grep -q '^STAGE90_XNU_UNDEF_DIRECT=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_UNDEF_DIRECT=1 - 1018's direct channel was not recorded this run"; exit 1; }
# 1015b's GLOBAL_FIX stays on as a REGRESSION (it is why the storm is gone and the frontier is the udf).
grep -q '^STAGE90_XNU_GLOBAL_FIX=1$' "$LIVE/xnu_arm_entry-config.txt" || {
  echo "REFUSING: the record does not carry STAGE90_XNU_GLOBAL_FIX=1 - 1015b's fix must stay on, or the single-address retry storm would be re-litigated"; exit 1; }

# BY VALUE, at the freeze point: 1018's change is that `__wrap_sleh_undef` in the LINKED image branches
# to the DIRECT writer.  Read it off the wrapper's own symbol range ([[mi4-linked-code-order-is-not-
# source-order]]).  This is the build clause re-checked on the FROZEN artifact, so a park that lost the
# switch fails HERE on its own claim.
_ua=$(arm-none-eabi-nm "$LIVE/xnu_arm_entry.elf" | awk '$3=="__wrap_sleh_undef"{print "0x" $1; n++} END{exit(n==1?0:1)}') || {
  echo "REFUSING: __wrap_sleh_undef is not in the parked image (or is defined more than once) - the undef witness is gone"; exit 1; }
_ub=$(arm-none-eabi-nm -n "$LIVE/xnu_arm_entry.elf" | awk -v s="${_ua#0x}" 'BEGIN{sv=strtonum("0x" s)}{a=strtonum("0x" $1)} a>sv{if(!d){print "0x" $1; d=1}}')
[[ -n $_ub ]] || { echo "REFUSING: no symbol follows __wrap_sleh_undef ($_ua) - its range cannot be read"; exit 1; }
_ud=$(arm-none-eabi-objdump -d "$LIVE/xnu_arm_entry.elf" --start-address="$_ua" --stop-address="$_ub")
_kv=$(grep -c '<entry_write_kv>' <<<"$_ud" || true)
_lw=$(grep -c '<entry_live_write>' <<<"$_ud" || true)
[[ $_kv -ge 6 && $_lw -eq 0 ]] || {
  echo "REFUSING: the parked image's __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv $_kv time(s) and entry_live_write $_lw time(s) - with the switch on it must be >=6 direct and 0 counted (the six keys off the capped channel). A park that kept the counted branch freezes an arm whose witness the cap can still silence."; exit 1; }
echo "park_1018: by value - __wrap_sleh_undef ($_ua..$_ub) branches to entry_write_kv x$_kv, entry_live_write x$_lw (the witness is on the DIRECT channel)"

PARK=$LIVE/frozen/$ARM
[[ -d $PARK ]] && { echo "REFUSING: $PARK already exists"; exit 1; }
mkdir -p "$PARK"
for m in "${MEMBERS[@]}"; do cp -p "$LIVE/$m" "$PARK/$m"; done

{
  printf '# %s  -  1018: THE CAP-EXEMPT UNDEF WITNESS (the udf #254 at __os_alloc_slow+0x108 was named\n# by 1017 but the recorder that would confirm it DYNAMICALLY rode the CAPPED channel and fired zero).\n' "$ARM"
  printf '# Switch set is 1015b-s VERBATIM (GLOBAL_FIX and all 11 of its members stay 1) plus\n'
  printf '# STAGE90_XNU_UNDEF_DIRECT=1.  ENTRY-TOOLCHAIN ONLY: no tree edit, no pool move, no --wrap change.\n'
  printf '# The TREE does not move; the ENTRY BIN moves (16fe8885 -> %s) and the PAYLOAD moves.\n#\n' "${ARM#armed-d13-}"
  printf '# WHAT IT MEASURES (docs/1018-design.md; records/1018-recon.txt a7bafd7): 1017 named the\n'
  printf '# instruction pid 1 sits on - un-slid VA 0x3823aa54 = libsystem_platform.dylib __os_alloc_slow\n'
  printf '# +0x108 = the Thumb udf #254 - and wrote # UNMEASURED for the mechanism. The 1015b capture could\n'
  printf '# not answer it because __wrap_sleh_undef (entry_trace.c:5537) writes entry_live_write, CAPPED at\n'
  printf '# 24576u (entry_stubs.c:4254): the cap published xnu_live_capped at :29963 and every capped record\n'
  printf '# after it was dropped, while the frontier sits ~4000 records later at :33977. With the switch on\n'
  printf '# the six keys ride entry_write_kv (the DIRECT channel entry_note_stall_sample uses), so their\n'
  printf '# absence after xnu_live_capped means the udf did not happen.\n#\n'
  printf '# THE 1018 CELLS (tools/check_1018_capture.py): U-FIRES-DIES (the trap terminated pid 1) /\n'
  printf '# U-FIRES-LOOPS (the handler returns and re-traps) / U-FIRES-ONCE (the trap is a transition) /\n'
  printf '# U-SILENT (witness live but fires at no sampled pc: 1017-s static id CONTRADICTED) / U-CTL\n'
  printf '# (withhold). The reader REQUIRES BOTH control halves - the boot self-test row AND a _seq row -\n'
  printf '# so a pre-arm capture with the witness still capped WITHHOLDS rather than reading as U-SILENT.\n#\n'
  printf '# VERIFIED BY VALUE (scripts/build_1018.sh + this park): __wrap_sleh_undef in xnu_arm_entry_trace.o\n'
  printf '# AND in the linked image branches to entry_write_kv (>=6) and not entry_live_write (0), both\n'
  printf '# switch directions.\n'
  printf '# Medium: 968 real iOS 7.1.2 HFSX rootfs over hfs_rmd0; CARD_COW=1, HDD_WRITE=0, base never\n'
  printf '# written - fully reversible. fastboot boot only, NEVER flash.\n'
  printf '# =================================================================================================\n'
  for m in "${MEMBERS[@]}"; do
    h=$(sha256sum "$PARK/$m" | awk '{print $1}')
    b=$(stat -c %s "$PARK/$m")
    printf 'set=%s sha256=%s bytes=%s file=%s role=1018-cap-exempt-undef-witness;%s\n' \
      "$ARM" "$h" "$b" "$m" "$m"
  done
} >> "$REC"

echo "parked $ARM ($(ls -1 "$PARK" | wc -l) members)"
tools/verify_revert_set.sh "$PARK" --set="$ARM" 2>&1 | tail -3