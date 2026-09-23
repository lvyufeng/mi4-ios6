# 588: one empty string, three states, and the sentence that tells the operator what is on disk

`$PREV_LOG` is empty for three different reasons, and both consumers of it read every one as "the park
failed". On the state the *real* `/tmp/cancro-last_kmsg.txt` is in as this is written - **absent** - the
exit-3 message therefore told the operator that the file "is still the run before this one's" and to
compare a sha256, about a file that does not exist. That is the message with the least margin for a wrong
sentence: the run is spent, the payload's log is in DRAM, and the next power press destroys it - which is
exactly why 562 and 564 exist.

Found by running the success path end to end, not by reading it.

No device action, no build, no arm: one reader edit plus rehearsals of seven states on demand. The frozen
574 arm is byte-identical (`151425c4…`, `914f45ac…`), the gate is green on it, and the phone is off the bus.

## 1. How it was found: a rehearsal harness bug that led to a real one

The end-to-end rehearsal - `run_and_capture.sh` against a stub, on the shape the coming run will produce -
is the only thing in this project that exercises steps 1 through 5 as one path. Its first version came
back **exit 3 on a run that should have succeeded**, and the cause was in the stub, not the runner:

```sh
      *"exec-out") cat $S/capture.txt; exit 0 ;;      # matches a string ENDING with "exec-out"
      *"boot")     touch $S/booted;     exit 0 ;;      # ... and one ENDING with "boot"
```

Neither pattern has a trailing `*`, so neither could match `adb -s 4a2fe00b exec-out cat
/proc/last_kmsg` nor `fastboot boot /mnt/…/stage90-qcdt.img`; both fell through the inner `case` to a bare
`exit 0` with no output, so the capture read zero bytes nineteen times and the rehearsal scored a
successful path as a capture failure. A `case` pattern is anchored at both ends unless it says otherwise -
the same "the test did not run, and the failure looked like the artifact's" shape this project keeps
finding, one layer out in the harness. Fixed, and the rehearsal then returned **exit 0** with the log
captured and summarised (`588-success-path-end-to-end.txt`).

**But the wrong run printed a sentence that was wrong in its own right**, and that is this step:

```
/tmp/r586/out.log is untouched, so it is still the run before this one - do not read it as this one's
(step 2b could not park it, so its sha256 must still equal the one the gate printed).
```

while step 2b, forty lines earlier, had said:

```
no log at /tmp/r586/out.log yet - so after this run, the name existing at all is the first check
```

Two lines of one run, contradicting each other about whether a previous log exists. The runner defect was
reachable *only* because the stub was broken; it was worth the stub bug.

## 2. The defect: one empty value with three producers, and two consumers naming one

Step 2b leaves `PREV_LOG` empty in three states and says which out loud:

| state | step 2b's own line | is `$LOGFILE` the previous run's afterwards? |
| --- | --- | --- |
| `nofile` | `no log at … yet` | no - the name holds nothing |
| `nopark` | `WARNING: could not move …` (511/512) | **yes** - and its sha256 is the gate's |
| `parked` | `parked the previous run's log: … -> …` | no - it is at `$PREV_LOG` |

and both consumers tested `[[ -n $PREV_LOG ]]`, which is false for `nofile` and `nopark` alike. So
`nofile` got `nopark`'s sentence. This is the project's most-repeated defect class - one decision answered
in N places, N−1 of them uncompared - and it is here in its cheapest form: not a wrong number, but a
**wrong sentence about what is on disk**, in the one place where being wrong costs the run.

**Detail the classes hide: `nofile` is not a rare state.** It is the state the device is in *right now* -
`/tmp/cancro-last_kmsg.txt` absent, the previous logs parked at `.prev` and `.prev.2` - because step 2b
parks the file and step 5 only re-creates it on a successful non-empty read, and the last several runs did
not return. So the coming run reaches step 2b in `nofile`, and had its capture failed, it would have
printed the false sentence.

