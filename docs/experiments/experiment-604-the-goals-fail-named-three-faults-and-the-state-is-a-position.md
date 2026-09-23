# 604: the goal's FAIL named three faults, and the state it will actually meet is a fourth

596 added the goal block — the one reading the user's own condition asks for (`user mode reached, and
a driver answering`) — and it sits outside the arm branch, so it runs on every log that carries
payload output. Its `FAIL` branch named **three** causes: the driver's open answered non-zero, the
control answered 0 too, or the read's word did not change.

All three are **wrong values**. But the fixture makes its calls in a fixed order —
`open(driver)`, `open(control)`, `read`, `getpid`, `exit`, `wait` — so a log whose records simply
**stop** is a boot that stopped there. That is a **position**, and on an arm whose entire purpose is
to find where the boot stops, it is the reading the run exists to produce.

Host-side only: one narration branch in `stages/stage90/run_and_capture.sh`, one fixture axis and two
states in `tools/rehearse_live_path.sh`, reads of two archived captures. No build, no device, no
`fastboot`, no `adb`, nothing written to storage, no boot. **TWRP stays withheld** —
「如果os已经能进去了的话」 is unmet.

## 1. How it was found: by building the state, not by reading the branch

A log built to end right after the driver's open (`xnu_live_open_seq=0x00000001`,
`_error=0x00000000`, nothing further) produced:

```
    open    1 call(s), error in call order: 0x00000000
    read    0 call(s), ret_lo=absent of nbytes=absent;
    ...
  FAIL  and the log has user-mode syscalls but not 504's reading of them, so one of the
        three links is broken: the driver's open did not answer 0, the control answered 0
        as well ..., or the read did not leave
        0xfeedface in the buffer. ...
```

**Every one of the three named faults is contradicted by the block's own table**, one screen up:

| the sentence says | the table says |
| --- | --- |
| "the driver's open did not answer 0" | `open 1 call(s), error in call order: 0x00000000` |
| "the control answered 0 as well" | there is **no** control open in the log at all |
| "the read did not leave 0xfeedface in the buffer" | there is **no read** in the log at all |

A `FAIL` that names a fault its own table refutes is worse than no `FAIL`: the operator's next action
is decided by the sentence, not by the table. And the state is not exotic — it is what a fresh arm
produces if it dies inside the userland phase, which is one of the two places this arm is looking.

**The most realistic shape is worse than the synthetic one.** A boot that dies between the two opens
is `open 1 call(s)` with the read, `getpid`, `exit` and `wait` all absent, because the fixture runs
them in order — so the block reports a driver fault while the numbers above say the driver answered
**0**, which is the one thing 504's reading requires.

## 2. The fix: presence first, values second

The branch now asks the sequence question first and the values question only if the sequence is
complete:

```sh
    local g_stop=""
    if   [[ -z $g_open1 ]]; then g_stop="the driver's open"
    elif [[ -z $g_open2 ]]; then g_stop="the control open"
    elif [[ -z $g_read_ret || -z $g_read_nb || -z $g_read_after ]]; then g_stop="the read"
    elif [[ -z $g_getpid_val ]]; then g_stop="getpid"
    elif [[ -z $g_exit_pid ]];   then g_stop="the child's exit"
    elif [[ -z $g_wait_status ]]; then g_stop="the wait"
    fi
```

`g_stop` names the first call in the fixture's own order whose record is not in the log, and it is
built from `-z` tests and counts only — **no arithmetic on a value that may not be a number**. That
last part is deliberate: the neighbouring `g_pair`/`g_driver` computations use `16#${g_open1#0x}`, and
a base-conversion on a non-numeric word prints to stderr under `set -e` — the class 601 spent a step
on. The new ladder cannot produce that noise, so the "presence" side of the split is safe by
construction rather than by the log being well-formed.

With a stop named, the `FAIL` says so and says what it means:

```
  FAIL  and the fixture's sequence has no record of the control open: this log has
        1 open(s), 0 read(s), 0 getpid, 0 exit,
        0 wait record(s), and it makes those calls in that order - so **what is
        missing here is a POSITION and not a driver fault**: the boot stopped before that
        call - on this arm that is the reading the run exists to produce. ...
```

