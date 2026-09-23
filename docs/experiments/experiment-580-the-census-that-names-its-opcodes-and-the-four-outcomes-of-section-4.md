# 580: the census that names its opcodes, and the four outcomes of section 4

Two defects found host-side, before any boot, and both of them in the shape this project keeps
meeting: **a check that was right about the machine and wrong about what it said**, and **a reader
that could not tell "I read a zero" from "I could not read"** - which matters most here because the
value it produced instead was the strongest negative reading this phase has.

Nothing ran. No device action, no arm changed: the entry image is byte-identical after the build-side
edit (`151425c4…` / `3bc72605…`, the frozen 574 arm), the gate is green on it (`EXIT=0`, 0 UNREAD), and
the device has been off the bus since 535's boot. The two changes are one build clause and one reader
function; everything else below is measurement.

## 1. The measurement arm is not coprocessor-free, and no arm of this seam is

574 section 2 listed "**0 coprocessor instructions in the body** (`$3 ~ /^mcr/`)" as one of the three
properties the build asserts, and 577 section 2 restated it without the parenthetical as "no
coprocessor instruction of any kind". Both are wrong about the body, and the parenthetical in 574 is
what shows why nobody caught it: the *pattern* is `mcr`, and the body contains a **`mrc`**.

Measured with `arm-none-eabi-objdump -d`, awk-scoped to `entry_seam_flush` from the ELF's own symbol
table (live: `0x8047c9c4`, 115 instructions, `0x1CC` bytes; 535's parked arm: `0x8047c9c4`, 118
instructions, `0x1D8`):

| | live arm (574) | 535's parked arm |
| --- | --- | --- |
| `mcr` (coprocessor **write**) | **0** | 0 |
| `mrc` (coprocessor **read**) | **1** - `8047ca64: mrc 15, 0, sl, cr1, cr0, {0}` (the compiler's inline `SCTLR` read) | **1** - the same instruction at `8047ca80` |
| `bl FlushPoC_DcacheRegion` | **0** | **1** - `8047ca64: bl 8004589c <FlushPoC_DcacheRegion>` |
| `bl`/`b` into `FlushPoU_Dcache` | 2 - `8047ca2c: b`, `8047ca58: bl` | 2 - the same two addresses |

**So the counts do not separate the arms; the callee does.** A `mrc` reads a coprocessor register and
cannot change cache state - the property the arm's safety rests on - while an `mcr` writes one and can.
The clause was therefore *right to refuse `mcr`* and wrong to call what it counted "coprocessor
instruction(s)", in its own refusal text, in the arm's narration, and in two documents. The fix is not
to widen the refusal to `mrc` (that would have refused the committed arm, and a read is harmless) but
to make the check say what it tests and show the pair:

- the census is now four numbers - `mcr`, `mcr` naming `cr7`, `mrc`, `mrc` naming `cr7` - printed in the
  arm's narration as `0 mcr (coprocessor write) / 1 mrc (coprocessor read, of which 0 name cr7)`;
- the refusal stays on any `mcr`, and a second refusal now covers an `mrc` naming **`cr7`** - a body
  reading a cache register is reaching into the subsystem this arm must not touch, and it is refused by
  *target* rather than by count, so it is not a literal pinned against this image.

**And the new refusal can fire, which is the point of it being a check rather than a claim.** The
negative control is a routine in the same image: `mmu_kvtop_wpreflight` (`0x80017594`) gives
`mcr=2 mcr7=1 mrc=1 mrc7=1` under the same awk - so both refusals have an artifact that makes them
fire, and neither is a clause that has only ever been seen to pass.

The edit is a build input, so the build was re-run to prove the artifact does not move: bin
`151425c4…`, elf `3bc72605…`, unchanged, and the gate green afterwards. The build's own arm-change
guard refused the first attempt (I had left `STAGE90_XNU_ISTACK_SEPARATE` at its default of 1 while the
record says 0) - the twelve-key guard working as designed, one step before the artifact.

## 2. 578 section 1's table, read back against `nm`

The peer session's census of the same body (`579`) checked 578's three claims against the image and
found them holding: `invalidate_mmu_dcache_region` is a clean-and-invalidate, `cr7,cr6,{1}` is absent,
`SCTLR.C` comes back on twenty-four bytes before the `pop`. Three things in that section's table did
not hold as written, and two of them are the same defect as section 1 above - an address or a name
carried from one kind of thing to another:

| written in 578 §1 | measured | what it is |
| --- | --- | --- |
| `CleanPoC_DcacheRegion` "also spelled `_Force`" | the alias is **`CleanPoC_DcacheRegion_Force`**; `nm` finds **no symbol named `_Force`** in either parked image | a truncated name |
| `FlushPoC_DcacheRegion` `(0x800458b0)` | **0x800458b0 is the `mcr`'s own address** inside `cfmdr_loop`; the routine's entry is **`0x8004589c`** | one column holding instruction addresses and function entries together - the same mix in `invalidate_mmu_dcache_region` `0x80045710` (entry `0x800456fc`) and `invalidate_mmu_dcache` `0x800456f4` (entry `0x800456f0`) |
| "**Every** data-cache maintenance site in ...elf" | the table scopes to the **by-MVA `{1}`** forms; the image also holds **9** set/way `{2}` data-cache sites, **5** `cr7,cr5` I-cache sites and **3** `cr7,cr8` TLB sites | a census sentence wider than the census |

The table is now scoped and corrected, and the scope is *the argument*, not a concession: an operation
aimed at one line must name an address, so only the by-MVA forms can be aimed at the slot's line at
all. The counts, the two `cr7,cr14,{1}` sites and the absent `cr7,cr6,{1}` are as measured, so nothing
in that section's finding changed - and the one row that got sharper is the first: **four of the five
`cr7,cr10,{1}` sites are the entry's own code** (`entry_epilogue` ×2, `entry_mmio_section`,
`entry_live_map`), not Apple's `CleanPoC_DcacheRegion`.

