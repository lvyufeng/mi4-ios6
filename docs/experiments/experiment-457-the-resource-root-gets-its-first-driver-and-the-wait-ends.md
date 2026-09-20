# Experiment 457 — the resource root gets its first driver, and the wait that could not end ends

**Status: built, gated, run on hardware — and the wait that could not end ends.** 456 left exactly one ingredient unnamed: `doServiceMatch`
fills the `IOResourceMatched` array that `IOFindBSDRoot`'s 30-second wait turns on
(`IOService.cpp:3751`) only `if (keepGuessing && matches->getCount() && (kIOReturnSuccess ==
getResources()))` (`:3724`), and it could not see `matches`. This step reads the source for the two
conjuncts 456 called unknown and links the third: `IOResources` overrides no `getResources`
(`IOServicePrivate.h:183-198`) and `IOService::getResources` returns `kIOReturnSuccess`
unconditionally (`IOService.cpp:982-985`), so the only variable left is `matches`, which *is*
`gIOCatalogue->findDrivers(this, &generation)` (`IOService.cpp:3688`) — and `findDrivers` files
personalities by **`IOProviderClass`** (`IOCatalogue.cpp:124-132`) and looks them up along the
service's own class chain (`IOCatalogue.cpp:199-231`). The resource root's chain is `IOResources`
then `IOService`, and a kernel with no kexts has no personality under either. So the step adds one:
**`MSM8974RootResource`**, a concrete `IOService` subclass whose personality is
`IOProviderClass = IOResources`, `IOResourceMatch = "IOBSD"` — the first driver this project links
into the image — plus the one wrapper that reads the candidate set
(`IOCatalogue::findDrivers(IOService *, SInt32 *)`, one `bl` in the whole image, at `0x80134378`
inside `doServiceMatch`).

## What the step adds, and where it came from

| piece | file | why it is that and not something else |
| --- | --- | --- |
| the driver | `stages/stage90/xnu_platform/MSM8974RootResource.cpp` (new) | the personality's provider class is the whole of its effect on the boot; its `start` records what it was handed and returns true |
| the personality | `stages/stage90/xnu_platform/stage90_platform_config_tables.c` | third entry, between the platform expert and Apple's fallback; **no `IOProbeScore`**, because a score ranks siblings in one provider-class array and this array has one member |
| the build | `tools/build_xnu_arm_kernel.sh` (`PLATFORM_SOURCES`) | compiled by the same loop and with the same flags as `MSM8974PlatformExpert.cpp`: it is an iokit translation unit with an `OSMetaClass`, a vtable and a `.init_array` entry |
| the link | `stages/stage90/xnu_arm_boot/build_entry.sh` | inserted immediately **before** `MSM8974PlatformExpert.o` in `LINK_OBJS`, so the `.init_array` order stays "Apple's, then this project's, then `last_kernel_constructor`" |
| the reading | `entry_trace.c`'s `__wrap__...findDriversEP9IOServicePl`, `entry_stubs.c`'s `entry_note_finddrivers` | `findDrivers` is in another object from its only caller, so `--wrap` reaches it; the wrapper filters on `service == getResourceService()` the way 455's `matchPassive` wrapper does |

Nothing in the driver is guessed. The personality's provider class is what `findDrivers` looks up;
its `IOResourceMatch` is the resource the driver that will own this machine's root device needs, and
it is the resource `IOKitBSDInit` publishes immediately before it waits
(`IOService::publishResource("IOBSD")`, `IOKitBSDInit.cpp:95`, whose value is `gIOServiceKey` because
`publishResource` substitutes it for a NULL — `IOService.cpp:3534-3536`, so the property exists and
is non-NULL, which is what both branches that test it need).

**One consequence the source states and the code makes unavoidable**: the same missing array is why
nothing ever woke the waiter. The notification that ends the sleep is filtered by
`matchPassive(notify->matching, 0)` (`IOService.cpp:4782-4784`, inside `copyNotifiers`), and the
notifier `waitForMatchingService` registers carries the *same* dictionary — so with no array there is
no wake *and* no match, and with the array there are both. 456 saw the state word go to `0x1e` and
the sleeper stay asleep; that pair is this paragraph.

## The prediction, written before the build and before the run

The array is now written on the registration the config thread processes after `"IOBSD"` is
published, so:

