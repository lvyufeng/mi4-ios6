# Experiment 311 — `kdp_udp.c`, and the step whose `.text` did not move at all

**Step:** link **`osfmk/kdp/kdp_udp.c`** (`out/xnu_kernel_obj/osfmk_kdp_kdp_udp.o`, manifest:529) —
the file that defines `kdp_init`, the name 310 stopped on.

**Prediction:** *`kdp_init` is one instruction, so it returns and the stop is the very next call the
image makes — `kpc_init` at caller key `0x8000E640`.* Plus counts: 2 resolved / **1 added**
(`panic_spin_forever`), 735 → 733 undefined and 646 → 647 function stubs.

**Result:** the run lands exactly on the key — `stub_hit=kpc_init` at
`xnu_entry_stub_caller=0x8000e640` = `kernel_bootstrap_thread + 0xc0`. **The count prediction was
wrong in the other direction: 2 resolved / 0 added**, and `.text` did not move at all.

## The object: 72 bytes, and twelve functions that are one instruction each

`osfmk_kdp_kdp_udp.o` is **72 bytes of `.text`** and nothing else — no `.data`, no `.bss`, no
`.rodata`, no `.rodata.str1.1` — with **12 definitions and 1 reference**. This configuration compiles
the KDP-disabled arm of `kdp_udp.c`, so the whole file is:

```
00000000 <kdp_init>:                    bx lr
00000004 <kdp_register_send_receive>:   bx lr
00000008 <kdp_unregister_send_receive>: bx lr
0000000c <kdp_get_interface>:           mov r0, #0; bx lr
00000014 <kdp_get_ip_address>:          mov r0, #0; bx lr
0000001c <kdp_get_mac_addr>:            mov r1, #0; strh r1,[r0,#4]; str r1,[r0]; bx lr
0000002c <kdp_set_ip_and_mac_addresses>: bx lr
00000030 <kdp_set_gateway_mac>:         bx lr
00000034 <kdp_set_interface>:           bx lr
00000038 <kdp_register_link>:           bx lr
0000003c <kdp_unregister_link>:         bx lr
00000040 <kdp_raise_exception>:         mov lr, pc; b panic_spin_forever
```

Its one reference is `panic_spin_forever`, the target of `kdp_raise_exception`'s tail branch.

## The build: `.text` unchanged, and a count that was wrong in the other direction

The baseline was built in this session with an **empty stand-in object** in this slot and reproduces
310 exactly (735 / 646 / 89, `.text` 0x11DA40, image 0x138AAC, headroom 1640168).

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 733 / 647 / 89 | 733 / **644** / 89 |
| `.text` | 0x11DA40 + 0x48 + 0x8 − 0x30 − 0x20 | **0x11DA40** (unmoved) |
| image | 0x138AAC | 0x138AAC |
| `__bss_start` / `__bss_end` / headroom | unmoved | unmoved |

**The prediction was wrong about `panic_spin_forever`.** I read "absent from the undefined list" as
"this image has never heard of it, so linking the object will make it a stub". That is only one of the
two things absence means — the other is **already defined by something already linked**, and
`panic_spin_forever` is defined by `osfmk/kern/debug.c`, which has been in this image since long
before the walk reached the bootstrap thread. Nothing had ever *referenced* it, which is why it was in
neither the undefined list nor the image's stub set. One `nm` on the image settles which reading is
right; the check was not made, and the build made it instead.

`.text` not moving is the arithmetic landing exactly, and it is the first time in this walk that a
step linked an object and changed nothing about the section:

```
  this object's .text                                   +0x48
  retired stub bodies: kdp_init, kdp_raise_exception    -0x30   (2 x 0x18)
  retired stub name strings, both padded                -0x20   (12 + 20)
  alignment fill, across the whole .text section        +0x08
                                                       ------
                                                         0x00
```

The map shows each term: the stub object's `.text` 0x3C90 → 0x3C60, its `.rodata.str1.4`
0x3260 → 0x3240, the section's total fill 35047 → 35055. Nothing about `.bss` could move, because the
object has no `.bss` and no storage at all.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8000e640   xnu_entry_stub_caller_e=0x8000e640
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
 xnu_entry_bss_start=0x80138ac0       xnu_entry_bss_end=0x8016f918
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kpc_init
```

`tools/host_resolve_entry_addr.sh 0x8000e640` → `kernel_bootstrap_thread+0xc0`, and the instruction
at `0x8000e63c` is `bl 801039d8 <kpc_init>` — immediately after the `bl kdp_init` at `0x8000e638`
that this step made return.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301620 bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures, and what it deliberately does not

The run measures one thing: **`kdp_init` returned**, which in this configuration means the kernel's
debugger stub layer is compiled out — there is no KDP to initialize, no serial or UDP transport, no
exception hook. That is worth stating as a *negative* result, because a step this cheap is tempting to
describe as progress: 311 does not measure that the kernel debugger works, it measures that this
configuration does not have one, and the value of the step is that a name left the frontier for 72
bytes.

* **`kdp_raise_exception` is now real, and it is a trap.** The image now contains a definition of
  `kdp_raise_exception` whose entire body branches to `panic_spin_forever`. Nothing calls it yet —
  `kdp_init` does not install it anywhere, because the KDP-disabled `kdp_init` does nothing. If
  anything reaches it, the machine spins rather than reports, which is exactly what `panic_spin_forever`
  is for; but it is the first *real* function this walk has linked that is a deliberate hang.

## Next

**`bsd/kern/kern_kpc.c`** (`bsd_kern_kern_kpc.o`, manifest:39) — the object that defines `kpc_init`,
and a much larger step than this one: `.text` **1796**, `.bss` 24, `.rodata.str1.1` 428, `.data`
**672** and a 56-byte `__DATA,__sysctl_set`, with 33 definitions and 28 references. It is the first
step that can move a section other than `.text` and `.bss`.

`kpc_init`'s body is three real lock calls — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init("kpc", …)`,
`lck_mtx_init(&sysctl_lock, …)` — and then `kpc_arch_init`, `kpc_common_init`, `kpc_thread_init`. So
the stop is predicted at **`kpc_arch_init`, caller key `0x8010279C`** (`bl kpc_arch_init` at
`0x80102798`), with `kpc_common_init` at `0x801027A0` as the alternative if `osfmk/arm/kpc_arm.o` turns
out to be already linked. `kpc_thread_init` is not a candidate: `osfmk_kern_kpc_thread.o` has been in
`LINK_OBJS` since long before this walk reached `kpc_init` — and that fact is the correction 312's own
count prediction needs, made twice now.
