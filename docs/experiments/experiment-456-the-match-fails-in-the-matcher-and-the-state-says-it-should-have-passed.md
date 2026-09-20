# Experiment 456 — the match fails in the matcher, the state says it should have passed, and the two readings bracket the registration

**Status: built, gated, run on hardware.** 455 could not see the query (`--wrap` cannot reach a call
issued from the object that defines the symbol) but it did see the one wrapper on this path whose call
site the linker *can* reach — `IORecursiveLockSleepDeadline`, entered by `waitForMatchingService` from
another object — so 456 takes the reading **inside the wait that never returns** and answers the
question 455 posed: `IOResources::matchPropertyTable`'s third branch is where the match dies, because
`copyProperty(gIOResources, gIOResourceMatchedKey)` is **NULL** — the resource root has no
`IOResourceMatched` array — and it is NULL at both waits, 10.7 ms apart. The state word says the guard
*before* the matcher is not the problem: at the second wait `__state[0] = 0x1e`, i.e. registered (0x2),
**matched (0x4)**, first-publish (0x8) and first-match (0x10) all set — so `copyExistingServices` did
reach `service->matchPassive(...)`, and the verdict was false. And the two readings turn out to bracket
something 454 could only see the shadow of: **`__state[0]` is `0x00000000` at the first wait and
`0x0000001e` 205152 ticks later**, i.e. the resource root is *not yet registered* when the IORTC query
runs and is registered-and-matched by the time the IOBSD query does — because `publishResource` only
*queues* the work on XNU's I/O config thread. Run: 304425 bytes, **305 live records**, 33 blocks with 8
returns, 6 sleeps, 2 IOKit waits, 3 dictionary records, 2 registry readings, 25 x
`persistent_write_attempted=0x00000000`, 87 x `failure_mask=0x00000000`, no
`exception:`/`panic:`/`stub_hit=`, device back on its own. `.text` 5016032 -> **5016704**, entry bin
**5224980** (unchanged), `.bss` `0x804fba40 .. 0x8054cd98` (unchanged: the seven new slots landed in the
existing fill), payload `1518e329...` 8245248 bytes.

## What the step adds

One instrument, `entry_registry_probe(seq, site)` in `entry_stubs.c`, called at the end of
`entry_note_iolock` — i.e. once per entry into either IOKit deadline sleep, which on this boot is
exactly the two waits 454 named. It reads three things, and they are the three the source's branch
tests:

| record | the call | what it decides |
| --- | --- | --- |
| `reg_state0`, `reg_state1` | `getResourceService()` then `w[9]`/`w[10]` | `copyExistingServices`' guards: `(inState == (__state[0] & inState))` with `inState = kIOServiceMatchedState = 0x4`, `0 == (__state[0] & kIOServiceInactiveState)`, and the `kIOServiceModuleStallState` bit in `__state[1]` that suppresses `copyNotifiers` |
| `reg_rm` | `copyProperty(gIOResources, gIOResourceMatchedKey)` — the matcher's own call (`IOService.cpp:5109`) | `IOResources::matchPropertyTable`'s third branch: NULL means there is no array to search |
| `reg_count`, `reg_idx` | `OSArray::getCount()`, `getNextIndexOfObject(gIOBSDKey, 0)` | the array's size and whether the `"IOBSD"` symbol is in it |

Three properties make this call safe here rather than merely convenient, and none of them is a guess.
`IORegistryEntry::copyProperty` takes `IORecursiveLockLock(reserved->fLock)`
(`IORegistryEntry.cpp:119`) — a *recursive* lock — and the matcher calls the same function from the
same context this probe runs in (`copyExistingServices` under `gNotificationLock`), so the probe adds
no lock order the boot does not already take; `copyProperty(const OSSymbol *)` is called directly
rather than through a vtable because nothing between `IOResources` and `IORegistryEntry` overrides it
(the image defines one such symbol); and the object it returns is retained, so it is released with
`OSObject::release`, the only `release` in the image and therefore not the wrong override.

