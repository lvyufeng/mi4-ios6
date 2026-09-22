# 533: the idle window's near end is left exactly as Apple left it

**The arm is 526's image with one thing taken out: the enter-side `SCTLR.C` re-enable.** Nothing else
changes - 526's null exit wrapper, 517's flush still out of the image, 516's `CleanPoC_Dcache`, 519's
`rtcPop` reading and the enter wrapper's own `entry_window_note` all stay - so this is the phase's first
arm whose *treatment* is the window's cache state and whose apparatus is 526's, already shown not to be
the cost.

It exists because 526's run came back negative and that is what 526 was for. The four configurations this
phase has measured now stand like this, and the missing cell is this arm:

| | exit-side `FlushPoC_Dcache` | enter-side `SCTLR.C` re-enable | exit wrapper's capture | returned? |
| --- | --- | --- | --- | --- |
| 518 / 519 / 520 | off | off | 520's shape (the publisher read `[sp-16, sp)` itself) | **yes**, dying at the `pop` |
| 521 | **on** | off | 521's repair (the caller loads the four words, twice) | no (power press) |
| 522 | off | **on** | 521's repair | no (power press) |
| 526 | off | **on** | **none** (a counter, `entry_slot_null_note`) | no (power press) |
| **533** | off | **off** | none (a counter) | *this arm* |

522's row and 526's differ by exactly the capture's memory traffic, and 526 says that traffic is innocent -
so the capture is out, and what is left is the one deliberate change that 521, 522 and 526 share and 520's
returning cell does not: **the write that switches the D-cache back on at the window's near end.**

**The run has not happened. The device is off the bus** - the last `usb 3-10` event is 526's own
`18d1:d00d` device 117 disconnecting at 16:35:37 on 2026-09-22, `sudo fastboot devices` and
`sudo adb devices` are both empty - **and it needs a power press before it can.** The gate is green and
the image is frozen.

## 1. The switch, and why its default is 0

`STAGE90_XNU_IDLE_CACHE_ENABLE` (`entry_trace.c:422-428`, `build_entry.sh:349-353`) guards the call in
`__wrap_platform_cache_idle_enter` (`entry_trace.c:1849-1855`): at 1 the block is 522's exactly, at 0 the
call is not compiled and the wrapper is the two `SCTLR` reads and the note.

**Its default is 0 - the cell that came back.** This phase has paid twice for the other choice: 520's gate
printed 519's image hash out of a comment literal, and 522's first build silently carried 521's flush
because the clause that was supposed to catch it asserted the flag rather than the image. A build that
forgets this variable must therefore be the *baseline* arm, not the arm under test, and the flag's two
positions are asserted against the image rather than against each other (`build_entry.sh:29110`: the
wrapper's compiled call count must equal the flag; `:29147`: the flag-off arm's own adjacency clause).

## 2. The switch's "on" is the old behaviour, byte for byte

The property that makes this arm readable is that setting the flag to 1 does not approximate 526's image,
it *is* it. Two builds of one tree, with the sources hash-checked before and after:

| build | entry image | sha256 | build |
| --- | --- | --- | --- |
| `STAGE90_XNU_IDLE_CACHE_ENABLE=1` | `out/stage90/xnu_arm_entry.bin` | `05596cc1daefd99f66f7a040f32c133b22740ace41b680a7008fc50766aad672` | 0 `FAIL`s |
| `STAGE90_XNU_IDLE_CACHE_ENABLE=0` | `out/stage90/xnu_arm_entry.bin` | `f202f2465886aba6daa357e110c6bf50e8208c6673a5c4119a8d069065eab28e` | 0 `FAIL`s |

`05596cc1…` is 526's frozen entry image to the byte (`out/stage90/frozen/526-xnu_arm_entry.bin`), so the
flag's on-position is not "close to 526" but *identical* to it, and the two arms differ by the enable and
by nothing else. The full invocation, for the record, is 526's with one variable more:

```
STAGE90_ENTRY_TRACE=1 STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_XNU_SLOT_NULL=1 \
STAGE90_XNU_EXIT_POC_FLUSH=0 STAGE90_XNU_ISTACK_SEPARATE=0 STAGE90_XNU_IDLE_CACHE_ENABLE=0 ./build_entry.sh
```