## 3. The repair: name the state once, and let every consumer read the state

`PARK` is set in step 2b - `dry`, `parked`, `nopark`, `nofile` - and both consumers `case` on *it*. Each
state now gets its own sentence, and the two that matter are new:

- **`nofile`**: *"does not exist and did not before this boot either (step 2b: 'no log at … yet'), so there
  is nothing here to mistake for an earlier run's - and nothing here is this run's until the hand retry
  above writes it. A file present afterwards is new."*
- **`nopark`**: *"is untouched, so it is still the run before this one's … It is also the name the hand
  retry above writes to, so a successful retry replaces it."* - the second half is new too: `nopark` is the
  one state where the hand retry's own redirect destroys the evidence it is meant to preserve.

And the default `*)` branch is **not** a state of the log. The first version of this edit made it a
dry-run sentence, which is an **unreachable branch claiming a state** - that block is inside step 5's
`else`, so a dry run cannot reach it (`582` §2 and `586` §2 are the same defect twice already). It now
says what it is: `$PARK`'s initial value means step 2b never ran, which is a defect in this file, and the
operator is told nothing about the file's provenance rather than something false.

Both blocks carry the same three real states, because the two are reached by different routes (section 4
times out with a rise in its *final* read; step 5 is reached at all only when section 4 returned) and the
same wrong sentence was in both.

## 4. Seven states rehearsed, each on the path that reaches it

| what | how reached | exit | sentence |
| --- | --- | --- | --- |
| success path | stub: returns, captures | **0** | the log summarised in the capture arm (581) |
| `nofile`, step 5 | returns, capture fails, no prior file | 3 | **the new one** |
| `parked`, step 5 | returns, capture fails, prior file present | 3 | names `$PREV_LOG` |
| `nopark`, step 5 | returns, capture fails, `mv` refused (dir mode 555) | 3 | 511/512, and the retry's own hazard |
| `unknown` | step 5's default, on a farm with `PARK=nofile` renamed away | 3 | the UNREAD line |
| `nofile`, section 4 | `late3`: the rise lands in the read after the timeout | 3 | **the new one** |
| `parked`, section 4 | same, with a file to park | 3 | names `$PREV_LOG` |

The `unknown` state is the only one that cannot be reached through the file as written, so it was reached
on a **symlink farm** whose runner has one line renamed (`PARK=nofile` -> a no-op comment) - the same
technique 586 used for the gate clause, and for the same reason: a branch is only a branch if it has been
seen to fire. Nothing in the repository was touched to run it.

The `nopark` state is worth its own note, because it is the only one that needed a real host condition
rather than a stub: `mv` inside a directory with mode 555 fails with `EACCES`, which is the effect 511/512
describe from the sticky-`/tmp` side. The rehearsal reproduces the *effect* (`mv` refused) and not the
*cause* (a root without `CAP_FOWNER` on a sticky `/tmp`), and says so here rather than implying the causal
path was tested.

## 5. Regression

- **The six section-4/step-5 states, re-run against the final reader**: `fail 1`, `silent 1`, `fall 2`,
  `none 2`, `late3 3`, `late2 3` - identical to 582's, 584's and 586's recordings
  (`588-six-states.txt`).
- **The success path end to end: exit 0**, the capture renamed into place, and `run_and_capture.sh`'s new
  "reading the log this run captured" step firing on the 574-shaped log.
- **520's real log: exit 0, `0 FAIL`, `0 UNREAD`** - unchanged.
- **The gate is green**: `EXIT=0`, 23 sections, 0 UNREAD.
- No device action, no `fastboot`, no `adb`, nothing written to storage, no build input, no arm: one reader
  edit, a stub under `/tmp`, a symlink farm under `/tmp`, and reads of logs already on disk. **The frozen
  arm is untouched and unrun.**

**The boot still waits only on the user's power press.** And the state it will reach step 2b in is
`nofile`, which is now the sentence that says so. TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