With nothing missing, the three original faults are named **with the values that decided them** —
`the driver's open answered 0x00000005 (must be 0)` — so the sentence and the table can be read
against each other instead of one being trusted over the other.

**And the two branches are not symmetric in what they claim.** The position branch says do *not* read
it as the driver failing; the values branch says the sequence is complete and the fault is in the
values. Before this step both states got one sentence, and it was the values one.

## 3. Verified by comparison, and the three states it moves are the three it is about

The oracle is the pre-step runner (`98c73371…`, this step's own parent state), staged at the **same
depth** — `stages/stage90/zz-head-cmp.sh` — because the script does `cd "$(dirname "$0")"` and
resolves `REPO_ROOT` from there; a copy one directory up is the trap 598 §5, 600 §7 and 601 §4 all
paid for. The copy was deleted before the gate ran.

| state | result |
| --- | --- |
| `533-A` / `533-D` / `533-J`, 513 run 1 / run 2, `516-run2` / `516-run3`, `cancro-last_kmsg.txt.prev` (520), `t566-real520.txt` (520), the sleeper `predicted` state, the falsifier state | **byte-identical**, both exit 0, 0 bytes of stderr — **11 of 14** |
| the log that stops after the driver's open | differs, **in the `FAIL` branch only** |
| the log that dies between the two opens | differs, **in the `FAIL` branch only** |
| every call present, the driver's open answering `0x00000005` | differs, **in the `FAIL` branch only** |

`diff` on the whole output of each changed state shows exactly one hunk, on the six lines of the old
`FAIL`. All three still exit 0 with 0 bytes of stderr, and the `UNREAD` branch (a log with payload
output and no fixture records) is unchanged.

## 4. The rehearsal gained two states, and they are the two sides of the split

`mk_sleeper_log` gained a fixture axis: `goal-truncated` stops the fixture after its first call, and
`goal-bad-values` has every call present with the driver's open answering `0x00000005`. Each is a
`reader_state` row with **two** expectations, so a row exercises the branch *and* the sentence:

| variant | expectations |
| --- | --- |
| `goal-truncated` | `no record of the control open` **and** `missing here is a POSITION and not a driver fault` |
| `goal-bad-values` | `the fault is in the values` **and** `the driver's open answered 0x00000005 (must be 0)` |

**Shown to fire**: one word doctored in the position branch (`driver fault` → `driver error`) made
exactly that one state fail, named it, and quoted the line it did not say, with **no `exit N,
promised N` part** — so the text check caught it and not the code check. The runner was restored from
a copy taken before the doctoring and verified by **sha256**, not by `git checkout`, because
`git checkout` would have reverted this step's own edit along with it.

Full pass on the restored tree: **7 ok / 0 failed** (section A, the live path's states) and
**9 ok / 0 failed** (section B, the reading), exit 0. Gate `preflight_boot_check.sh
--allow-xnu-entry` → **EXIT=0 / 518 lines / 0 stderr**, its exit census still **3 sites, codes `2 3`**
— so the branch adds no `exit` and no `die`, measured by the census and not by reading the diff.

## 5. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or gate verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no host
  action substitutes for a power press.
* **It does not change what the goal block can *see*.** Every value it prints was already read; what
  changed is which sentence a `FAIL` gets.
* **It does not sweep the rest of the file for this shape.** 603 §7 named the sweep and did not take
  it — every other `FAIL`/`UNREAD` whose named cause may not be the only one. This step found its
  instance by *building a state* rather than by enumerating branches, which is the method that worked
  twice now (601, 603) and is still not what the sweep would be. The sweep is owed and still not
  taken.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Two
host-side files edited, neither in `xnu_arm_entry-sources.txt` (the gate's freshness scan
deliberately does not match `*.sh`): `stages/stage90/run_and_capture.sh` (the `FAIL` branch inside the
goal block, no `exit`, no `die`) and `tools/rehearse_live_path.sh`. Reads of 533's and 520's archived
captures and of 513's two. One same-depth copy of the pre-step runner for the oracle, deleted before
the gate ran; one single-word doctoring of the runner, reverted from a copy and verified by sha256.
The payload, the parked frozen pair at `/tmp/r594/frozen-payload/` and the arm the next press sends
are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this can write to
storage.