The flag's off-position is a different image by 528,901 bytes, and the two are the **same size**
(5,519,996). That is not a surprise and it is worth naming, because it is the project's own arithmetic from
the other direction (`mi4-linker-fill-term`): the call that goes away is four bytes and every address after
it in `.text` moves by four, so the relocated immediates change - hundreds of thousands of bytes - while the
image's length is pinned by `__bss_start` and the fill absorbs the four. Two arms of equal size and
different bytes is what an instruction removed from the middle of a linked image looks like.

## 3. Two defects, both in this step's own checks, both caught by the build

Neither could have reached hardware - both stop the build - and both are the class this project keeps
meeting: a check whose *shape* was copied from the previous arm and not re-read against this one. The 526
doc records two of the same kind; these are this arm's.

1. **An extractor field that was only right because of the call this arm removes.** The `awk` that reads
   the enter wrapper's `SCTLR` accesses (`build_entry.sh:29100-29111`) took the *first* read with
   `if (en == 0) b = a` - which is "the first read" only because the enable call later in the body makes
   `en` nonzero, i.e. because of the very instruction 533 takes out. With the flag off, `en` never becomes
   nonzero and `b` is overwritten by every read, so `cwebefore` came out as `0x8047c940` - 516's read,
   28 bytes past the window's opening - and the arm was refused with a message naming the wrong read. The
   fix is `if (en == 0) { if (b == 0) b = a }`, and the flag-off clause now asserts the two reads are
   **adjacent** (`cwepm == cwebefore + 4`), so an extractor that drifts again fails the build instead of
   quietly publishing a plausible number. The general form: **an extractor whose field is meaningful only
   because of a call is an extractor that this step's change invalidates**, and the direction it fails in
   is the one that costs a build and not a run.
2. **The clauses about the write were unconditional.** The clause that asserts the re-enable comes after
   the call that opens the window, the pair that brackets the write and the note, and the one that orders
   the note against 516's reading were all 522's - and with the write gone they evaluated `0 > realaddr`,
   `0 < 0`, `0 > 0` and refused a correct image. They are now the *arm's* question
   (`build_entry.sh:29112-29147`): the flag-on arm keeps 522's four assertions and its adjacency test
   (`cwebefore == realaddr + 4`, `cweaddr == realaddr + 8`), and the flag-off arm asserts what is
   actually in the window instead - the call that opens it at `realaddr`, the first `SCTLR` read as the
   instruction after it, the second as the instruction after *that*, `entry_window_note` after the second
   and 516's cache-off reading after the note. That is the whole of the image's presence inside Apple's
   cache-off window for this arm, and it is read out of the addresses rather than counted.
   The 526 say line's sentence about the enter wrapper was the same defect in prose - it said "the enter
   wrapper is untouched, so 522's clause above still asserts the SCTLR.C enable *n* time" whatever the flag
   was - and is now selected by the flag (`build_entry.sh:29197`, `:29199`), because a say line that
   describes an image that does not exist is exactly 520's gate defect.
3. **The first frozen artifact was the wrong *kind* of image, and neither the build nor the gate said so.**
   The first payload of this step was built with a plain `./build.sh`, and the canonical run build is
   `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh` - without it `STAGE90_XNU_ENTRY` is `0u`, the
   `bl stage90_xnu_entry_run` at `stage90_main.c` is compiled out, and the image runs the payload's own
   ladder and reboots. It never jumps into XNU, so **it cannot produce the log this arm exists for**, and a
   ladder log read as 533's result would have been the wrong run's. Nothing in the build noticed, and the
   gate printed `off: the payload runs its own ladder and reboots` and exited 0 - the same defect class as
   1 and 2, one level up: prose that describes the artifact instead of a check that refuses it. The
   artifact was caught by the peer session's read-only audit (`stage90-build-config.txt` line 7,
   `STAGE90_XNU_ENTRY 0u`; no `bl` to `stage90_xnu_entry_run` anywhere in the payload), and the gate now
   refuses the combination - `--allow-xnu-entry` with the switch off is a `REFUSING:` line (the flag is
   what the run is *for*, so the mismatch is the one thing that must stop it).
   The image was rebuilt with the switch on, and the 48 bytes it adds are a good test of the gate's own
   clause: `kernel_size` 6,015,308 → 6,015,356 and therefore the blob's image offset 496,100 → **496,148**,
   which the gate recomputes from the payload's symbol table and cross-checks against a content search -
   exactly the property the "no offset written down" requirement was for. The frozen
   `533-*` files are the rebuilt pair, and the first pair (the ladder image, `ad458ecd…`) is *not* kept:
   an artifact whose identity is "the wrong kind of image" is a hazard in a directory of references.