1. **`xnu_live_finddrv_seq` ≥ 1 with `xnu_live_finddrv_svc = 0xc0589f78`** — the pointer 455 and 456
   both measured as the resource root — and **`xnu_live_finddrv_count ≥ 1`**, where 456's image would
   have read 0. The count is `matches->getCount()` itself, not a proxy for it.
2. **`xnu_live_rootdrv_seq` ≥ 1**, `xnu_live_rootdrv_prov = 0xc0589f78`, `xnu_live_rootdrv_iobsd != 0`
   — the driver was instantiated from the catalogue and started, with `"IOBSD"` already published.
3. **The second wait's registry reading flips**: `reg_rm != 0` (the array exists) where 456 measured
   `0`, `reg_count >= 1`, and `reg_idx` is `0` if the snapshot the config thread took already
   contained `"IOBSD"` and `0xffffffff` if it took an earlier one — both are meaningful and the third
   reading below says which.
4. **A third `xnu_live_iolock` record at the same site** (`0x80136174` = `waitForMatchingService+0xe0`;
   the site is inside `IOService.o`, which the step does not move), with
   **`dl - now ≈ 60 s` in ticks — about `1152000000`, twice wait 2's `575999988`** — because the wait
   that returns is `IOFindBSDRoot`'s first one and the next thing it does is wait for an `IOMedia`
   whose `Content` is `Apple_HFS` with `ROOTDEVICETIMEOUT`, which is 60 in this configuration
   (`iokit/bsddev/IOKitBSDInit.cpp:47-50`, `DEBUG` undefined). So the frontier moves from "a wait that
   cannot end" to "a wait for a driver that does not exist yet" — which is the honest shape of where
   this boot now is.

Falsifiers, named in advance: (a) the run looks exactly like 456's — which would mean the
personality never reached the catalogue (a parse or class-name error in the table, which the
`finddrv_count` line would localise to 0) or that `findDrivers` is not the gate after all;
(b) `finddrv_count >= 1` and `reg_rm = 0` — the set is non-empty and the array still is not written,
which would put the frontier inside `probeCandidates`/`copyPropertyKeys` and not in the catalogue;
(c) `rootdrv_seq = 0` with `finddrv_count >= 1` — the candidate is filed correctly but not
instantiated (an unregistered class, or `IOResourceMatch` failing at the moment of the candidate
test), which is a *partial* success: the array would still be written and the wait would still end;
(d) a stop inside `MSM8974RootResource::start` (`stub_hit=`, `exception:`) — the driver ran and
faulted; (e) **no third wait record and no stub hit, but `!BSD`-equivalent silence** — a wake with a
match that then blocks somewhere the instrument does not wrap; (f) `abort_entries != 0` or a
`panic:` line — a fault rather than a stop.

## The build, and the numbers it made

    xnu_entry_455: 34 --wrap'd symbols: 31 reached by a branch in this image,
                   2 same-object-only (copyExistingServices, matchPassive),
                   1 never called here (sleep)

`.text` 5016704 -> **5018624** (+0x780: the driver's `.text` 0x1C4 and `.rodata` 0x38C, the
wrapper, and the note function), `.init_array` **70 -> 71 entries** with the new one at **index 36**,
immediately before `MSM8974PlatformExpert.cpp`'s (37) and `last_kernel_constructor`'s (38) — the
order the placement in `LINK_OBJS` was chosen for. The copied image ends at `__init_array_end`:
**5224980 -> 5224984**, a four-byte image for a four-byte table entry, with `.text`'s growth absorbed
by the 16 KB alignment gap before `.data` (which does not move) and the `.bss` range the same one 455
and 456 report, the new slots landing inside it. Payload `c7cb2884...`, 8245248 bytes.

`_ZN19MSM8974RootResource5startEP9IOService` at **0x80268b44**, `_ZN19MSM8974RootResource10gMetaClassE`
at **0x8053ba74**, `_ZTV19MSM8974RootResource` at **0x8048d930**, the wrapper at **0x8045626c**, and
`entry_note_finddrivers` at **0x80004404** with its five slots in `.bss` at `0x804fce68`.

## The run (2026-09-20): the wait ends, and the frontier is a device that does not exist

    LOGFILE=/tmp/cancro-457-last_kmsg.txt ./run_and_capture.sh --allow-xnu-entry
    wrote 306521 bytes, exit 0, device back on its own

