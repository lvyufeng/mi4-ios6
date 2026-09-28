# 818 — the goal's first clause is met, and every document's closing sentence says it is not

**A HOST-SIDE READING OF THE ARCHIVE, AND A CORRECTION TO THE RECORD'S OWN VOCABULARY. NOTHING WAS
BUILT, NOTHING WAS SENT, NO BYTE UNDER `out/` MOVED.** Every number is a line of a capture already
committed here. The arm is untouched.

---

## 1. The measurement

`out/stage90/captures/rung34-cidgate-20260928-144115-last_kmsg.txt` — **the newest press in the
archive, and the arm the park holds** — carries, in the kernel's own words:

| line | what it is |
| --- | --- |
| `Added memory device md0/rmd0 (02000000/0D000000) at 0000000080519000 for 0000000000002000` | the memory device is attached |
| **`BSD root: md0, major 2, minor 0`** | **XNU's BSD layer has mounted a root filesystem** |
| `load_init_program: attempting to load /usr/local/sbin/launchd.development` … `/sbin/launchd` | **XNU is loading init** |
| `mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x8004deb8)` | the image is in the process |
| `mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)` | **pid 1 is running in USER mode** |
| `mini4: the OS has nothing to run -- pid 1 parked in poll for 2000 ms (caller 0x8028a898), and the kernel's own idle path was entered 62283 time(s)` | the OS is scheduling and idling |

and the ladder's own cells for the same boot:

| cell | value | reading |
| --- | --- | --- |
| `xnu_live_sleh_user` | `0x00000001` ×3 (and `0x00000000` ×6) | the CPU was **observed in user mode** |
| `xnu_live_read_word_before` → `_after` | `0x00102000` → **`0xfeedface`** | a `read` syscall was serviced and returned data |
| `xnu_live_wait_pid` / `_wait_status` | `0x00000002` / **`0x00000300`** | a `wait4` returned a status the kernel wrote |
| `xnu_live_sigchld_seq` / `xnu_live_psignal_calls` | `0x00000001` / `0x00000001` | the child's exit was delivered |
| `xnu_live_idle_en` / `_idle_caller` | `0x00000001` / `0x800bb9e8` | the kernel's idle path ran |

**And it is not a rung-34 property.** The same three cells — `_wait_pid = 0x2`,
`_wait_status = 0x300`, `_read_word_after = 0xfeedface` — are in **every capture from rung 19
(2026-09-26 18:09) through rung 34**, twelve presses running.

So: **XNU boots, mounts a root filesystem, loads init, runs a user-mode process, services its
syscalls, and keeps the machine up.** That is 「起码要能进入操作系统」, and it is met on the arm in
`out/` today.

## 2. How the run ends, so that §1 is not read as "and then it crashed"

`_post_end_calls = 8`, `_seam_post_end_ticks = 0x06ddd000 = 115,200,000`, `_post_cntfrq = 0x0124f800
= 19,200,000` — **6.0000 s, exactly the ladder's own deadline**, which every run since 520 has ended
on. The `panic(cpu 0 … fault_addr=0xfa0065c)` in the same capture is the **known forced ending**:
`entry_epilogue`'s first store (`RESTART_REASON`, `r2 = 0x0fa00000`, `r3 = 0x78665501`) takes a
translation fault in the context it runs in — measured by 684, and **it is the mechanism by which any
log comes back at all** (`[[mi4-the-run-ends-at-entry-epilogue]]`). The run does not end because the
OS failed. It ends because the instrument stops it.

## 3. The correction, and it is a defect this project has a name for

**Every experiment document in this phase closes with a sentence saying the opposite of §1**, and they
have done so since rung 19. The wording varies; the shape is always *"the OS not observed reaching
userland"* / *"THE GOAL IS NOT MET"*. **The first half of the goal has been met for twelve presses and
the closing sentence has been false for twelve presses.**

This is `[[mi4-one-value-two-definitions]]` applied to the goal statement itself, and the index has
carried the instance since 745:
> the closing sentence … states the absence of the **SECOND** half of the goal criterion (the machine
> staying up) in the vocabulary of the **FIRST** (user mode), and the first half is **MET**.

It is also m755's exact shape: **a negative is where two definitions hide best, because nothing
prints.** A sentence that begins "not observed" invites the reader to supply the subject, and the
subject that gets supplied is the one the author had in mind, not the one the cells measure.

**ASK OF EVERY CLOSING SENTENCE IN THIS RECORD: which CLAUSE of the goal is this negative about?**
The honest form names the clause: *"the storage driver is not up, so there is no block device backed
by real storage and no mount of one"* is true and specific; *"the OS was not observed reaching
userland"* was neither.

## 4. What is actually unmet, clause by clause

| clause | status | the measurement |
| --- | --- | --- |
| 把 xnu 正常加载 (load XNU) | **MET** | the kernel's own boot through `BSD root: md0` |
| 起码要能进入操作系统 (at least enter the OS) | **MET** | pid 1 in user mode, syscalls serviced, idle path live — §1 |
| 把基础驱动跑起来 (get the base drivers running) | **PARTLY** | XNU's own platform/driver plane and its BSD layer come up. **The storage driver does not**: the SDHCI ladder is at CMD9 (rung 35), the card is not in TRAN, and there is no block device backed by the eMMC |
| 让 os 可以正常启动并且挂载存储 (boot normally and mount storage) | **NOT MET** | the root is `md0`, a **memory** ramdisk. Nothing on the device's storage is mounted |
| TWRP 写入到存储 (write to storage with TWRP) | **WITHHELD** — see §5 | |

## 5. The conditional clause, read honestly

The operator's sentence is *"如果 os 已经能进去了的话，可以 twrp 写入到存储里了"* — **if the OS can
already get in, then TWRP may write to storage.** §1 says the antecedent is **true**.

**And acting on it now would be the bricking action the same sentence forbids.** The purpose clause
is *"让 os 可以正常启动并且挂载存储"* — so that the OS boots normally **and mounts storage**. There is
no storage driver: the ladder has never got the card into TRAN, never read a block, never written
one. Writing the OS to the eMMC today produces a device whose kernel cannot mount what it was written
to — a persistent change, made to a device that currently boots and answers, in exchange for a state
that does not boot. **The antecedent is met; the purpose is not reachable yet, and the two clauses of
one instruction disagree about whether to act.**

**So TWRP-to-storage stays withheld, and now for a stated reason rather than by inherited habit**:
not *"the OS cannot get in"* (it can), but *"the storage it would be written to is the one thing this
ladder has not got working."*

## 6. What follows for the next steps

* **The goal is nearer than the record says, and the remaining distance is exactly one thing**: a
  working SDHCI storage driver — CMD9 (rung 35, armed), then CMD7 (select), then the data path CMD8
  and CMD16, then a filesystem, then a mount.
* **The `md0` root is what makes the OS reachable without any of that**, and it is why "enter the OS"
  and "mount storage" were always two different clauses rather than one.
* **The closing sentences of the documents are left as written.** They are the historical record, and
  this project's rule is to supersede rather than edit. This section is the correction.

## 7. The arm

**THE ARM IS UNTOUCHED: rung 35, `armed-storage-47c657af`, switch value 34, CMD9, eleven members,
ARMED AND NOT PRESSED, and the press is the operator's.** Readiness is 5 of 5 exit 0 and `make check`
is exit 0. **TWRP-to-storage stays withheld** — for §5's reason, not for the one the record has been
giving.
