# Experiment 272 — the entry window's memory is faithful, and the run's execution of it is not

**Step:** take the measurement 271 left open. Dump the instruction words the report path is made of,
from the epilogue, where the caches and the mmu are off, and compare them with the linked ELF.
**Prediction:** the words would match. The payload copies the image into DRAM itself, verifies it,
cleans it, and invalidates the instruction cache before the jump; a mismatch would therefore mean the
copy or the coherency around it is broken, and a match would leave the fetch as the only explanation
for 271's `0x30 + d`.
**Result:** **661 of 661 words match, byte for byte** — `entry_kv`, `entry_write_kv`,
`entry_epilogue` and `entry_stub_hit`, 2644 bytes, the whole path that writes the report. And the
same run carries the proof of its own conclusion: `entry_kv` wrote `800:<254` into `g_kv_buf`
*during* the run and wrote `800ac254` from the epilogue for the same value, with the right bytes
verified in memory in between. **The memory held the right words and the core did not execute them.**
The run also stopped where 271's object put the frontier: `stub_hit=mk_timer_init`,
`xnu_entry_stub_caller=0x800ac254`, which resolves to `ipc_bootstrap+0x190`.

## The question, and the two candidates

Experiment 271's run printed `xnu_entry_stub_caller=0x800:;?=4` for a value of `0x800ac0b4`. The
wrong characters were exactly `0x30 + d` where the code computes `0x57 + d` — the digit loop's
non-decimal branch taken through the decimal one. Three roads to the same eight characters
(`entry_kv` during the run, the eight bytes in `g_kv_buf`, and `entry_kv` from the epilogue) put the
difference on the environment and not on the arithmetic: **the same function, at the same address,
with the same value, computes it correctly from the epilogue where SCTLR.C and SCTLR.I are clear and
the mmu is off, and incorrectly during the run.** Two mechanisms fit that:

1. the words *in memory* at that address are not the words in the file — a copy or coherency
   problem between the payload writing the image and XNU executing it; or
2. the words in memory are right and the core fetched something else, which on ARMv7-A is what an
   instruction fetch from Strongly-ordered memory is — and every page of this window is mapped
   Strongly-ordered (`STAGE90_PMAP_ATTR_MODE_SO_ONLY`).

Reading the words back from the epilogue separates them: if memory matches the file, DRAM is not the
variable, because DRAM does not corrupt one address and leave its neighbours alone.

## The first probe, and three runs with no report

The obvious instrument is the digit loop's own address, taken with GCC's `&&label` inside `entry_kv`
so that it cannot go stale when the image grows. That is what the first build did, plus twelve
`entry_word_at` reads of it in the epilogue.

