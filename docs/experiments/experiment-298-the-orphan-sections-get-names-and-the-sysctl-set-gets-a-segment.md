# Experiment 298 — the orphan sections get names, and `__DATA,__sysctl_set` gets a segment

**Step:** the six Mach-O input-section names the objects carry become named output sections in
`entry.ld`: `__TEXT,*` and `.ARM.exidx` into `.text`, `__DATA,*__data` and `__DATA,*__const` into
`.data`, and `__DATA,__sysctl_set` into an output section of its own placed between `.data` and
`.bss`. Plus a `verify_sections` check that closes the set.
**Prediction:** that no function address moves — the run's stop and caller stay
`kpc_thread_create` / `0x8000b034` — while `__entry_text_end` and `__entry_data_start` move +0x9C0,
`.bss` and `__bss_start` +0xC0, and the image +164.
**Result:** every line but one, and the miss was a prediction written against the wrong column —
`size`'s text is not `.text`.

## What 297 left standing

297 fixed what the orphans *broke*: the payload's `memset` no longer runs over them. It did not stop
them being orphans, and the same build output that found 297's defect listed five sections the linker
had placed by its own rules. Two of those placements are wrong for reasons that have nothing to do
with the memset:

* **`__TEXT, initcode` is code** — `memorystatus_init` and `memorystatus_freeze_init`
  (`bsd/sys/kern_memorystatus.h:370,463` put them there by section attribute) — and as an orphan it
  sat inside what this script's Mach-O header calls `__DATA`, the writable region.
* **`__DATA,__sysctl_set` is read by name through that same header.**
  `LINKER_SET_BEGIN(__sysctl_set)` is `getsectdatafromheader(_mh_execute_header, "__DATA", _set,
  &size)` (`bsd/sys/linker_set.h:193`) — a walk of *this image's Mach-O header*, asking for the
  string `"__sysctl_set"` in the segment `"__DATA"`. The section must therefore be inside the
  `__DATA` **segment**, and in the read-only group it is inside `__TEXT`, where no header entry could
  ever find it.

## The complete set, and a correction to 297

Measured once over all 695 objects and used here — name, objects carrying it, and the flags of the
*input* section:

| name | objects | flags | where the linker put it in 297 |
|---|---|---|---|
| `__TEXT,__const` | 1 | `A` | read-only group after `.text` |
| `__TEXT,__os_log` | 5 | `A` | read-only group after `.text` |
| `__TEXT, initcode` | 1 | `AX` | read-only group after `.text` |
| `.ARM.exidx` | — | `AL` | read-only group after `.text` |
| `__DATA, __const` | 1 | `WA` | **after `.data`, before `__bss_start`** |
| `__DATA, __data` | 207 | `WA` | **after `.data`, before `__bss_start`** |
| `__DATA,__sysctl_set` | 105 | `A` | read-only group after `.text` |

**That table corrects a claim 297 made.** 297 called the placement "the clearest evidence available
that ld's orphan placement is not a policy anyone can rely on", citing `__DATA,__sysctl_set` landing
with the read-only group while the other `__DATA` sections landed with `.data`. The flags say the
opposite: ld put every `A`-only section with the read-only group and every `WA` one after `.data`,
consistently and explainably. The lesson is narrower and sharper than "ld is arbitrary": **this
image's layout contract depended on a decision it had not made, and a placement that follows the
linker's rules rather than the contract's is exactly as invisible when it is wrong.** The rule is
"name every input section the objects produce", not "distrust ld". 297's own `Next` paragraph and
its ledger block carry the same correction.

## The change

```ld
    .text : {
        ...
        *("__TEXT,*")            /* __TEXT,__const, __TEXT,__os_log, __TEXT, initcode */
        *(.ARM.exidx)
    }
    .data : {
        ...
        *("__DATA,*__data" "__DATA,*__const")
    }
    . = ALIGN(4);
    .sysctl_set : {
        __entry_sysctl_set = .;
        *("__DATA,*__sysctl_set")
        . = ALIGN(4);
        __entry_sysctl_set_end = .;
    }
    __entry_sysctl_set_size = __entry_sysctl_set_end - __entry_sysctl_set;
```

