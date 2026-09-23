# 608: the tool image has a record now, and the gate reads it

605 built the gate for the first persistent write and named its own largest gap in as many words:

> **No TWRP image exists in this tree.** … A `cancro` TWRP build has to be obtained and its provenance
> recorded before the `--boot-image` argument means anything.

Until now the gate checked `--boot-image` by **parsing** it — a statement about a *format*, not about a
*file*. Any Android boot image would have been cleared to boot, and the stock recovery was accepted as a
mechanics stand-in for the real thing. This step closes that: a `cancro` TWRP build is fetched, its
provenance is recorded, and the gate now refuses any tool image it cannot name.

Host-side only: one image downloaded and verified **on the host**, one record file, one new tool, one
gate check and one reference section. No boot, no build, no device, no `fastboot`, no `adb`, **nothing
written to storage**. **TWRP stays withheld** — the criterion the gate enforces has still not been met by
any capture in this tree, so the gate has still never cleared on a real one.

## 1. What was obtained, and the two channels that agree about it

`twrp-3.7.0_9-0-cancro.img`, from TWRP's own device page for `cancro`, fetched 2026-09-23:

| field | value |
| --- | --- |
| bytes | 16,240,640 |
| sha256 | `a2f4b9037946ededf9f06f28dff922decebe2e7a0bf1bfff24b6e34327e02443` |
| md5 | `525f8796b1e9fa22a5fb7be3b89e76f1` |
| source page | `https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img.html` |
| signed by | `9570 7D42 307C 9D41 D09B F709 1D85 97D7 891A 43DF`, uid `TeamWin <admin@teamw.in>`, 2022-10-11 |
| parses as | `ANDROID!`, page 2048, kernel 9,066,373 B @ `0x8000`, ramdisk 7,171,052 B @ `0x1000000`, `dt_size=0`, cmdline `console=ttyHSL0,115200,n8 androidboot.hardware=qcom … androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=eng` |

**Two independent channels agree about one file.** The sha256 and md5 both equal the values TWRP
publishes for it, served from TWRP's own host; and the detached PGP signature verifies against TWRP's
release key. Neither channel is a copy of the other — one is a published digest, the other a signature
by a key that held no part in computing it — so a file that satisfies both is not a lucky download.

The honest limits, stated here rather than glossed in the reference: the signing key came from a public
keyserver and its UID is a self-claim, so what the signature proves is *which key signed these bytes*,
not that the key's owner is TeamWin — that half is a fingerprint worth confirming out of band. And the
image has **no appended DTB** (`dt_size=0`) while this project's own payload does, which is normal for
TWRP builds of this era and is a fact to carry rather than a defect to fix.

## 2. The first attempt *succeeded* and produced the wrong file

Worth recording because it is the whole reason a record hashes instead of naming. Fetching the image URL
directly with `curl` answers **HTTP 200** — with a 6,817-byte HTML interstitial, because the page's
cookie and referer are what redirect to the binary. A later attempt at the *mirror* host named in that
interstitial answered **404** and wrote a 5,800-byte error page to the `.img` filename.

Both are files that exist, at the right path, with the right name. The first attempt in this step did
exactly that and produced a 5,800-byte `twrp-3.7.0_9-0-cancro.img` — caught here only because the hash
was taken before anything else was done with it. **A name is not an identity**, and a download that
"worked" is not a file that is what it is called; the fetch that works is the one that goes *through* the
page (`-c` a cookie jar from the `.html`, then `-b` it and `-e` the page), and even that is only
believed after the bytes are hashed against a published value.

## 3. The record, and the check that reads it

`stages/stage90/tool-images.txt` — one line per image, keyed by sha256, carrying md5, size, the file
name, a **role**, the source page, the fetch date and the signer. `tools/verify_tool_image.sh` is what
compares an image against it, and `preflight_storage_write.sh` calls it with `--require-role=twrp`
**before it prints a single command**.

A record is only a constraint if something disagrees with it, so the second half of this step is the
refusals, each one a real invocation:

| what was passed | what it said | exit |
| --- | --- | --- |
| the recorded image | `VERIFIED` — hash, size, md5, parse, signature, role | 0 |
| `recovery.img` from the backup (**605's stand-in**) | hash `fbb01c55…` is NOT in the record | 1 |
| a one-byte-different copy | hash `3704ca49…` is NOT in the record | 1 |
| a file that does not exist | an absent file is not an image that failed to verify | 1 |
| an empty file | the file is empty | 1 |
| no record at all (`--record=/tmp/no-record.txt`) | nothing to compare against | 1 |
| a record line rewritten to `role=radio` | recorded with `role=radio`, caller requires `role=twrp` | 1 |
| a record line with `bytes=` off by one | size disagrees with the record | 1 |
| a record line with `md5=` zeroed | two hashes of one file disagreeing | 1 |

The stand-in row is the one that retires 605's shortcut: the stock recovery was accepted there because
the gate could only ask whether something *was* a boot image. It cannot be accepted here, and the
reason is a fact about the file rather than a rule about names.

## 4. The case a hash alone cannot catch, and what did

The interesting refusal is not on that table's terms. Take the real image, flip **one byte** at
`0x100000` (inside the kernel, past the header, so it still parses), and then rewrite the record so its
`sha256`, `md5` and `bytes` all describe the edited file. Every constraint 605 had — and every constraint
in §3 except one — is now satisfied: it parses, it is not the payload, and its hash *is* in the record.

The signature is what refuses it:

```
REFUSING: the detached signature in …/twrp-3.7.0_9-0-cancro.img.asc is NOT a good signature for
flip.img. The sha256 matched the record, so these two disagree about the same file, and a
disagreement between a hash and a signature is not a detail: do not boot this image
```

That is the argument for keeping the signature check even though the hash is the *constraint* and the
signature is only *corroboration*: an attacker who can edit the record can satisfy a hash, and cannot
satisfy a signature they do not hold the key for. The distinction is kept in the code rather than
flattened — a **bad** signature is a refusal, a **missing** one (or a host without `gpg`) prints
`NOT CHECKED` and still verifies everything else, because a check that succeeds by printing nothing
cannot be told from one that never ran.

## 5. A refusal that named the wrong fault — mine, found by doing it

Building the battery above required simulating a host without `gpg`, and the way to do that is a `PATH`
of symlinks to just the tools the script needs. The first attempt's links were built from `command -v`,
which in an interactive shell can resolve to a *function* name rather than a path — so the links were
self-referential, one of them was broken, and the run produced:

```
REFUSING: twrp-3.7.0_9-0-cancro.img hashes to a2f4b903…
          which is NOT in /mnt/data/mi4-ios6/stages/stage90/tool-images.txt
```

Every word of that is false. The hash *is* in the record; what was missing was `grep`, and the record
lookup had returned nothing. **A tool that is absent makes a check report a fact about the artifact when
it is reporting a fact about the host** — 595's class, a refusal naming the wrong fault, and this
project's most-repeated family. The fix is a guard at the top of the script that names the tools it is
made of before any of them speaks, and it is now a demonstrated refusal of its own:

```
REFUSING: this host is missing grep, which this script needs to hash, read or compare. A check that
          cannot run is not a check that passed …
```

The guard is the residue of a mistake, which is the only reason it exists — and it is the second time in
three steps that a synthetic `PATH` has produced a defect worth keeping.

## 6. What this does not do

* **It does not boot anything, and it changes no arm, payload or prediction.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not clear the gate, and it does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The
  cleared path was exercised on a **synthetic** log built to the shape the coming boot should produce,
  which is a test of the *gate* and not evidence that the criterion has been met — no capture in this
  tree meets it, which is why the gate has never cleared on a real one.
* **Nothing here was run on the device.** The image is verified on the host; whether it boots on this
  phone is a question for the device, and it will not be asked until the criterion is met.
* **It does not document an EDL path** for this phone, still the reference's other open gap.
* **It does not sweep the runner's `FAIL` branches** (603 §7, 604 §5, 606 §5, 607 §5) — still owed.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One image
downloaded to the repository root (gitignored by `*.img`, so it is not committed), one new tracked
record (`stages/stage90/tool-images.txt`), one new tool (`tools/verify_tool_image.sh`, mode 100755), the
signing key and detached signature under `stages/stage90/tool-images/`, one check added to
`preflight_storage_write.sh` and one section added to `docs/reference/recovery-and-rollback.md`. Reads of
the backup's own `SHA256SUMS.txt` manifest and of the tree's parsers. The boot gate was re-run afterwards
→ **EXIT=0 / 537 lines / 0 stderr**, unchanged, and the committed live-path rehearsal was run end to end
→ **7 ok / 0 failed** on the live-path states and **9 ok / 0 failed** on the reader states, exit 0. The payload, the parked frozen pair at
`/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified. `fastboot boot` only —
never `flash` — so no outcome of any of this can write to storage, and the gate still runs no `fastboot`
of its own.
