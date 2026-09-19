# Experiment 410 — `bsd/kern/kern_subr.c`: the object that empties `procinit`'s frame, and the frontier moves to `tty_init`

**Step:** one object linked — `bsd_kern_kern_subr.o`, the pool's only definer of `hashinit`
(`bsd/kern/kern_subr.c:307`) and therefore of 409's stop.

**Prediction:** `stub_hit=tty_init`, caller key `0x8003ABBC`. **Measured:** exactly that,
`abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`.

## The step's effect

    6 resolved (6 function, 0 storage) / 2 added (2 function, 0 storage)
      resolved  hashinit, uio_duplicate, uio_free, uio_getiov, uio_resid, uiomove
      added     copywithin, subyte
    766 -> 762 stub names, 647 -> 643 function, 119 -> 119 storage

## The reading, and the step 409 existed to make safe

`hashinit`'s body is 0xC8 bytes and calls exactly two names — `__MALLOC` (real since 377) and `panic`
(real) — with the rest a `LIST_INIT` loop. So nothing stops inside it, and the frame below (`procinit`)
empties: its four `hashinit` calls all run for real, its two `LIST_INIT`s are inline stores, and it returns.

**And the panic it is one statement away from does not fire, because 409 already cured the zero.**
`hashinit`'s first statement is `if (elements <= 0) panic("hashinit: bad cnt")`, and 409 linked
`bsd_conf_param.o`, so `maxproc` reads **1000** rather than a stand-in's zero: `elements = 1000 / 4 = 250`.
This run is the measurement of that cure — not of a stub, and not of a panic.

The stop is then the caller's next statement: the `bl 80194ae4 <tty_init>` at `0x8003ABB8`,
`bsd_init + 0x1C8`, whose key `0x8003ABBC` was read from the built image before the device was touched.
`tty_init` is record 537 of 766 and is unchanged by this link.

**Falsifiers, named in advance and all silent:** the `panic("hashinit: bad cnt")`; a stop inside `hashinit`;
a stop on `copywithin` or `subyte` (this step's two added names, neither called from `procinit` or from
`bsd_init`'s line); a stop at `procinit` again, which would mean this object was not the definer it claims.

## Layout

    text size    1824288 (0x1BD4A0)    — 409's 1819776 + 0x4510
    image bytes  1944328 (0x1DAB08)    — 409's 1944256 + 0x48
    bss          0x801DAB40 .. 0x80217CD8 (250264 bytes)
    layout       args +2199552 (0x80219000), topOfKernelData +4194304, tree +6291456, window 8388608
    headroom     1999656 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 ea106e03c99aa70b96a3a52a0c103a65eeb21d4a809f973de2c6a83a7a7ac627

Run markers: `image_bytes=0x001DAB08`, `bss_start=0x801DAB40`, `bss_end=0x80217CD8`, `args_pa=0x80219000`.

## Where the frontier is now

`tty_init` — the third of the three stubs `bsd_init` calls in a row (`kauth_init` 407, `procinit` 409,
`tty_init` here). Behind it the authorization, credential and process subsystems are all real, and
`bsd_init`'s own statement list is what the walk is now walking, one stub per object.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts. 25 × `persistent_write_attempted=0x00000000`,
87 × `failure_mask=0x00000000`, `abort_entries=0`, `checks=5` / `failures=0`, no `exception:`, no `panic`;
301620 bytes, ending `No errors detected`. Device back on Android on its own and confirmed there.