Three `xnu_live_iolock` records, all three at the **same site** and all three `ent=1`
(`IORecursiveLockSleepDeadline` — and the image has **exactly one `bl` to it**, at
`waitForMatchingService+0xDC`, so every deadline sleep this boot takes is that function's). Each one is
identified by the matching-dictionary record the factory wrapper wrote immediately before it, and by
the deadline it was handed:

| # | the dictionary, built just before | `now` | `deadline − now` | `reg_state0` | `reg_rm` | `reg_count` | `reg_idx` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `resourceMatching("IORTC")`, from `IOKitInitializeTime` (`dict_site=0x8011d5f8`) | 6.0772 s | 575999988 = **30.0000 s** | `0x00000000` | `0x00000000` | 0 | `0xffffffff` |
| 2 | `serviceMatching(gIOResources)` + `IOResourceMatched="IOBSD"`, from `IOFindBSDRoot+0x2C` | 6.0876 s | 575999988 = **30.0000 s** | `0x0000001e` | `0xc04a3720` | 2 | `0xffffffff` |
| 3 | `serviceMatching("IOMedia")` + `Content="Apple_HFS"`, from `IOFindBSDRoot+0x5C8` | 6.0885 s | 1151999988 = **60.0000 s** | `0x0000001e` | `0xc04a36e0` | **5** | **`0x00000004`** |

**Wait 1 is a wait neither 455 nor 456 knew about, and its failure is the frontier's own symptom.**
It is `IOKitInitializeTime`'s 30-second wait for the `IORTC` resource (`bsd/kern/bsd_init.c:728` →
`iokit/Kernel/IOStartIOKit.cpp:67`), whose table names `IOResourceMatch`, so
`IOResources::matchPropertyTable` answers it from its **first** branch — `0 != getProperty("IORTC")`
— which `MSM8974PlatformExpert::start`'s own `publishResource("IORTC")` satisfies. It still does not
match, because `copyExistingServices` will not return a service that is not yet in the matched state,
and at 6.0772 s the resource root's `__state[0]` is **`0x00000000`** — nothing at all had been
registered yet. It is woken 0.18 ms later, on the same tick as the config thread's first
`doServiceMatch` for the resource root (`finddrv_seq=1`, `0x06f47db9`), and `IOKitInitializeTime`
ignores the result and calls `clock_initialize_calendar()`.