## 3. `serial_enum_count` could not say "I could not read it", and read as zero instead

Section 4's return criterion is "`adb devices` lists the serial, **or** the host's own USB log shows a
new `SerialNumber: 4a2fe00b`", and the function that does the second half was:

```sh
n=$(sudo dmesg 2>/dev/null | grep -c "SerialNumber: $SERIAL" 2>/dev/null) || true
[[ $n =~ ^[0-9]+$ ]] && printf '%s' "$n"
```

**`grep -c` prints `0` for empty input**, and empty input is exactly what a `dmesg` that failed
produces - so the function's own guard (`[[ $n =~ ^[0-9]+$ ]]`) passed with `n=0`, and the caller's
`${ENUM_BEFORE:-UNREAD}` never fired. Measured, with a stub whose `dmesg` fails:

```
with a dmesg that FAILS:  [0]  -> call site reads [0]      # not UNREAD
same, through the stub:   [0]  -> [0]
```

The consequence is not cosmetic. With the log unreadable, `ENUM_BEFORE` and `ENUM_AFTER` are both `0`,
the wait expires with no rise, the `ENUM_AFTER -gt $ENUM_BEFORE` test fails, and the run falls through
to **`exit 2`, "The device did not come back: no adb entry, and no new enumeration in the host log"** -
the strongest negative reading in the file, printed by a reader that read nothing. On the run this
phase is waiting for, that message tells the operator to press power, and the payload's log lives in
the top of DRAM: the reading that spends the device, produced from no reading at all. This is
`[[mi4-silence-is-a-reading-only-if-success-is-silent]]` in the instrument itself, and the file already
had a branch for the state - the branch was decoration over a value the function could not return.

So the function now tests **the read** and not the count:

```sh
serial_enum_count() {
  local out n
  out=$(sudo dmesg 2>/dev/null) || return 0     # a dmesg that failed: say nothing -> UNREAD
  [[ -n $out ]] || return 0                     # an empty log is not a count of zero either
  n=$(printf '%s\n' "$out" | grep -c "SerialNumber: $SERIAL") || true
  if [[ $n =~ ^[0-9]+$ ]]; then printf '%s' "$n"; fi
  return 0
}
```

The trailing `return 0` is part of the repair and not tidiness: the old form's last statement was a
failing `[[ ... ]]` when `n` was not numeric, so under this file's `set -e` the *assignment* at
`ENUM_BEFORE=$(...)` would have aborted the run with **exit 1** - "the gate refused, or the device was
not found" - turning "the host log could not be read" into a wrong claim about the device, at the one
site where the value is taken.

## 4. The unreadable state has no verdict, so it is not exit 2

With the function repaired, the state its guard was written for is reachable for the first time, and
what it should exit is a decision rather than a detail: **exit 2's definition is a device verdict**
("the payload ran and the device did NOT come back"), and the unreadable state has none.

The first attempt made it **exit 4**, and **the gate refused the runner's own change**:

```
REFUSING: run_and_capture.sh's wait section can return 4, which is not a code this gate's
reading explains (2 3) - read section 4 and update this gate before spending a boot on a run
whose status it cannot narrate
```

