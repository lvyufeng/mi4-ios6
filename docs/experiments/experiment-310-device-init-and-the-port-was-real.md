# Experiment 310 — `device_init.c`, and the master device port was real

**Step:** link **`osfmk/device/device_init.c`** (`out/xnu_kernel_obj/osfmk_device_device_init.o`,
manifest:506) — the object that defines `device_service_create`, the name 309 stopped on.

**Prediction:** *`device_service_create` runs to its last instruction, and the stop is the next call
`kernel_bootstrap_thread` makes — `kdp_init` at caller key `0x8000E63C`.* Named falsifier: the
object's **own** second call, `bl panic` at `+0x30`, taken when `ipc_port_alloc_special` returns
`IP_NULL` — `panic("can't allocate master device port")`. That would be the first candidate in this
walk that is a *panic* rather than a stub, and it is also the first falsifier that is not a stub
name: a panic prints its string, a stub prints `stub_hit=`, and the log says which happened.

**Result:** the prediction holds and the falsifier does not fire. `stub_hit=kdp_init`, caller key
`0x8000e63c` = **`kernel_bootstrap_thread + 0xbc`**. No panic line, and the string `master device`
appears nowhere in the log.

## The object, and why this step's answer was computable on the host

`osfmk_device_device_init.o` is **188 bytes of `.text`**, 28 of `.bss` and 43 of `.rodata.str1.1`
(`arm-none-eabi-size -A`), with **9 definitions and 11 references**: one function,
`device_service_create`, and eight symbols nothing outside it has ever heard of —
`master_device_port`, `master_device_kobject`, `dev_lck_grp`, `dev_lck_grp_attr`, `dev_lck_attr`,
`iokit_obj_to_port_binding_lock` (all `B`, 4 bytes each) and two `.L.str` constants.

The eleven references are `ipc_port_alloc_special` (behind the `ipc_port_alloc_kernel` macro),
`panic`, `ipc_kobject_set`, `host_priv_self`, `ipc_port_make_send`, `kernel_set_special_port`,
`lck_grp_attr_alloc_init`, `lck_grp_alloc_init`, `lck_attr_alloc_init`, `lck_mtx_init` and
`ipc_space_kernel`. Every one was already real in this image, and
`tools/stub_calls_in_function.py` reports **no stub call inside any of their bodies** — the same tool
the 308 and 309 notes introduced after a hand scan used the wrong predicate. So the whole function was
predictable before the device was touched, and the run is a confirmation rather than a discovery.
That is the state this walk has reached on the `kernel_bootstrap_thread` straight line: its calls are
real and this object's body is closed over them.

## The build: counts exact, `.bss` wrong

The baseline was built in this session with an **empty stand-in object** in this slot, and it
reproduces 309 exactly — 736 / 647 / 89, `.text` 0x11D980, image 0x138AAC, bss
0x80138AC0..0x8016F8D8, headroom 1640232 — so the deltas below are this object's and nothing else's.

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 735 / 646 / 89 | 735 / 646 / 89 |
| `.text` | 0x11D980 + 0xBC − 0x18 − 0x18 + 0x2B + fill | **0x11DA40** (+0xC0) |
| `.data` | 0x80120000 | 0x80120000 |
| `__bss_start` | 0x80138AC0 | 0x80138AC0 |
| `__bss_end` | unmoved, 0x8016F8D8 | **0x8016F918** (moved +0x40) |
| image | 0x138AAC | 0x138AAC |
| headroom | 1640232 | **1640168** (−0x40) |

Resolved **1** (`device_service_create`, this stop), added **0**. The two stub counts and the storage
count are exact. `.text` is the arithmetic landing: +0xBC for the object, +0x2B for its
`.rodata.str1.1` (merged into the `.text` output section), −0x18 for the retired stub body, −0x18 for
the retired stub name string (`device_service_create` in the stub object's `.rodata.str1.4`,
0x3278 → 0x3260), and **+0x9** of net alignment fill — the `.text` output section carried 22
`*fill*` entries before this step and 24 after. The image is unchanged because `.text` ends well
below the fixed `.data` at 0x80120000, so this step's 0xC0 is absorbed by slack.

**The `.bss` prediction was wrong, and it is the mirror image of 308's.** 308 predicted movement and
measured none, because the object's 0x18 of `.bss` fitted inside the 0x2C of alignment fill already
separating `commpage.o` from the stub object. Here the prediction was "unmoved" and the measurement
is +0x40:

```
 .bss   0x8016e1b0  0x1c  osfmk_device_device_init.o   <- this step's 28 bytes
 .bss   0x8016e1cc  0x0   xnu_arm_entry_rtabi.o
 *fill* 0x8016e1cc  0x34
 .bss   0x8016e200  0x1704 xnu_arm_entry_realstubs.o  <- the 89 stand-ins
```

The stub object's `.bss` is **64**-byte aligned, not 16 — `readelf -S` says `al 64`, and the reason is
in the generator: all **89** stand-ins carry `__attribute__((aligned(64)))`, so the section inherits
it. (308's note said "16-byte aligned", which is true of the spacing it observed but is not the
constraint; this step is where the difference shows.) The 0x10 of fill that existed in front of the
stub object cannot hold 0x1C, so the whole 0x1704 moved to the next 64-byte boundary at 0x8016e200. The section grew by exactly that move,
0x40 — **0x1C** of new input plus **0x24** more fill (the fill in front of the stub object went from
0x10 to 0x34) — and the trailing fill after it is 0x14 in both builds, which is why the end moves by
the same 0x40. `__bss_end` is the end of the *padded* output section either way: 308 was the case
where the padding absorbed the input, this is the case where the input pushed the padding along.
Headroom is `topOfKernelData − __bss_end`, so it moved by the same 0x40, from 1640232 to 1640168.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000059      xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80106bd8             xnu_entry_why_byte=0x00000061
 xnu_entry_stub_caller_v=0x8000e63c   xnu_entry_stub_caller_e=0x8000e63c
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
 xnu_entry_bss_start=0x80138ac0       xnu_entry_bss_end=0x8016f918
 xnu_entry_copied_bytes=0x00138aac    xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kdp_init
