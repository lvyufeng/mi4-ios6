# Experiment 116 — a 45-minute hang, and the tooling defect that hid it

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/make_defines.sh` (fixed), `tools/build_xnu_arm_kernel.sh` (timeout)

## What happened

A background run of `build_xnu_arm_kernel.sh` was launched to re-measure `RELEASE` with
`experiment-115`'s fixes. Forty-five minutes later it had produced nothing: a **single `clang`
process** had been running for the whole time, on `bsd/netinet/ip_input.c`. Its output was buffered
behind a pipe, so there was no partial output to read either.

Two separate defects, and the second is the one worth keeping.

## Defect 1: the define generator could not handle a value containing spaces

`tools/xnu_config/make_defines.sh` read the option name as `$NF` — the last whitespace-separated
field. That works for `options KPC` and `options CONFIG_TASK_MAX=512`, and fails for the one option
whose value contains spaces:

```
options        CONFIG_NMBCLUSTERS="((1024 * 256) / MCLBYTES)"
```

`$NF` took `MCLBYTES)"`, stripped the quote, and emitted

```
-DMCLBYTES)=1
```

**That redefines `MCLBYTES` — a genuine kernel macro — to the garbage string `)=1`**, and every use
of it downstream is then wrong. It is not a missing definition; it is a *corrupted* one, which is a
worse class of error because the build looks like it has a definition.

Fixed: the field is now everything after the `options` keyword, with a trailing comment stripped and
quotes removed. Verified by checking that no emitted define fails the `-DNAME[=VALUE]` shape — the
only one that did was this one.

## Defect 2: the build script had no timeout, so a hang cost 45 minutes

`bash build_xnu_arm_kernel.sh` calls `clang` in a loop with no bound. One file that does not
terminate therefore stops the run forever, and because the whole thing was piped through `sed`, even
the *sign* of progress was hidden.

Fixed, and the fix is more than a safety net:

- **`PER_FILE_TIMEOUT` (default 60s)**, via `timeout`.
- **A timeout is its own reported outcome**, not folded into the failure count. `clang hung` and
  `XNU does not compile` are different findings, and a summary that merges them loses the more
  interesting one.
- **Every output file is truncated at the start.** `failed.txt` was only ever appended to, so a
  count read after two runs was a count of two runs — the re-measure showed 582 failing files where
  the real number was 371.

## The hang itself, and how far I got

With the *correct* define the file still times out, so this is not only the generator defect. What is
established:

- It is `bsd/netinet/ip_input.c` alone — 1 file of 569.
- It needs a **combination** of `RELEASE`'s options: each of the candidates tested singly
  (`INET6`, `IPV6SEND`, `IOKIT`, `IST_KDEBUG`, `KPC`, `KPERF`) compiles it in under a second, and
  the range 55..108 of the 108 defines is where it starts.
- It is **not the preprocessor**: `clang -E` on the same file with the same defines exits normally.
  So it is in parsing or semantic analysis.
- The earlier 45-minute process had produced no output, which is evidence of non-termination rather
  than slowness.

Not established: which pair of options triggers it, or whether it is a clang bug or a pathological
expansion in XNU's own headers. That is a bisect I did not finish, and it is written down here as
unfinished rather than implied to be resolved.

## The corrected numbers

Both configurations re-measured after the fix:

| | files tried | compile | fail | timed out |
| --- | --- | --- | --- | --- |
| `RELEASE` | 569 | **197** | 371 | 1 |
| `STAGE90_BOOT` (minimal) | 401 | **191** | 210 | 0 |

`RELEASE` goes from 172 to 197 — the `-D_CLOCK_T=1` fix from `experiment-115` plus the corrected
defines.

## What this is an instance of

The third time in this session that a *measurement* was wrong rather than the thing it measured:

1. `experiment-107` reported `arm_init.c` as "4 errors" — a count truncated by a fatal include.
2. `experiment-114`'s numbers were taken with a fraction of the configuration in place of Apple's
   104 options.
3. This one: a failure count that accumulated across runs, and a hang that looked like slowness.

Each was caught by looking at *why* a number was what it was, and each correction is recorded rather
than quietly edited — the same rule this project has applied since `experiment-96`'s probe reported
four runs of false negative against a premise that was never true.

**XNU still does not run.** No OS entered, no driver running; nothing here touches the device.