That refusal is the check working (it reads the section's `exit N` sites at gate time and refuses a
code it cannot narrate), and it moved the choice onto the right ground: the state is a **host-side
failure that says nothing about the device**, which is what **exit 1** already is in this file ("the
gate refused, the device was not found", and 511/512's unwritable `$LOGFILE`). So the branch `die`s -
exit 1 - and the header's exit-1 line now names the third clause. No gate change was needed, no new
code exists, and the operator still gets the "check by hand before pressing power" instructions.

**One thing that refusal exposed and that is worth naming rather than hiding**: the gate's census
counts literal `exit N` sites, and a `die` is a call, so this path is a second producer of exit 1
*inside* section 4 that the census cannot see. The gate hedges exactly this in its own text ("a code can
have a second producer outside it"); here the second producer is inside. It is benign - exit 1 is the
host-failure code and the census is about the wait's device verdicts - but a census that reads a
section for `exit` lines and not for `die` is a coverage limit, not a proof of completeness.

Also split out, because it shared the branch and the text: a **fall** in the enumeration count (the
`dmesg` ring buffer rotating) used to print the sentence "UNREAD: the host log could not be compared"
while the count *had* been compared - two counts, both read, the second smaller. It still exits 2 (the
reading it cannot rule out) and now says what happened.

## 5. Every outcome of section 4 and step 5, exercised against this reader

Six states through a stub whose device side is identical in all of them - `adb` lists the serial before
the boot and not after it, `exec-out` always fails - so each row is decided by what the host log says
and by whether the runner can read it. `RETURN_TIMEOUT=6`, `CAPTURE_WAIT=5`, the stub's five modes
selected by `S4_MODE`:

| state (stub mode) | host log | exit | where | printed |
| --- | --- | --- | --- | --- |
| `dmesg` fails (`fail`) | `UNREAD -> UNREAD` | **1** | section 4, unreadable | the UNREAD block + `die` |
| `dmesg` prints nothing (`silent`) | `UNREAD -> UNREAD` | **1** | section 4, unreadable | the same - and the pre-boot hint "the host log is unreadable here, so only adb can speak" fires for the first time |
| count falls (`fall`) | `2 -> 1` | **2** | section 4, the fall | "the comparison fell ... the dmesg ring buffer rotating" |
| no rise at all (`none`) | `1 -> 1` | **2** | section 4, non-return | "The device did not come back: no adb entry, and no new enumeration" |
| rise **at** the after-read (`late3`) | `1 -> 2` | **3** | **section 4** | "REFUSING to call this a non-return" |
| rise **inside** the wait (`late2`) | loop breaks early | **3** | **step 5** | "REFUSING to call this a hang: the device returned to the host" |

Each state prints exactly one of those messages and no other, and the two exit-3 producers are
separated exactly as the header says: **when the enumeration was seen**, not whether adb came up. The
`late2`/`late3` pair is the discriminating one and it turned on the stub's *call count*, not a clock -
because `ENUM_AFTER` is read immediately after the wait, so "late" has to mean "on that read".

## 6. And 566's variant 3 was right; the re-run of its stubs was not

The four stub variants parked in `/tmp/e2e` from 566 (`bin_fail`, `bin_hostret`, `bin_s4`, `bin_s4b`),
re-run today against the current reader, **all four** landed on **step 5** with `exit 3` - which first
read as a regression against 566 section 3's row 3 ("section 4, exit 3", with `bounded wait expired`
and `host log: 1 -> 2`). It is not: those four runs took the **default** `RETURN_TIMEOUT=180`, and both
"late" stubs let the enumeration rise inside that wait (the marker told the loop `RETURNED=1`), while
`bin_s4`'s `adb` clause never consulted its boot marker at all, so the device stayed listed and the loop
succeeded on its first pass - the variant that was written to exercise section 4 could not reach it.
That is 566c's own subject re-derived from the other side (**the producer is a property of the
rehearsal's wait**), plus the harness's own copy of the lesson: a stand-in with the right name and the
wrong body, and a counter file that was never cleared between runs.

566's measured column is reproduced exactly by the new stub's `late3` mode. So the expectation that was
wrong was the re-run's, not the document's.

## 7. Safety, and what is still owed

No device action, no `fastboot`, no `adb`, nothing written to storage: one build-input edit and one
rebuild to prove the artifact does not move (`151425c4…` unchanged, gate `EXIT=0`, 0 UNREAD), one
reader edit, and eleven rehearsals in `/tmp` against stubs. The frozen arm is untouched and still
unrun.

**The boot still waits only on the user's power press.** When it comes, the reading is: exit code, then
the death's shape, and if a log returns, `xnu_live_seam_*` with `op=0`. TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.

Parked under `out/stage90/captures/580-rehearsal/`: the six rehearsal logs, the stub, the build log,
the gate run that refused the fourth code and the green one after it, and the two disassemblies
(`entry_seam_flush` from each arm) with the negative control.