It ran, and then produced **no report at all** — three times in a row, byte-identical logs of 294042
bytes, each ending at the payload's own `jumping to XNU's _start` line and never reaching
`No errors detected` with any entry-image text before it. Two `entry_kv` calls run on the stub path
*before* the epilogue writes its first line, so a run that dies before writing anything dies before
the report exists, and the only instructions that build added to a run-time path were the `adr` that
takes the label and the `str` that keeps it, both inside `entry_kv`.

The control is what makes it a measurement rather than an anecdote: the **unmodified** 271 image was
rebuilt and run on the same device, through the same payload, in the same session, and reached
`stub_hit=mk_timer_init` with the caller at `ipc_bootstrap+0x190` and zero aborts — as its own
prediction said. So the device, the payload, the preflight and the capture path were all fine, and
the difference was inside the entry image.

## What the dump measured

The second design adds nothing to `entry_kv`: the base is `&entry_kv` (a symbol the linker fixes, not
a label and not a constant a later link would invalidate), and the dump is one contiguous region from
`entry_kv` through the end of `entry_stub_hit`. The comparison is mechanical and complete:

```
words dumped: 661
mismatches:   0 of 661
```

against `out/stage90/xnu_arm_entry.elf`'s `.text` at `0x80002024`. Everything the report is written
by — the formatter, the console writer, the teardown, the stub entry — is in memory exactly as
linked, read with the MMU off and both caches off, in the run that produced the report. Reading
`.text` as data with the MMU off cannot itself be affected by an instruction-fetch problem, which is
the point: this is an independent road to "what is in DRAM".

Two runs of this build were identical to the character, including all 5288 hex digits of the dump.

## The same run's other half: right bytes, wrong execution

The build's report is fully correct, and it contains 271's anomaly *and* its cross-check:

| key | this run | what it is |
| --- | --- | --- |
| `xnu_entry_kv_words` | 661 words | memory, matches the ELF at every word |
| `xnu_entry_stub_caller_digits` | `0x00000032` | where in `g_kv_buf` the digits were written |
| `xnu_entry_stub_caller_w0` | `0x3c3a3030` | the four bytes stored during the run: `0` `0` `:` `<` |
| `xnu_entry_stub_caller_w1` | `0x0a343532` | the next four: `2` `5` `4` `\n` |
| `xnu_entry_stub_caller` | `0x800:<254` | read back from `g_kv_buf`, written during the run |
| `xnu_entry_stub_caller_a` | `0x800:<254` | the second call, same state, same answer |
| `xnu_entry_stub_caller_v` | `0x800ac254` | the value itself, read from `.bss` in the epilogue |
| `xnu_entry_stub_caller_e` | `0x800ac254` | the same digits, written by `entry_kv` from the epilogue |

The colon is `0x3a` and `<` is `0x3c`: `0x30 + 10` and `0x30 + 12`, where `a` and `c` were owed. The four
decimal digits came out right. And the words that produced them are in the dump, at
`entry_kv+0xf8`:

```
8000211c:  e3530009  cmp   r3, #9
80002120:  e2834057  add   r4, r3, #87   ; 0x57
80002124:  92834030  addls r4, r3, #48   ; 0x30
```

`cmp` sets the flags, `add` writes `0x57 + d` unconditionally, `addls` overwrites it with `0x30 + d`
when the flags say lower-or-same. `0x30 + d` for the non-decimal digits is what `addls` executing
with flags that do not come from that `cmp` produces — a skipped or mis-fetched `cmp`, or an `addls`
whose condition field was fetched as something else. Either way the bytes were right and the
execution was not.

## The caution that comes with the reading

The first version of *this* probe — same source, same symbol base, dumping `entry_kv`'s 88 words
instead of the region — had a `.text` **of exactly the same size** as this one (935428 bytes) and two
runs identical to each other, and its epilogue reads the small `.bss` globals as garbage:

| key | 88-word build | 661-word build | 271 build |
| --- | --- | --- | --- |
| `xnu_entry_why` | `0x800e3d00` | `0x800ce9f4` | `0x800ce840` |
| `xnu_entry_why_byte` | `0x00` | `0x61` | `0x61` |
| `xnu_entry_kv_in_dram` | `0x00000001` | `0x00000082` | `0x00000082` |
| `xnu_entry_stub_caller_v` | `0x000509c8` | `0x800ac254` | `0x800ac0b4` |
| `xnu_entry_abort_entries` | `0x0001b9c0` | `0x00000000` | `0x00000000` |
| `xnu_entry_abort_first_lr` | `0x6f635f5f` = `__co` | `0x00000000` | `0x00000000` |

`0x800e3d00` is the linker's `__entry_text_end` and `0x801346c8`, which the same build read for
`xnu_entry_abort_first_ttbr1`, is `__entry_image_end`: those fields hold values out of the *linker*,
not out of the run. `__co` + `nst` + `__DA` + `TA` + `__PR` + `ELIN` + `K_TE` across the
`g_first_abort_*` block is `.rodata` string bytes read at `.bss` addresses. And 271's `0x82` is the
control that shows what a *faithful* read of those fields looks like: `0x82` is `0x5e + 36`, exactly
the 36 bytes the epilogue's own `entry_kv("xnu_entry_stub_caller_e", …)` adds to `g_kv_len` before
that line is written.

So the same source and the same layout size produced a garbage report and a correct one, one constant
apart. That does not change what this experiment measured — the 661-word dump is a *read of memory*,
and it matched, in a build whose report is internally consistent and repeatable — but it does say
what the wider claim is: **the entry image's report is not a function of its source either, and a
field is only as good as its second road.** Every field this project has acted on has one: the stop
from the stub, the caller from three roads, the counts from registers, and now the whole code path
from a byte-for-byte comparison.

## What is ruled out, and what is left

Ruled out, for the words this project reads: the copy, the write-back, and DRAM. 661 words of the
path that writes the report are exactly the linked words, in the run, after the run.

What is left is the window's **instruction fetch or execution**, which is what 271's three roads
already pointed at from the other side. The candidate this project can act on is the mapping:
`STAGE90_PMAP_ATTR_MODE_SO_ONLY` maps every page of the window Strongly-ordered, and a fetch from
Strongly-ordered memory is architecturally unpredictable on ARMv7-A. What this experiment does *not*
explain, and does not claim, is why XNU's own code in the same window has executed faithfully enough
to reach the same frontier in five consecutive builds: a genuine fetch-unpredictability would not
respect that boundary. The mechanism therefore stays recorded as an open question with a
now-narrowed field, and the two candidate causes — the attributes the window is mapped with, and
whatever makes one instruction in an otherwise correct stream not take effect — are what the next
measurement on this line would have to separate.

## Safety

Three runs produced no report and three produced reports; every one of the six returned the device to
Android on its own, and none wrote to storage. `fastboot boot` only, as in every stage. The failing
builds cost their runs and nothing else.

## What is next

Two directions, in this order:

1. **The frontier.** `mk_timer_init` is the stop, and the object that defines it is the next step —
   the same one-object step the last four experiments have taken, with the prediction written before
   the device is touched.
2. **The attribute question**, which is now the largest unknown in the *reporting* path and, if it is
   what it looks like, in XNU's own path too: rebuild with the entry window mapped as anything other
   than Strongly-ordered and see whether `0x30 + d` survives the change. That is a change to the
   stage's mapping policy, not to a probe, and it is worth more than another diagnostic.

## Reproduce

```bash
# entry image with the dump (it is in the tree)
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

