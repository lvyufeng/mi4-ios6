# Experiment 275 — `mac_policy_init`, and a tail call that reports its caller's caller

**Step:** link the object that defines `mac_policy_init` — `security/mac_base.c`, `manifest:664`, the
name 274's run stopped at — and take the next one-object step.
**Prediction:** `stub_hit=mac_labelzone_init`, with the caller at **`kernel_bootstrap+0x248`** — the
*same offset 274 measured*, because `mac_policy_init` ends in a tail call and the stub's `lr` is
therefore `mac_policy_init`'s own return address.
**Result:** exactly that, both halves. Resolved 3, added 32, 840 → 869 undefined. The run stopped at
`stub_hit=mac_labelzone_init` with `xnu_entry_stub_caller_v=0x8000e148` = **`kernel_bootstrap+0x248`**,
three roads agreeing, zero aborts — and the caller key alone cannot tell this stop from the previous
one. **The seventeenth prediction in a row.**

## The step, and what the object brought

`security_mac_base.o` is **10087 bytes of text, 1280 of data, 2120 of bss, 115 definitions and 72
references** — the first object this walk has taken out of `osfmk/` and `bsd/` into `security/`, and
the largest single step since 267. It resolved **3** (`mac_policy_init`, `mac_policy_initbsd`,
`mac_policy_initmach` — the only three of its definitions the image references at all) and added
**32**: the `sbuf_*` family (`sbuf_new`, `sbuf_setpos`, `sbuf_putc`, `sbuf_printf`, `sbuf_len`,
`sbuf_finish`), the MAC label surface (`mac_cred_label_*`, `mac_vnode_label_*`,
`mac_mount_label_externalize`, `mac_file_check_get`/`_set`, `mac_labelzone_init`), the `kauth_*`
credential calls, `namei`/`nameidone`/`vn_setlabel`, `vfs_context_current`, `sysctl__children`,
`strsep`, `act_set_astmacf`, `bsd_exception` and `_FREE`. 28 of the 72 references were already real
and 12 already stubs. Every one of the 32 is defined by an object the build has already compiled, so
each stand-in's size comes from its own definition — the check that has failed the build rather than
guess a size since 244. One of the 32 is storage, not a function: `sysctl__children`, 4 bytes.

Build: 840 → 869 undefined, 765 → 793 function stubs, 75 → 76 storage, text 938308 → 949572, image
bytes 1048440 → 1049720, `.bss` `0x800ffa00` .. `0x801355c8`, headroom 1878584 bytes, payload
1544176 bytes.

## The built function, and the `CONFIG_EMBEDDED` half

`mac_policy_init` is data setup and nine calls, and the data half says `CONFIG_EMBEDDED` is on in this
configuration:

```
00f4: vld1.64 {d16-d17}, [pc, #0xb4]   ; a 32-byte .rodata constant for the list header:
011c: vst1.32 {d16-d17}, [r1 :128]!    ;   numloaded 0, max 0x200 (MAC_POLICY_LIST_CHUNKSIZE),
                                       ;   maxindex 0, staticmax 0, freehint 0, chunks 1
0128: bl bzero(mac_policy_static_entries, 0x800)   ; 512 * 4 bytes
0144: bl lck_grp_attr_alloc_init    \  nine calls, all real, all already in this image
014c: bl lck_grp_attr_setstat       |
015c: bl lck_grp_alloc_init         |  (`locks.o` has been linked since 269; these are the
0164: bl lck_attr_alloc_init        |  same lock primitives 273 and 274 called on every run)
016c: bl lck_attr_setdefault        |
0178: bl lck_mtx_alloc_init         |
018c: bl lck_attr_free              |
0194: bl lck_grp_attr_free          |
019c: bl lck_grp_free               /
01a0: pop {r4, r5, r6, lr}
01a4: b mac_labelzone_init          ; a tail call, and a new undefined name
```

`mac_policy_list.entries` is set to `mac_policy_static_entries`, **not** to a `kalloc` — the
`#if CONFIG_EMBEDDED` branch, and this object defines that array itself
(`SECURITY_READ_ONLY_LATE(static struct mac_policy_list_element) mac_policy_static_entries[MAC_POLICY_LIST_CHUNKSIZE]`,
`mac_base.c:273`), so it needs no stand-in and no size decision. The 2048-byte `bzero` is over its own
static storage.

## The prediction, and why the caller key is not enough