## 4. The image, and a gate that could not see which arm it carried

The step's product is `out/stage90/stage90-qcdt.img`, 8,540,160 bytes,
`1daaf44e624563694e9f5306276dd4b09e743f8a80ada5882aa2c52e8d12fef3`; the entry image it embeds is
`f202f246…`. Both are frozen as `out/stage90/frozen/533-stage90-qcdt.img` and
`out/stage90/frozen/533-xnu_arm_entry.bin`, and the gate is green (`preflight_boot_check.sh
--allow-xnu-entry`, exit 0).

Frozen is not the same as *identified*, and the difference is the second finding of this step, which comes
from the peer session's read-only audit and is landed in this commit. `build.sh` generates
`out/stage90/xnu_arm_entry_blob.c` from `out/stage90/xnu_arm_entry.bin` and the payload's `.rodata` holds
those bytes, so the image *is* a function of the entry image - but nothing checked it:

- `preflight_boot_check.sh` never mentioned `xnu_arm_boot/`, and its freshness scan was
  `find "$STAGE_DIR" -maxdepth 1`, i.e. one directory too shallow. Editing `entry_trace.c` and rebuilding
  only the entry image therefore left every check green.
- Nothing tied the image to the entry bin in either direction, and `stage90-build-config.txt` carries the
  **payload's** switches only - so the gate's printed switch list cannot say which arm the image holds, and
  a forgotten payload rebuild would leave the image and `SHA256SUMS.txt` in perfect agreement while the
  gate described one arm and the image carried another. This instance was benign for exactly one reason:
  the prose still described 526 and 526 was what the image carried. That is 520's defect one level down.

The fix is two clauses, `preflight_boot_check.sh:103-125` (freshness now covers
`xnu_arm_boot/*.{c,h,S,ld,sh}`, with `*.sh` added only in that subdirectory because `build_entry.sh` is as
much a source of the image as any `.c` in it) and `:127-245`, which asserts **the image carries the arm
`out/stage90/xnu_arm_entry.bin` holds, byte for byte**, out of four readings that no two share a source:
the payload ELF's own symbol table (`stage90_xnu_entry_blob` at va `0x000809e4`, size 5,519,996, and
`stage90_xnu_entry_blob_size` at va `0x000809e0`, asserted to be 4 bytes); the image header's `page_size`
2048, `kernel_size` 6,015,356 (the XNU-entry build's; the ladder build's was 6,015,308) and `kernel_addr`
0x00008000, which first prove the image's kernel section
*is* `stage90.bin` byte for byte; a byte-content search for the entry image inside the payload and inside
that kernel section, requiring exactly one match; and the payload's compiled-in size word, read out of the
image at `stage90_xnu_entry_blob_size`'s address. The offset is computed at gate time from the payload's
own symbol table - `blob_va - kernel_addr` = 494,100, i.e. image offset 496,148 - and cross-checked
against the content search, so the two cannot drift.