Then compare the dump with the linked ELF — 2644 bytes from `0x80002024`, i.e. `entry_kv` through the
end of `entry_stub_hit`:

```bash
python3 - <<'EOF'
import re, subprocess
log = open('/tmp/cancro-last_kmsg.txt', errors='replace').read()
dump = re.search(r'xnu_entry_kv_words=([0-9a-f]+)', log).group(1)
out = subprocess.run(['arm-none-eabi-objdump', '-s',
                      '--start-address=0x80002024', '--stop-address=0x80002a78',
                      'out/stage90/xnu_arm_entry.elf'], capture_output=True, text=True).stdout
bl = []
for line in out.splitlines():
    m = re.match(r'\s*([0-9a-f]{4,8})\s+((?:[0-9a-f]{8}\s+){1,4})', line)
    if m:
        bl.append(m.group(2).replace(' ', ''))
elf = ''.join(bl)
words = [elf[i:i+8][6:8] + elf[i:i+8][4:6] + elf[i:i+8][2:4] + elf[i:i+8][0:2]
         for i in range(0, min(len(elf), len(dump) // 2 // 4 * 8 + 8), 8)]
bad = [i for i in range(len(dump) // 8) if words[i] != dump[i*8:(i+1)*8]]
print('mismatches: %d of %d' % (len(bad), len(dump) // 8))
EOF
```

The first probe's design — the `&&label` version, whose three runs produced no report — is described
in this document and was not kept; the comment above `entry_probe_dump_kv_words` in `entry_stubs.c`
records why the symbol base replaced the label.