`mac_policy_init` is entered by `bl mac_policy_init` from `kernel_bootstrap` at `+0x244`, so its own
`lr` is `+0x248`. Its prologue pushes that `lr`, its epilogue pops it back, and then the **tail call**
`b mac_labelzone_init` leaves *that* value in `lr` for the stub. So:

```
274:  stub_hit=mac_policy_init        caller = 0x8000e148   (the `bl` at +0x244)
275:  stub_hit=mac_labelzone_init     caller = 0x8000e148   (the same value, from a tail call)
```

`caller-4` resolves to the same `bl mac_policy_init` for both stops. **The name is what says which
function the walk has reached**, and this is the first time in this walk that the ambiguity is between
two *consecutive* stops. It is 246's and 252's shape for the third step running — 252 was a tail call
*into* a function the walk had already left; here it is a tail call *out of* one — and the reason the
prediction was written as an offset rather than an address is the same reason as the last four steps.

## What the run measured

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `mac_labelzone_init` | the prediction, named by the stub's own write |
| `xnu_entry_stub_caller` | `0x8000e148` | read out of `g_kv_buf`, written during the run |
| `xnu_entry_stub_caller_a` | `0x8000e148` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x8000e148` | the value itself, through the ram-console path |
| `xnu_entry_stub_caller_e` | `0x8000e148` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_stub_caller_digits` | `0x36` | 54 = 29 + 25 — the digits' offset, corrected in 273 |
| `xnu_entry_stub_caller_w0` | `0x30303038` | `8000`, the four bytes actually in `g_kv_buf` |
| `xnu_entry_stub_caller_w1` | `0x38343165` | `e148`, the next four |
| `xnu_entry_kv_written` | `0x63` | 99 = a 29-byte stub record plus 34- and 36-byte caller records |
| `xnu_entry_kv_in_dram` | `0x87` | 135 = 99 + 36, the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken |

Three roads to one value and a fourth that has ever disagreed — and this time all four agree, with the
report's echo intact:

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mac_labelzone_init
 xnu_entry_stub_caller=0x8000e148
 xnu_entry_stub_caller_a=0x8000e148
 xnu_entry_stub_caller_e=0x8000e148
```

What `mac_policy_init` did, read from its own instruction stream: the 32-byte constant into the list
header, the 2048-byte `bzero` of `mac_policy_static_entries`, both `LIST_INIT`s, and all nine lock
calls — the MAC framework's lock group and mutex now exist on this device.

Two things this run settles beyond the step:

- **The digits window reads from the first digit again.** `_w0 = 0x30303038` and `_w1 = 0x38343165` are
  `8000e148` read back out of `g_kv_buf`, and `digits = 0x36` = 29 + **25**. 273's off-by-one (a
  21-character key counted as 22, so the window began one byte late) is fixed *and verified on
  hardware*, by the field that was wrong before.
- **The report's echo is intact in this build**, where 274's was mangled twice identically. The same
  source-level reporter, two consecutive builds, two different outcomes — 272's caution arriving once
  more: **the entry image's report is not a function of its source either**, and the two-road rule is
  what has kept every step's result readable through it. The 661-word dump of `entry_kv` through
  `entry_stub_hit` also matches the linked ELF again, byte for byte, for the fourth build running.

## Safety

One run, `fastboot boot` only, nothing flashed. `persistent_write_attempted=0x00000000` in all 25
places, `failure_mask=0x00000000` in all 87 contracts, zero aborts, and the device returned to Android
on its own.

## What is next

`mac_labelzone_init` is `security/mac_label.c` (`manifest:669`) — the object that defines it is
already compiled, and it is the next step: the label zone plus whatever else that object brings. Behind
it in `kernel_bootstrap`'s line: `ipc_init` (real since 265), then `mapping_free_prime` (`8000e170`),
and after that stretch `bsd_init` — where "XNU loads, enters the operating system and runs its basic
drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above SECURITY_MAC_BASE_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x8000e148`;
`tools/host_resolve_entry_addr.sh 0x8000e148` resolves it against `out/stage90/xnu_arm_entry.elf` to
`kernel_bootstrap+0x248`, whose `caller-4` is `8000e144: bl 800b51b0 <mac_policy_init>`. The stop is
named by ` stub_hit=mac_labelzone_init`, and the two are consistent in the only way they can be: the
`b 800ced6c <mac_labelzone_init>` in `mac_policy_init`'s own tail is what put that `lr` there.
