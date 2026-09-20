# Experiment 469 — the pool was built from the previous configuration's device table

**The run stopped on a name that a *fixed* image would have defined, and the reason was an ordering
defect in the build script rather than anything in the kernel.** `tools/build_xnu_arm_kernel.sh` wrote
`out/device_table.txt` (via `xnu_config/device_table.py --write`) — and the *manifest*, which is what
decides which files are compiled, is generated **from that table** by `xnu_config/list_sources.py`. The
table block sat about forty lines *below* the manifest block, so the manifest was always generated from
the table the **previous** build had left behind.

That is experiment 440's defect in a second place. 440's version was the table itself not being
generated; this one is the table being generated and then read by nothing. The comment above the block
already stated the property in words — "a required input that a generated input determines has to be
generated too, or the report is about two different configurations" — and the order contradicted it.

**What it cost, measured.** Experiment 468's configuration selects three `pseudo-device` lines the
previous one did not, one of them `pseudo-device vndevice 4 init vndevice_init`
(`config/MASTER:436`). The table therefore gained `vndevice 1`, and the manifest written *from the old
table* omitted two files: `bsd/dev/vn/vn.c` and `bsd/dev/vn/shadow.c`. Meanwhile `pseudo_inits[]` is
generated from the **configuration** (`tools/gen_pseudo_inits.py` over `xnu_config/expand.sh`), which was
current, so the array named `vndevice_init` while no object in the pool defined it. The stub generator
supplied a function stand-in, and `bsd_autoconf`'s walk entered it:

    xnu_entry_stub_hit_count    = 0x00000001
    xnu_entry_stub_caller_v     = 0x80043ae4      = bsd_init + 0x354
    xnu_live_stub_hit_name_w0   = 0x65646e76      "vnde"
    xnu_live_stub_hit_name_w1   = 0x65636976      "vice"  -> vndevice_init

**It is the first stand-in the boot has entered in four hundred experiments**, and that is the finding
rather than an incidental: every earlier stop was a *missing* symbol on a path the walk had not yet
reached, while this one was a symbol the walk had always been *supposed* to have.

**The measurement that separates the two configurations**, and the reason the defect is provable rather
than plausible: a fresh `xnu_config/list_sources.py STAGE90_XNU --write` selects **733** files where the
stale manifest held 731 — `bsd/dev/vn/shadow.c` at line 20 and `bsd/dev/vn/vn.c` at line 21 of the
regenerated manifest. `nm` on the recompiled `out/xnu_kernel_obj/bsd_dev_vn_vn.o` then reports
`T vndevice_init`, and the name is gone from the entry link's undefined set (27 → 26).

**Fixed by moving the block, and by a check that would have caught it in the entry build.** The device
table is now written before the manifest is generated (`tools/build_xnu_arm_kernel.sh:144-149`, above
`list_sources.py` at `:159`). And `build_entry.sh` now asks the question the two generators could not ask
each other: **every name `stage90_pseudo_inits.o` references must be defined by this link's own objects.**
`nm -u` on that object is the array's `ps_func` targets *by construction* — the `.data` relocations
`R_ARM_ABS32` at `+4, +0xc, +0x14 ...` are exactly what `gen_pseudo_inits.py` wrote — so the check reads
the pointers the kernel will call rather than re-deriving names from the configuration, which is the thing
that had already drifted. The negative control is the real defect: adding `vndevice_init` to a copy of
the undefined list makes the check name it.

**Measured:** device run exit 0, `No errors detected`, device back on Android by itself; log
`/tmp/cancro-469-last_kmsg.txt`, 465579 bytes. The OS console now reads `Added memory device md0/rmd0`,
`BSD root: md0, major 2, minor 0` and `load_init_program`'s first line — thirteen lines past `mbinit:
done`, where 468's regression had stopped at it.
