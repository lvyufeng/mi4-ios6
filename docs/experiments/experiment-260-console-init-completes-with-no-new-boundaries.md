# Experiment 260 — `console_init` Completes, and the Link Adds Nothing

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

259's stop was `console_init`, the first target in `kernel_bootstrap`'s straight line that is not an
allocator or a lock group. It is `osfmk/console/serial_console.c:166` — note the directory:
`osfmk/console/`, which the manifest has listed all along
(`out/xnu_arm_manifest.txt:477-480`: `serial_console.c`, `serial_general.c`, `video_console.c`,
`video_scroll.c`). The object is `out/xnu_kernel_obj/osfmk_console_serial_console.o`.
**`OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ` is the change.**

**This is the cheapest step in a long time, and the measurement that makes it so.** The object is 2646
bytes of text, 36 of data, 52 of bss, with **27 references — and every one of the 27 is already
satisfied by this image**, so the link adds nothing at all:

```
resolved  console_init  console_write  console_cpu_alloc  cngetc  cnputc  cnputc_unbuffered
          _serial_getc  cons_ops_index  nconsops
added     (none)
```

Nine stand-ins become real code, and no new boundary is created. It is the same shape as 258's
`libkern/os/log.o` — an object the manifest already builds, waiting for the entry image to link it —
taken one step further, because this one needs nothing new in return.

## The prediction

`console_init` calls only `OSCompareAndSwap` (real — `ldrex` at `0x80057ec4`), `kmem_alloc` (real),
`panic` (real) and `arm_usimple_lock_init` (real, the ARM layer); `console_ring_lock_init` and
`hw_lock_init` are inline. So it completes, and what it does is the third real kernel allocation this
frontier has made:

```c
if (!OSCompareAndSwap(0, KERN_CONSOLE_RING_SIZE, (UInt32 *)&console_ring.len))
        return;
ret = kmem_alloc(kernel_map, (vm_offset_t *)&console_ring.buffer,
                 KERN_CONSOLE_BUF_SIZE, VM_KERN_MEMORY_OSFMK);
```

— guarded so that the first caller wins and any later one returns early. It returns to
`kernel_bootstrap+0x1e8`, where the straight line is `kernel_debug_string_early` (real) and then

```
8000dc34  bl stackshot_init    ; STUB at 0x8009a500
```

**The prediction: `stub_hit=stackshot_init`, `xnu_entry_stub_caller=0x8000dc38`** — `caller - 4` =
`0x8000dc34` = `kernel_bootstrap+0x1f4`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003b
 xnu_entry_kv_in_dram=0x0000003b
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=stackshot_init
 xnu_entry_stub_caller=0x8000dc38

No errors detected
```

`0x8000dc38` resolves to `kernel_bootstrap+0x1f8`, whose `caller - 4` is
`8000dc34: bl 8009a500 <stackshot_init>` — the prediction, address for address, for the **third step in
a row**. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x3b`, two bytes above 259's `0x39`: `stackshot_init` is two characters
longer than `console_init`, and the KV buffer records the stub's name verbatim.

## Cost

| | exp-259 | now |
| --- | --- | --- |
| undefined | 605 | **596** (9 resolved, **0 added**) |
| function stubs | 527 | **520** |
| storage stubs | 78 | **76** |
| entry text | 708644 B | **711044 B** (+2400) |
| entry image | 802248 → 818656 B | **818696 B** (+40) |
| entry `.bss` end | 0x800f6b88 | **0x800f6b88**, unchanged |
| headroom | 1086584 B | **1086584 B**, unchanged |
| derived `args` offset | +1015808 | **+1015808**, unchanged |
| payload text | 1310874 B | **1310914 B** (+40) |

The text grew 2400 bytes and the image only 40, so the growth fitted inside the linker script's
alignment padding; and for the first time since 257 nothing derived moved — not `.bss`, not the
`args` offset, not `topOfKernelData`, not the headroom. The payload moved by exactly the image's 40.

## What is next

`stackshot_init` — `osfmk/kern/stackshot.c`, an ordinary tree symbol. Behind it the same measured
straight line, every target still a stub:

```
8000dc34  bl stackshot_init     <- stub
8000dc44  bl sched_init         <- stub
8000dc54  bl ltable_bootstrap   <- stub
8000dc64  bl waitq_bootstrap    <- stub
8000dc74  bl ipc_bootstrap      <- stub
```

`sched_init` is the interesting one: a scheduler is not an allocator or a console, and it is the first
target in this list whose dependencies may reach outside its own object. `waitq_bootstrap` and
`ipc_bootstrap` follow, and then `kernel_bootstrap` is past its initialisation block — the run-in to
`bsd_init`, where "XNU loads, enters the OS and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# the object, and that every one of its references is already satisfied
grep -n 'osfmk/console' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_console_serial_console.o | grep ' T console_init'
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_console_serial_console.o | wc -l   # 27

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -23 <(sort /tmp/undef_259.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 9
comm -13 <(sort /tmp/undef_259.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # empty
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^8000dc34:/{print; exit}'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc38     # -> kernel_bootstrap+0x1f8
```