**The quoting is load-bearing, and it was tested with a two-object link before the change was
written.** A pattern containing a space or a comma has to be quoted or the linker cannot parse it, and
a quoted pattern still takes wildcards — so `*("__DATA,*__data")` covers both of XNU's spellings
(`"__DATA, __data"` with the space, from `vm_types.h`'s `VM_ALLOC_SITE_STATIC`, and `__DATA,__data`
without, from `linker_set.h`'s `__LS_VA_STRCONCAT`) with one entry.

**`.sysctl_set` is not merged into `.data`,** even though its contents are four-byte pointers and
`.data` is where a pointer belongs. Its *name* is its interface: a section merged into `.data` has no
address of its own, so no Mach-O section entry could describe it, and `getsectdatafromheader` is the
only way XNU reads the set.

**`entry_macho.s` deliberately does not change in this step.** The header still declares no
`__sysctl_set` section, so nothing can find the set — which is also true today, since nothing in this
image calls `SYSCTL_OID`'s consumer. The section entry, and the `host_entry_macho_check.sh` range
assertion that must go with it, belong to the step that links `bsd/kern/kern_sysctl.c`. The two
symbols above are what that entry will be written from.

## The build

| | predicted | measured |
|---|---|---|
| undefined / function / storage | 772 / 682 / 90 | 772 / 682 / 90 |
| `.text` size | 0x116360 | 0x116360 |
| `.data` | 0x80118000, 0x18930 | 0x80118000, 0x18930 |
| `.sysctl_set` | 0x80130930, 0xa4 | 0x80130930, 0xa4 |
| `.bss` | 0x80130a00 | 0x80130a00 |
| `__bss_start` / `__bss_end` | 0x80130a00 / 0x801677d8 | 0x80130a00 / 0x801677d8 |
| `__entry_text_end` = `__entry_data_start` | 0x80116360 | 0x80116360 |
| `__entry_data_size` / `_filesize` | 0x51478 / 0x1a6a0 | 0x51478 / 0x1a6a0 |
| image | 1247700 | 1247700 |
| `verify_pad` writes | +0xC0 | 0x801677c8 / 0x801677cc |
| **`size`'s text column** | **+0x9C0** | **+12** |

and three new build lines:

```
  the allocated output sections are exactly .bss .data .sysctl_set .text - nothing is where the linker put it
  __bss_start 0x80130a00 is the .bss output section's first byte (224728 bytes to 0x801677d8)
  the copied image ends at 0x801309d4, 44 bytes below __bss_start, so the memset touches nothing that was copied
```

### The one wrong line, and why it is worth a paragraph

The `+0x9C0` came from `.text` growing by the read-only material that moved into it, and `.text` grew
by exactly that. The `size` column grew by 12, because **`size`'s text is not `.text`**: `size` adds
every `ALLOC` section that is not writable to its text column, so in the 296/297 layouts that column
was `.text` **plus the 2648-byte orphan group** — 1139704 against `.text`'s 1137056, a difference the
ledger had been reading as "text size" for four experiments — and now it is `.text` plus
`.sysctl_set`'s 164 bytes. `+0x9C0 − (2648 − 164) = +12`, exactly.

So the column is derivable after all, from the section list, but it is a different quantity from the
one every prediction here is about. The build report now prints `.text` from `__entry_text_size`
instead of `size`'s text column, and the miss is recorded rather than the number quietly changed.
This is the [[mi4-measurement-defects]] shape in its mildest form: a number that meant something
other than its label, found because a prediction was written against it.

## The run

```
stub_hit=kpc_thread_create    xnu_entry_stub_caller=0x8000b034     (unchanged, as predicted)
xnu_entry_bss_start=0x80130a00                                     (was 0x80130940)
xnu_entry_bss_bytes=0x00036dd8                                     (unchanged - `.bss` moved, not grew)
xnu_entry_copied_bytes=0x001309d4                                  (was 0x00130930, +0xa4)
```

`0x8000b034` is `thread_create_internal + 0x348`, the same address this walk has reported since 296.
That is the step's own claim about itself, measured: the read-only material was appended after every
object in the link, `.data`'s address did not change, and **nothing that executes moved** — only the
addresses inside `.bss` that the build derives from it. The copy grew by exactly the sysctl set's
0xa4 bytes, because the set is now the last file-backed section and `objcopy` ends the binary there.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301118 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 299 — `osfmk/kern/kpc_thread.c` for `kpc_thread_create`, and
`sched_set_thread_base_priority`/`sched_thread_mode_demote` (`osfmk/kern/priority.c`), which closes
`thread_create_internal`; after that `kernel_thread_create` returns a real thread to
`kernel_bootstrap`, which calls `thread_deallocate` and branches to `load_context` — the first time
this walk crosses into a context switch rather than a function call. Two things to carry forward:
`entry.ld` now defines `__entry_sysctl_set`, `__entry_sysctl_set_end` and `__entry_sysctl_set_size`,
which `entry_macho.s` can use directly — it is assembled into the same link and already takes its
`__TEXT`/`__DATA` numbers from linker symbols that way — and the set's address is inside `__DATA`
now, which is what the `"__DATA"` segment argument of `getsectdatafromheader` requires.
