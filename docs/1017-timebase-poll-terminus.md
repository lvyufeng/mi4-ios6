# 1017 — the `-3` poll: terminus or bystander?

2026-10-15. Read-only rung. No build, no press, no device action. This file names a wall; it does
NOT remove it. It does NOT claim iOS 7 userspace launched.

**Question asked.** Is the `-3` (`mach_absolute_time`) poll the thing pid 1 is stuck on, or a
bystander while something else spins?

**Answer.** The `-3` poll is a **BYSTANDER**. pid 1's frontier frame is a **USER/Thumb `udf` pad in
the dyld shared cache** (`pc = 0x3b04aa54`, `lr = 0x7`), on a code path that makes **no syscall** —
it is not in the `-3` call chain and it is not dyld's `_mach_absolute_time`. The `-3` calls are
dyld's *own* phase-trace timing, all of them issued and returned **before** pid 1 entered the shared
cache. They are, however, the **witness to a separate, real 24.39 s tick-delivery blackout** during
the dyld phase — a freeze that is NOT explained by a `mach_absolute_time` byte-poll (that poll is a
leaf syscall that cannot hang) but IS consistent with a kernel-side tick-starve that the poll loop
was waiting on.

Two independent claims, kept apart:

- (A) the `-3` poll does **not** contain the frontier stall — the stall is a shared-cache `udf`;
- (B) the `-3` poll's era **contains** the 24.39 s tick blackout — a real freeze, not the poll.

---

## 0. Capture and the instrument's own rules

    CAP = out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt

Recovered 16:38 after the operator's power press; 34011 lines. Same capture read by
`records/1016a-read.txt`.

Instrument rules, read by value from source (each claim is a machine reading; the source line is
shown):

- The usvc ladder's `_pc` is the **saved user PC** and `_num` is the saved **r12**:
  `src/entry/entry_stubs.c:7346` `entry_write_kv("xnu_live_usvc_num", num)` with
  `:7349` `... "xnu_live_usvc_pc", state[STAGE90_SS_PC / 4])`. So `_pc` is where the trap returns
  to (the instruction after the `svc`) — the return-site convention 1003 §1/§6 already established.
- The ladder thins past `USVC_FULL_N`:
  `src/entry/entry_stubs.c:7313  #define USVC_FULL_N 64u`; `:7343` full 8-field window for
  `n <= 64`; `:7353` `else if ((n & (n - 1u)) == 0u)` — `_n`/`_num` only. **The ladder's last row is
  not a stop**: values 65..127 would publish only at n=128. Total user SVCs in the run ∈ [64, 127].
- The stall sampler samples the **interrupted** frame on the tick's powers-of-two ladder,
  `src/entry/entry_stubs.c:4659` `entry_write_kv("xnu_live_stall_pc", pc)` where
  `pc = frame[STAGE90_SS_PC / 4]` of the IRQ context (`src/platform/darwin13/pe_msm8974.c:523-529`,
  `entry_note_stall_sample((const uint32_t *)context)`).
- `xnu_live_tick_sum_maxd = g_msm8974_tick_max_d` = the **worst elapsed between board timer
  deliveries**, in 19.2 MHz ticks (`pe_msm8974.c:613`; producer `msm8974_tick_sample`, `:453-462`,
  `d = CNTVCT_lo - tick_arm_cntv`). It is a per-slice maximum, so once a big blackout occurs it
  persists in every later summary (`:1744` semantics, observed below).

---

## 1. The usvc ladder, last ~20 rows (tabulated)

Command (ordered read, not the grep slip the design warned about):

    python3 -I - <<'EOF'
    import re
    f='out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt'
    rows=[];cur={}
    for line in open(f,errors='replace'):
        m=re.search(r'xnu_live_usvc_(\w+)=(\S+)',line)
        if not m: continue
        k,v=m.group(1),m.group(2)
        if k=='n':
            if cur:rows.append(cur)
            cur={'n':v}
        else:cur[k]=v
    if cur:rows.append(cur)
    def si(x):
        x=int(x,16);return x-(1<<32) if x>=1<<31 else x
    for r in rows[-20:]: print(r['n'],si(r['num']),r['pc'],r['lr'])
    EOF