The readings are **live only**, and that is a deliberate consequence of 455's own run: a boot that
hangs at the frontier never reaches `entry_epilogue`, so a key written into the final buffer is a key
no run this instrument exists for will ever print. One new build check came with it, because a
mangled name typed wrong is *not* a link error in this image — the generator invents a stub and the
probe would stop the boot at itself (455's doubled underscore, twice). So the five names the probe
uses are checked against the linked image (defined, and of function/storage type) and against
`xnu_arm_entry_undef.txt`:

    xnu_entry_456: the registry probe's five names are real symbols in the image and none of them is in the undefined list

## The prediction, written before the run, and what came out

Predicted: `reg_calls = 2` with the site at `waitForMatchingService+0xe0`; bit 0x4 **clear** in
`reg_state0` (the guard failing before the matcher) with the stall bit clear; and `reg_rm = 0`. The
first part was wrong, the second right, and the miss is the more informative half:

    4011  reg_seq=1  reg_site=0x80136174  reg_ptr=0xc0589f78
          reg_state0=0x00000000  reg_state1=0x80000001  reg_rm=0  reg_count=0  reg_idx=0xffffffff
    4132  reg_seq=2  reg_site=0x80136174  reg_ptr=0xc0589f78
          reg_state0=0x0000001e  reg_state1=0x84000002  reg_rm=0  reg_count=0  reg_idx=0xffffffff

Read against the constants (`kIOServiceInactiveState` 0x1, `kIOServiceRegisteredState` 0x2,
`kIOServiceMatchedState` 0x4, `kIOServiceFirstPublishState` 0x8, `kIOServiceFirstMatchState` 0x10;
`kIOServiceNeedConfigState` 0x80000000, `kIOServiceConfigState` 0x04000000 in `__state[1]`,
`IOServicePrivate.h:49-68`):

  * **At the second wait the guard passes and the matcher is reached.** `0x1e` = registered | matched |
    first-publish | first-match, inactive clear, and the stall bit clear in `0x84000002`. So
    `copyExistingServices`' fast path ran `service->matchPassive(matching, options)` on the resource
    root, and `IOResources::matchPropertyTable` answered false — its third branch, because the array
    `copyProperty(gIOResourceMatchedKey)` is NULL. **The frontier is the matcher's third branch, and
    what it needs is a property that this boot never sets.**
  * **At the first wait nothing of the sort was true**: `__state[0] = 0x00000000` — not registered, not
    matched — so for the IORTC query the guard fails *before* the matcher, which is why 454 saw that
    wait sleep even though `resourceMatching("IORTC")` ends at `matchPropertyTable`'s **first** branch
    (`ok = (0 != getProperty(str))`, `IOService.cpp:5095`), a property published seven records
    earlier. The prediction had the mechanism right for the wrong wait.
  * **The two readings are 205152 ticks (10.7 ms) apart and bracket the registration.** Nothing else in
    the run changes between them, and `reg_ptr` is identical (`0xc0589f78`), so this is the resource
    root being registered between the first wait and the second — by XNU's **I/O config thread**,
    because `publishResource` only queues the work: `gIOResources->setProperty(key, value)` then
    `gIOResources->registerService()`, whose `doServiceMatch` runs on the thread 449 found
    (`_IOConfigThread::main`, created by `_IOServiceJob::pingConfig`). That also closes 454's positive
    control from the other side: the IORTC wait was woken by a notification that could only have been
    delivered *when that thread did the registration*.
  * **`0x4` set with `reg_rm = 0` is one measurement with a single reading.** The matched bit has
    exactly one writer in the tree — `copyNotifiers(gIOMatchedNotification, kIOServiceMatchedState,
    0xffffffff)` (`IOService.cpp:3754`) — and in `doServiceMatch`'s tail the *same* guard that lets
    that call through also guards `if (resourceKeys) setProperty(gIOResourceMatchedKey, resourceKeys)`
    (`:3751`). So the tail ran, and `resourceKeys` was NULL when it did: the branch that sets it
    (`:3729`, `this == gIOResources` inside `if (keepGuessing && matches->getCount() &&
    (kIOReturnSuccess == getResources()))`) did **not** run. `publishResource` never adds to that
    array anyway — it is `setProperty` and nothing else (`:3532`).

## What this means, stated as what it is

The boot is not stopped by a register, an instrument, or a missing timer *at this wait*: it is stopped
by the resource root having no `IOResourceMatched` array, and that array is written only when the
catalogue hands `doServiceMatch` a non-empty candidate set for the resource root. In a kernel with no
kexts and no filesystem driver, an empty set is the expected answer — which makes this the **first
measurement in this project that is about the kernel's drivers rather than about the kernel's
bring-up**: `IOFindBSDRoot` is waiting for a driver that publishes what it asks for, in an image that
has none.

Two consequences, and they are the next two work items rather than one:

  1. **The timer is still owed, and it is what makes this wait *end*.** The 30 s deadline is armed on
     the thread's own timer, delivered by a timer interrupt this boot has none of, so without it the
     wait can only end by a wakeup that (as this step shows) will never come. With a working
     `ml_init_timebase` + MSM8974 `tbd_ops_t` over the GPT at `0xf9020000` (19.2 MHz — 454 measured
     that from the device — IRQ 19) the deadline expires, `waitForMatchingService` returns NULL,
     `IOFindBSDRoot` prints its `!BSD` and runs on, and the frontier moves to whatever the mount path
     does next. The timer must land inside the watchdog window (25 s today) to be usable at all, or the
     window has to be widened with the recovery nets re-proved at the new value.
  2. **The registry needs something to match.** Until the resource root's `IOResourceMatched` array
     exists, `IOFindBSDRoot`'s first wait is a 30 s stall on every attempt, and `mountAttempts++` loops
     with a 5 s busy-wait between attempts. So the mount path is a dead end without a driver of some
     kind, and the next experiment that is *not* the timer has to name which ingredient is missing:
     what `gIOCatalogue->findDrivers(gIOResources)` returns on this boot (wrappable — `findDrivers` is
     called from another object), and whether `getResources()` returned success on the branch that
     would have set `resourceKeys`.

Safety, unchanged and re-measured on every run: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned to
Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:` line. The probe is read-only with respect to the boot: it
reads two words, calls the property accessor the matcher itself calls, and releases what it takes.
