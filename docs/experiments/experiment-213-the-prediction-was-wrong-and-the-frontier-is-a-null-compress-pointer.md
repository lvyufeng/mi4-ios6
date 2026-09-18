# Experiment 213 — the Prediction Was Wrong, the Run Took a Prefetch Abort at Address Zero, and the Method That Predicted It Was Reading Only Half the Calls

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: exception: prefetch abort
 xnu_entry_kv_written=0x00000054
 xnu_entry_kv_in_dram=0x00000054
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_prefetch_abort_ifar=0x00000000
 xnu_entry_prefetch_abort_ifsr=0x0000000f

No errors detected
```

**`grep -c stub_hit /tmp/cancro-last_kmsg.txt` is 0.** There is no stub hit in this run - not a
different stub, none at all - so the experimental method's usual output is absent and the run has to
be read from the two fault registers instead. `kv_written == kv_in_dram == 0x54 = 84` is exactly the
two `xnu_entry_prefetch_abort_*` lines, so nothing else was in the KV buffer and the transfer to DRAM
worked.

The device recovered on its own, as designed: the fault was caught by the *entry image's* vector
table (`fleh_prefabt`, `entry_stubs.c:718`), not the payload's - the message text is the entry
image's - and `entry_epilogue` did its usual clean-and-report. `persistent_write_attempted=0x00000000`
in all 25 contracts that report it and the device returned to Android. A payload branch to a NULL
pointer is a *recoverable* event here, and this is the first run in which that was demonstrated by an
unplanned fault rather than by a deliberate injection.

## The prediction was `ccdigest_init`, and it was wrong

Experiment 212's doc predicted `stub_hit=ccdigest_init`, from this reasoning: `cchmac_init` calls
`ccdigest_init` at offset 0x28 and `ccdigest_update` at 0x3c, both before anything else, and both are
absent from the image, so whichever comes first is the stop. The reasoning about the *order* was
right. What it left out is that neither call is reached.

`cchmac_init`'s first four instructions decide it, and they depend on data the image supplies:

```
   8: ldr  r0, [r0, #8]     ; r0 = di->block_size
  18: cmp  r0, r2           ; r2 = key_len
  1c: bcs  94               ; TAKEN: block_size(0) >= key_len(0)
  28: bl   ccdigest_init    ; NOT REACHED
  3c: bl   ccdigest_update  ; NOT REACHED
  94: cmp  r6, #0
  98: beq  c8               ; TAKEN: key_len is 0
  c8: mov  r6, #0
  cc: ldr  r0, [r5, #8]     ; block_size = 0
  d0: cmp  r0, r6
  d4: bls  f8               ; TAKEN
  f8: ldr  r2, [r5, #4]     ; state_size = 0
  fc: ldr  r1, [r5, #20]    ; di->initial_state = NULL  -> memcpy(dst, NULL, 0)
 10c: bl   memcpy          ; real, length 0, returns
 11c: ldr  r3, [r5, #24]    ; di->compress = NULL
 130: blx  r3              ; -> JUMP TO 0  <-- the fault
```

The two inputs are `key_len` and `di->block_size`, and both come from the same zeroed stand-in:
`hmac_dbrg_update` calls `cchmac_init(di, ctx, state->keysize, state->key)` with
`state->keysize = di->output_size` (set by `init` in the previous step) and `di->block_size` read
straight out of `ccsha1_eay_di`. Both are 0, so `block_size >= key_len` is true, and the whole
`ccdigest_*` pair is bypassed by the branch at 0x1c. The step's prediction was therefore not
"early" or "late" - it was about a path this run cannot take.

## The finding: `IFAR=0, IFSR=0x0F` is a call through a NULL function pointer

`IFSR = 0x0000000F`. In the ARMv7 short-descriptor format (`FS[10]=0`, `FS[3:0]=0xF`) that is
**permission fault, page**. `IFAR = 0`, and it is valid because bit 10 of the IFSR is clear.

The distinction matters and is the reason both registers are logged. A *translation* fault at 0
(`IFSR` 0x07) would mean nothing is mapped at VA 0; a *permission* fault at page granularity means a
page table entry exists there and does not permit execution. So the reading is not "the image jumped
off the map" but "the image executed a branch to address 0, which is mapped and not executable" -
which is what `blx r3` with `r3 == 0` produces, and `entry_stubs.c:705` says as much in advance:
"`dfar=0x00000000` says the image stored through a pointer it set to zero, and `dfar=0x0020xxxx` says
the code was jumped to rather than reached."

`ccdigest_info` (`EXTERNAL_HEADERS/corecrypto/ccdigest.h`) is
`{ output_size, state_size, block_size, oid_size, oid, initial_state, compress, final }`, so offsets
4/8/16/20/24/28 are `state_size`/`block_size`/`oid`/`initial_state`/`compress`/`final`. The faulting
instruction loads `[r5, #24]` = **`di->compress`** and branches to it. `ccsha1_eay_di` is a zeroed
storage stand-in (`B 002773c0`), so `di->compress` is NULL - and `di->initial_state` is NULL too,
which is why the `memcpy` three instructions earlier was handed a NULL source and got away with it
because its length was 0.

## The deeper finding: relocations list direct calls, and only direct calls

Every frontier prediction in this project has been made by reading an object's calls out of its
relocation table - `arm-none-eabi-objdump -dr | grep -E "R_ARM_CALL|R_ARM_JUMP24"`, or equivalently
`nm -u`. That method has now been shown to read **a subset** of the calls an object can make. An
indirect call - `blx rN`, `ldr pc, [...]` - has no relocation, because there is nothing for the
linker to resolve. It is invisible to `nm -u` and to `objdump -r` alike.

Measured across the objects this project has linked:

| object | indirect calls | did the prediction depend on seeing them? |
| --- | --- | --- |
| `osfmk_prng_random.o` | 9 | no - the stub that stopped the run was at a direct call *before* them |
| `osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o` | 0 | n/a - which is why exp-212's prediction was exact |
| `osfmk_corecrypto_cchmac_src_cchmac_init.o` | **3** | **yes - and it is the first object where it mattered** |
| `osfmk_arm_caches.o` | 5 | no - the stop was `clean_dcache`, reached before the dispatch pointer |

So the method was not *wrong* for experiments 207 through 212; it was incomplete, and incomplete in a
way that did not show until an object both (a) called through a pointer and (b) had that call
reachable before any direct call it named. `cchmac_init` is such an object, and it is the first.
Nine of `early_random`'s calls are indirect, all through `drbg_info`; they were simply never reached
while the field was a stub returning early.

**This is the nineteenth entry in [[mi4-measurement-defects]], and it is a new shape for this
project**: the first dozen were numbers produced by a tool that stopped early or accumulated, and
206 and 211 were the source's statement order standing in for control flow. This one is a tool whose
*output format* does not contain the category being looked for - the answer was never in the data,
and no amount of care in reading it would have produced it.

**How to apply:** the call graph of a compiled object is `objdump -d` and a grep for `blx`/`ldr pc`,
not `nm -u`. For prediction work specifically, the question is not "what does this object call" but
"which call does control reach first, given the data the image holds" - so both the disassembly
*and* the register values feeding its branches have to be read. `cchmac_init` is the worked example:
the disassembly alone would still have predicted `ccdigest_init`, because the branch that skips it is
three instructions above and turns on `di->block_size`.

## What the run proves, positively

Less than the last eleven, and worth stating precisely because the useful content is negative:

- `cchmac_init` is real, was entered, and ran its first branch - so the step's object is linked
  correctly and the frontier moved;
- its `block_size >= key_len` branch was taken with both zero, which is a *measurement of the data*:
  `di->block_size == 0` and `state->keysize == 0`;
- `di->compress` is NULL. That is the third field of the same stand-in to be observably zero, after
  `output_size` (exp-212's guard) and `initial_state` (the NULL source of a zero-length `memcpy`);
- the entry image's exception path works from inside XNU with the fault registers live, and reports
  `IFAR`/`IFSR` rather than merely "something faulted".

It does **not** prove anything about SHA-1, and it is the clearest possible statement of the debt
experiment 212's doc named: the DRBG is running on an all-zero digest, three of whose fields have now
been individually observed to be zero, and every step that passes is passing for that reason.

## Cost

`osfmk_corecrypto_cchmac_src_cchmac_init.o` (`osfmk/corecrypto/cchmac/src/cchmac_init.c`) - **444
bytes of text, no data, no `.bss`, 4 references, 1 definition**:

```
resolved (1):  cchmac_init
added    (2):  ccdigest_init ccdigest_update
366 -> 367 undefined
```

Both directions link, taken by standing an empty object in for the new object.

| | exp-212 | now |
| --- | --- | --- |
| entry objects linked | 52 | 53 (`cchmac_init.o`) |
| entry text | 282320 B | 282800 B |
| entry image | 387904 B | 387904 B |
| entry `.bss` | 0x0025e698–0x00277408 (101744 B) | unchanged |
| undefined | 366 | 367 |
| stubs | 305 functions, 61 storage | 306 functions, 61 storage |
| boot_args offset | +495616 | +495616 |
| headroom below `topOfKernelData` | 1608696 B | 1608696 B |
| payload text | 880078 B | 880078 B |

480 bytes of text, no image growth and no `.bss` movement - the object is pure text, so nothing above
it shifted. The payload did not change at all beyond the entry image it embeds.

## A second finding, in the payload's own contract

This run needed a careful reading of the log, and the log's own summary fields turned out to be part
of the problem. The result section of every experiment doc since 189 has cited
`stage90_xnu_entry_stub_no_exception=0x00000001`, and experiment 194's and 195's docs cited it as
evidence that the run **did not fault**. It is not a measurement of anything:

```c
	r->no_exception_observed = 1u;
	r->satisfied_mask |= STAGE90_XNU_ENTRY_STUB_SAT_NO_EXCEPTION;
```

`xnu_entry_stub.c:388` assigns the constant unconditionally, and the contract is emitted *before* the
handoff, so nothing that happens inside XNU can reach it. `FAIL_EXCEPTION` (`0x00000400`) is defined
and can never be set. It has been a constant since stage 85, and this run is the one that shows it:
the log says `no_exception=0x00000001` while the same run took a prefetch abort inside XNU. The
**real** signal was never missing - the entry image prints `exception: prefetch abort` and both fault
registers - so the field is redundant as well as misleading. It is renamed and documented in the
commit that follows this one; the run's own log naturally still shows the old name.

That is the twentieth entry, and it belongs to a class this project has not logged before: **a field
that was never a measurement, presented in the same format as the measurements around it**. The
distinguishing test is cheap and worth applying to the rest of the contract: for each reported value,
find the assignment and ask whether a device run could have produced a different one.

## What is next: give `ccsha1_eay_di` a value

The frontier rule does not name this step, because this run produced no stub hit to name anything.
The step it selects itself is the one experiment 212's doc flagged as "should be taken deliberately
rather than reached": link `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` - **4932 bytes, defining
`ccsha1_eay_di` and `sha1_compress`, referencing `ccdigest_final_64be` and
`ccsha1_initial_state`** - which turns the zeroed stand-in into an initialized `ccdigest_info` and
ends the stretch of runs that pass on empty parameters.

What it will cost, read the corrected way (disassembly, not relocations): `ccdigest_final_64be` is a
function stub and `ccsha1_initial_state` a storage stub, both new. `di->compress` becomes
`sha1_compress`, real and in the same object, so `cchmac_init`'s `blx` at 0x130 will land in real
code for the first time and the run will continue past it into `cchmac_update` - which is still a
stub, and is therefore the prediction for that step. It is written here with the caveat this
experiment earned: **`sha1_compress` must be disassembled for indirect calls and branches before that
prediction is called settled**, because `cchmac_init` is what happens when that is not done.

## Reproduce

```bash
# the step: 1 resolved, 2 added, 366 -> 367
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CCHMAC_INIT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 1 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 added    (unique to B)

./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|OK:"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -c stub_hit /tmp/cancro-last_kmsg.txt          # 0 - there is no stub hit in this run
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14
#   ... exception: prefetch abort ... ifar=0x00000000 ifsr=0x0000000f

# the branch that skipped both digest stubs, and the indirect call that faulted
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf \
  | sed -n '/<cchmac_init>:/,/^$/p' | sed -n '1,14p'
arm-none-eabi-objdump -d out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o \
  | grep -nE "\bblx\s+r|\bldr\s+pc"
sed -n '/^struct ccdigest_info {/,/^};/p' \
  external/xnu-4570.1.46/EXTERNAL_HEADERS/corecrypto/ccdigest.h

# the method defect: indirect calls have no relocation, so nm -u cannot see them
for o in osfmk_prng_random osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac \
         osfmk_corecrypto_cchmac_src_cchmac_init osfmk_arm_caches; do
  printf '%-50s %s direct, %s indirect\n' "$o.o" \
    "$(arm-none-eabi-nm -u out/xnu_kernel_obj/$o.o | wc -l)" \
    "$(arm-none-eabi-objdump -d out/xnu_kernel_obj/$o.o | grep -cE '\bblx\s+r|\bldr\s+pc')"
done

# the contract field that was never a measurement
grep -n -B2 -A2 "no_exception_observed" stages/stage90/xnu_entry_stub.c
grep -n "no_exception" /tmp/cancro-last_kmsg.txt
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