`n` below is **decimal** (the ladder's `_n` is `0x0000002d`…`0x00000040`, i.e. 45…64):

| n (dec) | num (signed) | pc | lr |
|---|---|---|---|
| 45 | -3 | 0x2beef328 | 0x2bedec9f |
| 46 | -3 | 0x2beef328 | 0x2beded75 |
| 47 | -3 | 0x2beef328 | 0x2bedec9f |
| 48 | -3 | 0x2beef328 | 0x2beded75 |
| 49..64 | -3 | 0x2beef328 | 0x2bedec9f |

(46 of the 64 rows are `-3`; the two `lr=0x2beded75` rows are n=46 and n=48 only.)

(`0xfffffffd` decoded as signed 32-bit = -3, i.e. `mach_absolute_time`.) All 64 rows carry
`pid=1`, `thr=0xc0bfa000`, `r0=0`, `cpsr=0x60000010` (mode USR, ARM — the `6` is the condition
flag nibble, not mode). `_lr` varies between exactly two values, `0x2bedec9f` (30 rows) and
`0x2beded75` (2 rows).

**Slide (by value).** 1016-A's dyld slide is **0xd4000**:

    xnu_live_entrypt_entry = 0x2bed5028   (capture :22195)
    __dyld_start (link)    = 0x2be01028   (/tmp/dyld_syms_full.txt)
    0x2bed5028 - 0x2be01028 = 0xd4000

    pc link = 0x2beef328 - 0xd4000 = 0x2be1b328
    lr link = 0x2bedec9f - 0xd4000 = 0x2be0ac9f
    lr link = 0x2beded75 - 0xd4000 = 0x2be0ad75

Corroboration, not argument (the slide is already *derived*, nothing is being asked): the same
delta drives 1003's numbers — 1003 slide 0x7c000, `0x2be97328 - 0x7c000 = 0x2be1b328` and
`0x2be85a9f - 0x7c000 = 0x2be0ac9f`, the SAME two links. The design's "pc-slide = 0x2be97328"
lump-sum was comparing the two runs' *raw* pcs across their different slides.

---

## 2. Disassembly — the callee is `_mach_absolute_time`

dyld binary: `/usr/lib/dyld` from the HFSX rootfs, `196576` bytes, ARMv7, dyld-324.1. Extracted by a
prior rung; verified by Mach-O header (this re-run):

    file /tmp/dyld_ios7
      -> Mach-O armv7 dynamic linker
    xxd -l 32 /tmp/dyld_ios7
      -> cefa edfe ...              (filetype 7 = MH_DYLINKER)
    LC_SEGMENT __TEXT: vmaddr=0x2be00000 fileoff=0 filesize=0x21000   -> link == file offset

    arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x2be00000 \
        --start-address=0x2be1b2c0 --stop-address=0x2be1b32c /tmp/usr_dyld.bin

    2be1b2c0:  movw  ip, #16520          ; ip = 0xFFFF4088
    2be1b2c4:  movt  ip, #65535
    2be1b2c8:  ldrb  r0, [ip, #8]        ; load commpage flag byte @ 0xFFFF4090
    2be1b2cc:  cmp   r0, #0
    2be1b2d0:  beq   0x2be1b320          ; flag == 0  -> SLOW PATH
       ... (flag != 0: VCT-based fast path, returns via pop {r4,r5,r7,pc} @ 0x2be1b31c)
    2be1b320:  mvn   ip, #2              ; ip = 0xFFFFFFFD = -3
    2be1b324:  svc   #0x80
    2be1b328:  bx    lr                  ; <-- pc 0x2beef328 lands here (link 0x2be1b328)

Symbol: `dyld_syms_full.txt:647  2be1b2c0 l F __TEXT,__text _mach_absolute_time`. `0x2be1b328` is
`_mach_absolute_time + 0x68`. **Every** recorded `-3` landing is on the SLOW PATH: the commpage flag
byte at `0xFFFF4090` reads 0, so the VCT fast path is never taken. That is the timebase-edge
condition 1003 §6 named and the commpage work (`d13-commpage-mach-absolute-time`) tracks; it is
NOT this rung's subject.

---

## 3. The caller chain (`lr` sites)

All 46 `-3` rows were mapped to symbols with the 0xd4000 slide:

    link = lr_raw - 0xd4000 ; bracket against /tmp/dyld_syms_full.txt

| lr_raw | link | count | containing symbol |
|---|---|---|---|
| 0x2bedec9f | 0x2be0ac9f | 30 | `ImageLoader::recursiveInitialization(LinkContext const&, unsigned, InitializerTimingList&)` |
| 0x2beded75 | 0x2be0ad75 | 2 | `ImageLoader::recursiveInitialization(...)` |
| 0x2bedda9f..0x2beddba3 | 0x2be09a9f..0x2be09ba3 | 8 | `ImageLoader::link(LinkContext const&, bool, bool, bool, RPathChain const&)` |
| 0x2bede711, 0x2bedea2d | 0x2be0a711, 0x2be0aa2d | 2 | `ImageLoader::weakBind(LinkContext const&)` |
| 0x2bedeb13, 0x2bedeb1b | 0x2be0ab13, 0x2be0ab1b | 2 | `ImageLoader::runInitializers(LinkContext const&, InitializerTimingList&)` |
| 0x2beef2ab, 0x2beef2b1 | 0x2be1b2ab, 0x2be1b2b1 | 2 | `_mach_init_doit` |

Call site for the dominant 30 (the return is 2 bytes after the `blx` because the return value is a
64-bit pair, `r0:r1`):

    arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x2be00000 \
        --start-address=0x2be0ab6c --stop-address=0x2be0acc0 /tmp/usr_dyld.bin

    2be0ac92:  ldr   r0, [sp, #52]        ; the InitializerTimingList cursor
    2be0ac94:  str   r0, [sp, #56]
    2be0ac96:  movs  r0, #7
    2be0ac98:  str   r0, [sp, #64]
    2be0ac9a:  blx   0x2be1b2c0          ; <-- _mach_absolute_time
    2be0ac9e:  str   r0, [sp, #16]       ; r0:r1 = 64-bit start time
    2be0aca0:  str   r1, [sp, #12]
    2be0aca2:  ldr   r0, [sp, #48]
    2be0aca4:  movs  r3, #45
    2be0aca6:  strb.w r3, [r0, #44]      ; state = dyld_image_initializing, etc.
    2be0acb8:  blx   r2                  ; <-- run the initializer (ImageLoader vtable call)
    ...
    2be0ad70:  blx   0x2be1b2c0          ; <-- the second call: STOP time (lr 0x2be0ad75)

**They are `mach_absolute_time` phase-trace pairs.** Every `-3` site is the well-known dyld idiom:
read the timebase before and after a phase (link, weakBind, runInitializers, each initializer) to
fill an `InitializerTimingList` and dyld's `-printStatistics` output. The two `lr` values in
`recursiveInitialization` are the same pair (start @ 0x2be0ac9a, stop @ 0x2be0ad70), 0x6 bytes apart
in return-site, i.e. the function brackets one initializer call between them.

**The call at 0x2be0ac9a is not in a loop.** The function's only backward branch
(`0x2be0ad46: bcc.n 0x2be0acec`) closes a loop whose body is **after** the call; the call sites
themselves are straight-line, executed once per `recursiveInitialization` entry. So the `-3` wall is
**discrete timing readings**, not a hot poll spinning in this frame. Between them pid 1 is elsewhere
(inside initializers, or in the kernel).

Decisive structural fact: **none of the 46 `-3` sites is in the shared cache.** All six callers are
in dyld's own `__TEXT` (`ImageLoader` C++). The frontier, §5, is in the shared cache. They are
different images, reached at different times.

---

## 4. The `_mach_init_doit` `-3` rows (n=1,2)

The first two ladder rows are the run's first two SVCs, in `_mach_init_doit` (`lr` link 0x2be1b2ab /
0x2be1b2b1), i.e. dyld's own Mach bootstrap init, before ever calling `dyld`'s main. Set aside:
they are the same `mach_absolute_time` primitive, early in the run, and not near the frontier.

---

## 5. The REAL frontier — pid 1 spins in the shared cache, in USER mode

The stall sampler's last five samples (n=0x0b..0x0f, capture :33975-34009) are byte-identical:

    xnu_live_stall_pc = 0x3b04aa54
    xnu_live_stall_cpsr = 0x20000030   ->  cpsr&0x1f = 0x10 = USR ; bit5 = 1 = Thumb
    xnu_live_stall_sp = 0x2bd2b400
    xnu_live_stall_lr = 0x00000007

Per the sampler's own reading rule (`entry_stubs.c:4607-4623`): *"the sampled pcs are all the user pc
AND IDENTICAL => pid 1 is wedged at one user instruction."* Five consecutive samples on a doubling
ladder ⇒ **a tight user-space loop**.

`0x3b04aa54` is in **mapping 0 of the dyld shared cache**:

    IOS7LAB cache mapping 0 va=2ee10000 size=0c266000 offset=00000000   (capture :4105)
    [0x2ee10000, 0x3b076000) contains 0x3b04aa54   -> TRUE
    file offset = 0x3b04aa54 - 0x2ee10000 = 0xc23aa54   (cache mapping 0 offset=0)

Disassembly of the actual bytes (`/tmp/dyld-extract/dyld_shared_cache_armv7`):

    python3 -I -c "open('/tmp/sc_pc.bin','wb').write(open('/tmp/dyld-extract/dyld_shared_cache_armv7','rb').read()[0xc23aa54-64:0xc23aa54+192])"
    arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x3b04aa14 /tmp/sc_pc.bin

    3b04aa4c:  mov   sp, r4
    3b04aa4e:  ldmia.w sp!, {r8, sl, fp}
    3b04aa52:  pop   {r4, r5, r6, r7, pc}     ; <-- a function's return path
    3b04aa54:  udf   #254                      ; <-- 0xdefe: the function-align PAD, decoded as `udf`
    3b04aa56:  nop
    3b04aa58:  movw  r0, #17856 ...

**`0x3b04aa54` is a DELIBERATE trap, not an alignment pad.** ⚠ **CORRECTION (2026-10-15, from
`docs/1017-shared-cache-naming.md` §2-3 and confirmed by the adjudicator's own branch decode below).**
An earlier wording of this section called `0x3b04aa54` "the 2-byte alignment pad immediately after a
`pop {..., pc}`". That is **WRONG**: a decoder reading only the bytes around the `pop` cannot see that
`0x3823aa54` is a **branch target**. Decoding the two branches above it (Thumb, by value):

```
$ python3 -I ...   # Thumb B/B<cond> decode on /tmp/dyld-extract/dyld_shared_cache_armv7
0x3823a9b0  bne  0x3823aa54     ; the _mach_vm_map FAILURE branch
0x3823aa26  b    0x3823aa54     ; the second _mach_vm_map FAILURE branch
0x3823aa52  pop  {r4, r5, r6, r7, pc}   ; the SUCCESS return, which skips 0x3823aa54
0x3823aa54  udf  #254           ; <-- the SAMPLED PC
```

**A pure alignment pad is never a branch target.** `udf #254` here is the compiler's
`__builtin_trap()` fallthrough that BOTH `_mach_vm_map == 0` failure checks branch to — inside
`__os_alloc_slow` of `/usr/lib/system/libsystem_platform.dylib` (symbol `0x3823a94c`; the naming doc
resolves it from the cache's own `localSymbolsInfo`). So the sampled pc is not "a return path"; it is
**the `os_alloc` slow path having taken the `_mach_vm_map` failure branch**. What stays true from the
paragraph that follows is only the *region* fact (USER/Thumb code in the shared cache, a different
image from every `-3` site); the *nature* of the instruction is the trap, and `# UNMEASURED` remains
the mechanism by which the pc stays put across samples (the undefined-instruction handler returning to
it, or an outer retry re-entering the failure path).

**What this is NOT:**
- not a syscall (`cpsr` is USR, not the `svc` vector);
- not dyld's `_mach_absolute_time` (`0x2be1b2c0` link is dyld `__TEXT`; at slide 0xd4000 its
  runtime address is `0x2beef2c0`, not `0x3b04aa54`, and a different image);
- not any address in the `-3` call chain (§3).

So the `-3` poll is a **bystander** to the stall. The stall's terminus is shared-cache user code.

---

## 6. Cross-check — does a `mach_absolute_time` poll explain the 24.4 s?

`xnu_live_tick_sum_maxd = 0x1be9f52b` in the SAME capture (:30909, and persistent through the last
summary :32765). At the capture's own timebase frequency:

    [RTCLOCK] Frequency = 19.2 MHz    (capture :4010, per 1003/1015 notes)
    0x1be9f52b = 468317483 ; 468317483 / 19.2e6 = 24.3915 s

This is the "worst gap between board timer deliveries" (source §0). **A `mach_absolute_time` poll
cannot explain this, and the reason is in the code:**

- dyld's poll of `mach_absolute_time` is a **leaf syscall**: `svc #0x80` with `-3` dispatches to
  `ios7lab_timebase` -> `blx _mach_absolute_time` -> `swi_exit64` (`traps_lo.s:285-286`). It takes no
  lock and sleeps, and the kernel's `mach_absolute_time` (`rtclock` fraction from `CNTVCT`) advances
  on its own — the capture itself records `[RTCLOCK] Frequency = 19.2 MHz`, i.e.
  `rtclock_sec_divisor != 0`. So a *polling loop* of this syscall returns promptly every time; it
  cannot itself account for a 24.39 s delivery gap.
- Therefore the ~24.4 s blackout is **separate**: it is the **board timer line not being serviced for
  24.39 s**, i.e. interrupts masked / the tick-starve the 1005/1007/1008/1009 line already localized
  (`records/1005-cadence-rung.md..1009-abt-pc-rung.md`). During that window `rtclock_intr` does not
  run, so `mach_absolute_time`'s *tick* (as opposed to raw `CNTVCT`) does not advance — which is
  exactly the condition a `mach_absolute_time`/`wait_timer` **consumer** would be blocked on.

**When** the blackout happened: the last summary before the jump is `0x04ba91b4` = 79.3 ms at
`:29748`, the first with `0x1be9f52b` at `:30909` — and that read sits right beside the
`recursiveInitialization` `-3` wall (`-3 lr 0x2bedec9f` is at :33797..; `xnu_live_usvc_r0` at
:33797-33805 are in the same block) and just before the `IOS7SHREG` shared-cache mapping block
(:3977-4113). So **the 24.39 s tick blackout is inside the dyld-run-initializers phase, before pid 1
entered the shared cache.**

⇒ Read together: pid 1 issued the dyld phase-trace `mach_absolute_time` calls and, in that phase,
**endured a 24.39 s gap in the tick** (a real freeze). It then progressed — the stall sampler later
catches it in USER mode inside the shared cache. That progression means the `-3` wall is **not the
final resting place**: pid 1 moved past dyld, into the shared cache, where it now spins. The one
thing the `-3` wall is a witness to is the tick blackout, and that blackout is the timer line, not
the poll.

---

## 7. Verdict

| claim | reading | status |
|---|---|---|
| `-3` poll is what pid 1 is stuck on | stall pc is `0x3b04aa54` in the shared cache, USR/Thumb, `lr=0x7`; no `-3` site is in the shared cache | **FALSE** |
| `-3` poll is a bystander while something else spins | the frontier is shared-cache user code (`__os_alloc_slow`'s `_mach_vm_map`-failure `udf`, a different image and a different time from every `-3` site) | **TRUE** |
| the `-3` poll "explains" the 24.4 s freeze | the poll is a lock-free leaf syscall that returns promptly; the gap is a *timer-delivery* gap (`_maxd` = 24.3915 s / 19.2 MHz) | **FALSE (separate)** |
| the 24.4 s freeze is real and located | worst timer-delivery gap; bounded between summary `0x04ba91b4` (:29748) and `0x1be9f52b` (:30909), inside the dyld-run-initializers phase | **TRUE** |

**Next-wall handoff.** The named wall is: **pid 1 is wedged in USER mode at `0x3b04aa54` (mapping 0 of
the dyld shared cache, `[0x2ee10000,0x3b076000)`); it is not in a `mach_absolute_time` syscall.** The
instruction there is a deliberate `udf #254` — the `_mach_vm_map`-failure trap of `__os_alloc_slow`
in `/usr/lib/system/libsystem_platform.dylib` (`docs/1017-shared-cache-naming.md` §2-3). The shared
cache was mapped by the 998/997/996 SHREG wire (`map_file kr=0`, `map_slide ret=0`, capture
:4109-4113) and its code is executing. **The next rung (1018) is the ALLOCATOR's `_mach_vm_map`
question, not a "which symbol" one**: witness that call's return value and map arguments at the two
`blx` sites (`0x3823a6e4`-family pic stubs) to learn why pid 1's `os_alloc` ring mapping is refused —
the SHREG/`map_file` territory of 998, now reached from inside pid 1's own allocator. Separately, the
24.39 s tick blackout during dyld init remains an open, INDEPENDENT defect.

**This rung names a wall; it does not remove it. iOS 7 userspace is NOT claimed launched.**

---

## 8. Exact commands

    # ladder (ordered read)
    python3 -I - <<'EOF'
    import re
    f='out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt'
    rows=[];cur={}
    for line in open(f,errors='replace'):
        m=re.search(r'xnu_live_usvc_(\w+)=(\S+)',line)
        if not m: continue
        k,v=m.group(1),m.group(2)
        if k=='n':
            if cur:rows.append(cur)
            cur={'n':v}
        else:cur[k]=v
    if cur:rows.append(cur)
    def si(x):
        x=int(x,16);return x-(1<<32) if x>=1<<31 else x
    for r in rows: print(r['n'],si(r['num']),r['pc'],r['lr'])
    EOF

    # dyld identity / segments
    file /tmp/dyld_ios7 ; xxd -l 64 /tmp/dyld_ios7
    python3 -I  # parse LC_SEGMENT: __TEXT vmaddr=0x2be00000 fileoff=0

    # symbols
    grep -in 'mach_absolute_time' /tmp/dyld_syms_full.txt
    awk '{a=strtonum("0x"$1); if(a>=0x2be0da28&&a<=0x2be0e400) print}' /tmp/dyld_syms_full.txt

    # disassembly (ARM callee, Thumb caller/SharedCache)
    arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x2be00000 \
        --start-address=0x2be1b2c0 --stop-address=0x2be1b32c /tmp/usr_dyld.bin
    arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x2be00000 \
        --start-address=0x2be0ab6c --stop-address=0x2be0ad80 /tmp/usr_dyld.bin
    python3 -I -c "open('/tmp/sc_pc.bin','wb').write(open('/tmp/dyld-extract/dyld_shared_cache_armv7','rb').read()[0xc23aa54-64:0xc23aa54+192])"
    arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x3b04aa14 /tmp/sc_pc.bin

    # capture reads
    grep -n 'xnu_live_tick_sum_maxd' "$CAP"
    grep -n 'xnu_live_stall_' "$CAP" | tail -40
    grep -n 'cache mapping\|IOS7SHREG\|entrypt_entry\|RTCLOCK' "$CAP"

**# UNMEASURED** (not claimed): the symbol name of the shared-cache function at `0x3b04aa54` (no
cache symbol table extracted here); whether the loop at `0x3b04aa54` issues any syscall at all
(inferred "none" from the frame's `pc`/`lr`, not observed); the exact wall-clock mapping of the
`-3` wall's role in the 24.39 s gap (located by record order, not timestamped).