**Wait 2 is the one that could not end, and it ends 0.32 ms in.** `IOFindBSDRoot` builds
`{"IOProviderClass"="IOResources", "IOResourceMatched"="IOBSD"}` (`IOKitBSDInit.cpp:377-379`) — the
**third** branch, the `IOResourceMatched` array (`IOService.cpp:5106-5118`) — and the array it reads
exists (2 keys, written by the first tail) and does not contain `"IOBSD"`. So it sleeps at
`0x800144bc` (`lck_mtx_sleep_deadline`, the sleep the record's next block records). Then, on the
config thread, in 0.32 ms: `finddrv_seq=2` (the same tail, count 1), an allocation
(`alloc_name=0xc0459210` — the driver instance), `rootdrv_seq=1` with `iobsd=0xc0459400`, and at
`0x06f79363` **`block_return=0x800144bc block_returns=9`** — the boot thread's sleep returned. The
wake is the notifier, not a re-probe: `copyNotifiers(gIOMatchedNotification, ...)` keeps the
notifiers whose `matchPassive(notify->matching, 0)` passes, and the waiter's dictionary is the one
whose third branch now finds `"IOBSD"` in the array the second tail rewrote. `!BSD` was not printed —
`IOFindBSDRoot` took the `if (service)` arm (`IOKitBSDInit.cpp:379-383`).

**Wait 3 is where the boot now is.** `IOFindBSDRoot` walked its boot-arg/`rd`/`rootdev`/UUID logic
(the boot args carry none of those), built the `IOMedia` table (`serviceMatching("IOMedia")`,
`dict_name` = the literal `"IOMedia"` at `0x804823ed`, then `Content = "Apple_HFS"`,
`IOKitBSDInit.cpp:526-535`) and entered

    do { t.tv_sec = ROOTDEVICETIMEOUT;
         service = IOService::waitForService(matching, &t); } while (!service);

with `ROOTDEVICETIMEOUT` 60 (`IOKitBSDInit.cpp:49`, `DEBUG` undefined) — the third record's deadline
is exactly `now + 60.0000 s`. **This loop cannot end**: it needs an `IOMedia` service whose `Content`
is `Apple_HFS`, and this image has no storage driver, no block device and no filesystem. The log's
last records are inside it (`0x06f7c523`) and the watchdog returns the device. The prediction's fourth
item said this in advance; what the run adds is that the *first* wait now ends, which is the whole
point of the step.

### Where the prediction was wrong, and what the errors were

1. **The shapes all held.** `finddrv_seq` 1 and 2 with `finddrv_svc = 0xc04861e0` and
   `finddrv_count = 0x00000001` (456's image could only infer 0); `rootdrv_seq=1` with
   `prov = 0xc04861e0` and `iobsd = 0xc0459400`; `reg_rm` `0xc04a3720` then `0xc04a36e0` where 456
   measured `0x00000000`; a third record at the same site with the 60-second deadline.
2. **Two constants did not, both for the same reason: a pointer and an address in this walk are
   per-build, not properties of the boot.** The resource root is `0xc04861e0` here and `0xc0589f78` in
   455/456 (the heap moved when the image did), and the sleep site is `0x80136214` — not the
   `0x80136174` this document predicted, because **the step's own code moved
   `waitForMatchingService` by 0x80**. The sentence "the site is inside `IOService.o`, which the step
   does not move" was simply false, and the durable form of the claim is *symbol + offset*:
   `waitForMatchingService+0xE0`, which is what all three records share. 456's own resolution
   (`caller − 4` = the `bl`) still works and is the reason the single call site is checkable here.
3. **The `reg_idx` prediction was a misreading of the source it quoted.** The document said "`0` if
   the snapshot already contained `IOBSD` and `0xffffffff` if it took an earlier one". Measured: 2
   keys then 5, with `"IOBSD"` at index **4**. `keys` is `copyPropertyKeys()` — the resource root's
   property *names* — and `matchPropertyTable` tests `(0xffffffffu != keys->getNextIndexOfObject(prop,
   0))` (`IOService.cpp:5116`), i.e. **the index of any match, not only 0**. So `reg_idx = 4` *is* the
   match, and the probe's own comment ("`0` means the array exists *and* holds the `IOBSD` symbol")
   was wrong in the same way. The array being *rewritten* per tail (a new `OSArray` at a new address
   each time: 2 keys at wait 2, 5 at wait 3) is the other half of the same reading.
4. **Both 30-second deadlines went unexpired.** Waits 1 and 2 ended 0.18 ms and 0.32 ms into 30-second
   deadlines, because an event source existed and fired. That is a measured statement about the timer:
   `assert_wait_deadline` does not need its timer to fire when a notifier wakes the thread, so **the
   timer is not on this path** — it stays open for waits that have no publisher, which is what wait 3
   is.
5. Safety as measured: 306521 bytes of log, 25 × `persistent_write_attempted=0x00000000`, 87 ×
   `failure_mask=0x00000000`, no `exception:`, no `panic:`, no `stub_hit=`, device back on its own.
   The driver this step links ran once, read two words and returned — no fault, no second instance.

**Next:** the root device. The frontier is no longer a wait that cannot end but the OS's own
"waiting for root device" loop, which needs an `IOMedia` service with `Content = Apple_HFS` — or the
ramdisk path, which needs `rd=`/`rootdev=` in the boot args and the memory device `IOFindBSDRoot`
builds from them (`IOKitBSDInit.cpp:400-500`). Either way it is a driver, not a link step.

## Safety, unchanged and re-stated for a step that runs new code

`fastboot boot` only, never flash; every touch through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`; the hardware watchdog is
armed by the payload before the jump and XNU never touches it, and it is the net that returns the
device (the software dead-man is disarmed before the entry by design, 308). What is new is that the
boot now *runs* a class of this project's: one instance, on the I/O config thread, whose `start`
reads two words and returns — three live writes and one `getProperty`, no allocation, no lock, no
registration of its own. If it faulted, the entry image's own `fleh_dataabt` reports it and the
watchdog returns the device; and it runs on the config thread while the boot thread sleeps in the
wait, so a fault there cannot reach the boot thread's stack.