```

`tools/host_resolve_entry_addr.sh 0x8000e63c` → `kernel_bootstrap_thread+0xbc`, and the instruction
at `0x8000e638` is `bl 801037c8 <kdp_init>` — immediately after `bl 80102650 <device_service_create>`
at `0x8000e618`, which **returned**. Note what did *not* happen between them: the only instructions
at 0x8000e61c..0x8000e634 are the function's own return path and one `bl kernel_debug_string_early`
at 0x8000e634. There is no evidence anywhere in the log of the panic arm.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301620 bytes, exactly one `stub_hit=` line, **no `exception:` line**,
`xnu_entry_abort_entries` 0.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures: a port, and not a count

The step's content is not the +1/−0 in the symbol table. `device_service_create` does four things
with one object:

```c
	master_device_port = ipc_port_alloc_kernel();
	if (master_device_port == IP_NULL)
	    panic("can't allocate master device port");

	ipc_kobject_set(master_device_port, (ipc_kobject_t)&master_device_kobject, IKOT_MASTER_DEVICE);
	kernel_set_special_port(host_priv_self(), HOST_IO_MASTER_PORT,
				ipc_port_make_send(master_device_port));

	/* allocate device lock group attribute and group */
	dev_lck_grp_attr= lck_grp_attr_alloc_init();
	dev_lck_grp = lck_grp_alloc_init("device",  dev_lck_grp_attr);

	/* Allocate device lock attribute */
	dev_lck_attr = lck_attr_alloc_init();

	/* Initialize the IOKit object to port binding lock */
	lck_mtx_init(&iokit_obj_to_port_binding_lock, dev_lck_grp, dev_lck_attr);
```

so a return means: a real port object was allocated out of the real `ipc_space_kernel` (that is what
the `ipc_port_alloc_kernel()` macro is), tagged `IKOT_MASTER_DEVICE` with `master_device_kobject` as
its kobject, given a send right and installed at `HOST_IO_MASTER_PORT` in the host's special-port
table — `host_priv_self()` supplying the host the port is installed on. The three port calls are the
same three the image has been resolving since 278 (`ipc_port_alloc_special`), 283
(`kernel_set_special_port`) and 276/277 — this is the first time they have been reached from *this*
caller and the first time their results are stored in real storage rather than a zeroed stand-in.
The last three statements are the other half of the object: the lock group, the lock attribute and
the mutex, all initialized with real storage and a real group name, the `.rodata.str1.1` string
`"device"` — the object's second string, and the one that would have been an invented zero had this
step not linked the object.

The object's six lock/port globals are new names to this image rather than replacements for
stand-ins, and that is worth stating precisely: `grep` for `dev_lck_grp`, `dev_lck_grp_attr`,
`dev_lck_attr`, `iokit_obj_to_port_binding_lock`, `master_device_port` and `master_device_kobject`
against `xnu_arm_entry_realstubs.c` is **0** — nothing referenced them before this step, so the
generator never had a reason to invent them, and there was no zeroed stand-in for the real storage to
displace. They arrive with the object, 24 bytes of symbols in a 28-byte `.bss`.

## What it does not measure

* **Which thread ran.** 307's open question — the thread `sched_startup` created, or
  `processor->idle_thread` — is still open. 309 did not answer it either, and nothing in this run
  distinguishes them.
* **That the port is *reachable*.** `kernel_set_special_port` installs it, but nothing has looked it
  up yet: the first `task_get_special_port` for it is a long way down the line, in
  `iokit`/`mach_init` territory. Installing is not the same as being found.
* **That `kdp_init` is the last thing on this line.** It is only the next stub the image will call.

## Next

**`osfmk/kdp/kdp_udp.c`** (`osfmk_kdp_kdp_udp.o`, manifest:529) — the file that defines `kdp_init`.

It is a cheap step and a small object: **72 bytes of `.text`** and nothing else (no `.data`, no
`.bss`, no `.rodata`), 12 definitions and **1** reference. This configuration compiles the
KDP-disabled arm, so the body of `kdp_init` — like `kdp_register_send_receive`,
`kdp_unregister_send_receive`, `kdp_set_ip_and_mac_addresses`, `kdp_set_gateway_mac`,
`kdp_set_interface`, `kdp_register_link` and `kdp_unregister_link` — is a bare `bx lr`, and
`kdp_get_interface` / `kdp_get_ip_address` are `mov r0, #0; bx lr`:

```
00000000 <kdp_init>:
   0:	e12fff1e 	bx	lr
```

It resolves **2** (`kdp_init` and `kdp_raise_exception`, both already undefined in this image) and
adds **1** (`panic_spin_forever`, the target of `kdp_raise_exception`'s tail branch), so the
prediction is 735 → **733** undefined, 646 → **647** function stubs, 89 storage unchanged. Because
`kdp_init` is one instruction, the stop is predicted to be the very next call the image makes:
**`kpc_init` at caller key `0x8000E640`** (`bl kpc_init` at `0x8000e63c`).