**One measured number belongs in this doc because the natural guess is wrong.** "The blob is at
`2048 + size(stage90.bin)`" is not where it is: that is 6,017,356, past the end of the kernel region,
because the entry image is not appended after the payload - it is *inside* it, at payload offset 494,100
(494,052 in the ladder build, which is what the paragraph's first number came from),
which is why the payload is 6,015,356 bytes and not 494 KB. 494,100 is an **offset** and not a size; the
blob's 5,519,996 bytes end at 6,014,048 and 1,260 bytes follow them. The clause was run in both
directions before being accepted: against the tree as it stood (image carrying `05596cc1…`, entry bin
holding `f202f246…`) it fails, and it fails on the bytes and not on the lengths - both entry images are
5,519,996 bytes, so a size check alone would have called that image correct - and against the image built
above it passes with `the linker's symbol says the blob is 5519996 bytes at 494100, which agrees`. A
third direction was taken too: a shrunken entry bin (a 5,000,000-byte prefix of the arm the image carries)
is refused, which a prefix match could not see.

## 5. What the run is to be read for, written before it happens

1. **Does the device come back at all.** This is the experiment rather than a criterion of it, and if it
   does not, the host's own USB log is the reading: a return is `18d1:d00d` -> `2717:0368` -> `18d1:4ee7`
   inside ~20 s on `usb 3-10` with serial `4a2fe00b`, and a hang is one dead second in fastboot with
   nothing after it - which is what 521, 522 and 526 each produced, and the fourth time in a row that the
   hardware watchdog (armed, 25 s) has not recovered a run.
2. **If it returns, where it dies.** 520's `sleh_storm 9` at the same `pc` is the idle exit's
   `pop {fp, pc}`, and anything later is progress.
3. **The two readings that say this image is the arm it claims to be**: `xnu_live_slot_cwe_win` and
   `_set` must **agree**, both with `C` clear. The note is kept on purpose even though the write it
   brackets is gone, because an absent key and a key that says "no change" are different facts
   (`mi4-silence-is-a-reading-only-if-success-is-silent`) - and a `_set` with `C` set would be an image
   still carrying 522's write.
4. **The ending shape**, and whether Apple's own panic path runs (520's log carries it), because a
   returning arm with the instrument intact is what 523's console item and the storage arm both need
   before they can be developed at all.

The two outcomes are not symmetric, and the asymmetry is worth stating before the run rather than after
it. **A return indicts the enable cleanly**, because 526 and 533 differ by nothing else. **A non-return
does not put the cost on one thing**, because 533 is not 520's cell: 520's image has neither the
enter wrapper's `entry_window_note` (a store *inside* Apple's cache-off window, 522's addition) nor 526's
null instrument, and the returning configuration of the phase is 520's row, not this one. A non-return
would say the enable is not the cost either - which would leave that note's store and the null wrapper's
own shape as the next bisection, in that order.

## 6. What this does not decide

- **The storage arm.** 530-532 stand where they were: the root device is already supplied through
  `__wrap_mdevlookup`, the HFS+ row is a re-derivation rather than a copy, and the controller's two
  windows are one 1 MB section that no table maps yet. Nothing here touches any of it.
- **Where the volume comes from.** TWRP is still withheld: the goal's precondition 「如果os已经能进去了的话」
  is unmet, since XNU reaches user mode and then dies at the idle exit's `pop {fp, pc}`.
- **Boot-image reproducibility.** The *entry* image reproduces byte for byte from the tree; the **boot
  image does not**, and the cause is unresolved (the peer session's reading, recorded as measurement
  defect 408): 526's frozen `7819cddb…` against a same-tree rebuild's `76bf4ea7…`, 48 bytes apart, 44 of
  them inside `stage90_xnu_pmap_bootstrap_contract_selftest` and
  `stage90_xnu_pmap_table_dryrun_contract_selftest` and 4 immediately before
  `stage90_embedded_macho_size` / `stage90_embedded_macho`, with the embedded Mach-O fixture byte-identical
  once the offset delta is applied first and the eleven other diff sites decoding as address encodings.
  So the payload link consumes something beyond the committed sources, and **arms are compared through the
  entry image** - which is why §2's identity is stated for `xnu_arm_entry.bin` and not for a boot image.
- **The mechanism.** A returning 533 does not name an instruction; it removes a state change and observes
  that the boot survives without it.

## 7. Safety

No device action in this step: two entry builds, one payload build, one gate, and the gate is green, all
of it on the host. The run that follows is one non-persistent `fastboot boot` through
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`, writes nothing to
storage, and cannot brick the device by construction - the worst case remains a phone that needs a power
press, which is what 521's, 522's and 526's runs each needed. **No build script was edited while this
image's checks ran**, and the two source files this arm is built from are the ones the loop hash-checked
before and after it.
