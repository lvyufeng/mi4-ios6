# 615: the path the caller names was resolved against this script's own directory

The step after the one gated boot is to **read that run's capture**, and the reader takes the log's path as
an argument. This step found that the argument was not the caller's: `run_and_capture.sh` `cd`s to its own
directory before it parses anything, so a relative `--summarise` path was joined to `stages/stage90/` — and
the tool then refused with `no such log`, naming a file that was sitting right there.

The device state is unchanged and nothing about the arm moved. Host-side only: one script's path handling,
one rehearsal's cells, one document, one index row. No boot, no build, no device, no `fastboot`, no `adb`,
**nothing written to storage**; both parks were hashed and never modified. **TWRP stays withheld.**

## 1. The defect, measured three ways

`run_and_capture.sh:58` is `cd "$(dirname "$0")"`, which the file needs — every internal relative path
(entry sources, config records, the gate) is stable only from there. The `cd` runs **before** the argument
loop, so it re-based the arguments too. Before the fix:

```
$ cd /mnt/data/mi4-ios6
$ bash stages/stage90/run_and_capture.sh --summarise out/stage90/captures/533-2026-09-23-last_kmsg.txt

== summarising out/stage90/captures/533-2026-09-23-last_kmsg.txt ==
run_and_capture: no such log: out/stage90/captures/533-2026-09-23-last_kmsg.txt
$ echo $?
1
```

The same invocation from the capture's own directory, with the bare filename, refuses identically. Only an
absolute path works. The file is **597,641 bytes**, `-rw-rw-r--`, and `head` reads it.

That is a refusal that **names the artifact for a fact about the harness** — the shape
`mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one` records — and it lands on the one reader
whose entire product is a reading *of* a log. `no such log:` is indistinguishable from *"the capture step
failed"*, and after a single gated boot that is the difference between rebuilding and re-reading.

## 2. The fix, and the second defect inside it

Input paths are now resolved against the directory the script was **invoked from**, captured before the
`cd`:

```sh
INVOKE_PWD=$PWD
cd "$(dirname "$0")"
...
RESOLVED=""; REBASED=""
resolve_path() {   # resolve_path PATH - sets RESOLVED, appends to REBASED
  local p=$1; RESOLVED=""
  [[ -z $p ]] && return 0
  if [[ $p == /* ]]; then RESOLVED=$p; return 0; fi
  REBASED="$REBASED $p"; RESOLVED="$INVOKE_PWD/$p"
}
```

applied to both path arguments (`--summarise` and a `LOGFILE` from the environment), and the refusal now
prints what it actually looked at:

```
run_and_capture: no such log: /mnt/data/mi4-ios6/out/stage90/captures/nope.txt (looked for that path,
   resolved against /mnt/data/mi4-ios6; '/mnt/data/mi4-ios6/out/stage90/captures/nope.txt' is not a
   readable regular file)
```

**The first version of `resolve_path` `printf`ed its result and was called as `LOGFILE=$(resolve_path …)` —
and that silently lost the `REBASED` record**, because a command substitution runs in a subshell and the
append died with it. The resolution itself worked and the note printed nothing. Found by running the fix
and noticing the absence of a line that should have been there, which is the project's own rule about
silence turned on its author: **a check that succeeds by printing nothing cannot be told from one that
never ran**, and here the *thing* printing nothing was the instrument rather than a check. The result now
comes back in a variable.

A relative path is also **narrated**, one line per re-based argument, so the interpretation is visible at
the moment it is made rather than discovered later:

```
note: 'out/stage90/captures/533-2026-09-23-last_kmsg.txt' is a relative path, resolved against the
      directory this was invoked from (/mnt/data/mi4-ios6), not against /mnt/data/mi4-ios6/stages/stage90.
      Pass an absolute path to say it exactly.
```

## 3. The cells, and which two of them are worth anything

`tools/rehearse_live_path.sh` gained a fourth section, **4 ok / 0 failed**, beside the existing live-path
(7) and reading (13) sections — 24 in total, exit 0.

| cell | what it asserts |
| --- | --- |
| `relative-path-reads` | a path relative to the caller's directory reads the log (it used to be refused) |
| `relative-from-root` | the same, invoked from the repository root — the path is **derived** with `os.path.relpath`, not written down, so the cell does not depend on which captures are on this host |
| `relative-absent-refused` | an absent relative path is **still** refused (no-regression) |
| `absolute-path-no-note` | an absolute path prints **no** note |

**The falsification is what makes the last two honest.** Removing the `resolve_path` call for the argument
(and nothing else) turns cells 1 and 2 red with the original symptom — `no such log: capture.txt` — and
leaves cells 3 and 4 **green**, because a missing file is refused either way and the broken version printed
no note either. So the two green cells are exactly the two a broken version also satisfies; both are kept
and both say so in their own comments. `absolute-path-no-note` is an assertion of **absence**, and its
evidence is only readable together with cells 1 and 2 — the pair is what turns "no note" into "absolute
paths stay silent" instead of "that line never runs".

This is the same discipline the shrink cells in `tools/rehearse_revert_set.sh` needed one step earlier, and
it is now the second place in the tree where an absence assertion carries its evidence in a sibling.

## 4. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the arm is still the sleeper, and the run is still
  the user's.
* **It changes no reading.** Every other `--summarise` invocation in the tree passed an absolute path
  (`/tmp/cancro-last_kmsg.txt` in the watcher prompt, `$WORK/...` in the rehearsal), so no archived reading
  was wrong. What changed is that a path the operator would naturally type now works.
* **It does not make the reader more capable.** The reader's verdicts, its rung ladder and its UNREAD
  branches are untouched; this is the argument, not the analysis.
* **It does not sweep `LOGFILE`'s other uses.** The variable is re-based at the single point where it is
  read from the environment, but the script also derives `.prev` names from it; those inherit the resolved
  value, which is what makes them land beside the log instead of in `stages/stage90/`.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10:` disconnect with serial `4a2fe00b` at **2026-09-23 01:18:06**, now **8.0 h**; `adb devices`
  and `fastboot devices` both empty; the only phone-class device on the host is the neighbouring
  `05c6:9008` QDL on bus 003. The one event that can move the goal is still the user's power press, and
  what this step adds is that the reading taken after it is not blocked by the way the log is named.

## 5. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One script
edited (`stages/stage90/run_and_capture.sh` — `INVOKE_PWD`, `resolve_path`, the two resolutions, the note
loop, and the refusal's message), one rehearsal extended (`tools/rehearse_live_path.sh` — a fourth section
of four cells and a `path_state` helper that asserts exit code, a required phrase and a forbidden phrase).
The gate was re-run → **EXIT=0 / 549 lines / 0 stderr**, unchanged. `rehearse_live_path.sh` → **7 + 13 + 4
ok / 0 failed**, exit 0. `rehearse_revert_set.sh` → **38 ok / 0 failed**. Both parks verify **exit 0**
against their recorded sets, hashed in place with no file modified. The payload, both parks and the arm the
next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this can
write to storage.
