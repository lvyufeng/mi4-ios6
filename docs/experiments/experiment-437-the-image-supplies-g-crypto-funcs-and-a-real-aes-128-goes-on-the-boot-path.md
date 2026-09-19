# Experiment 437 — `g_crypto_funcs`: the image supplies the kernel's crypto dispatch table, a real AES-128 goes on the boot path, and `bsd_init` is down to **one** stub

**Step:** two new files and two new link inputs.

- `stages/stage90/xnu_supply/stage90_aes.c` + `stage90_aes.h` — AES-128 per FIPS-197 (key expansion,
  the forward and inverse ciphers) and SP 800-38A CBC. Plain C, no tree headers at all, compiled into
  the entry image by `build_entry.sh` **and** natively by the same script so its known-answer tests
  run before the object is linked.
- `stages/stage90/xnu_supply/stage90_crypto_functions.c` — `struct crypto_functions` from
  `<libkern/crypto/register_crypto.h>`, whose AES-CBC entries are the real thing above and whose
  other **45 members are self-naming stand-ins**, installed from an `.init_array` constructor.
- `tools/build_xnu_arm_kernel.sh`: `stage90_crypto_functions.c` joins the platform block's
  out-of-manifest list (now two files, so the summary line says "tables this image supplies").
- `build_entry.sh`: `STAGE90_AES_OBJ` built and the host KAT run, `STAGE90_CRYPTO_FUNCTIONS_OBJ`
  required and linked, and the prediction block below.

## Why this step exists: 436's stop was not a symbol

436 linked the whole kernel and the run ended like this:

    exception: data abort
    xnu_entry_stub_caller_v      = 0x00000000        <- no stub was reached at all
    xnu_entry_abort_first_pc     = 0x80409DB8        aes_encrypt_key128 + 0x18
    xnu_entry_abort_first_insn   = 0xE590603C        ldr r6, [r0, #0x3c]
    xnu_entry_abort_first_dfar   = 0x0000003C        r0 was 0

`r0` came from `0x8053381C`, `g_crypto_funcs`: a real `.bss` global, defined `B` by
`libkern/crypto/register_crypto.o`, whose only writer is `register_crypto_functions()` and whose only
caller in the whole source tree is Apple's **`com.apple.kec.corecrypto`** kext. Nothing to link and
nothing to stub — **a value that has to be written**, which is 432's `pthread_functions` shape.

And the call is unconditional. `tcp_init`'s inlined `tcp_tfo_init()`:

    8035ac58:  mov r0, r4 ; mov r1, #16
    8035ac60:  bl  read_frandom                  <- the key
    8035ac70:  bl  aes_encrypt_key128            <- "aes_encrypt_key128(key, &tfo_ctx)"

with no branch between `tcp_init`'s entry and that call but `tcp_initialized`. So on the real device
the table is non-NULL before `bsd_init` runs and supplying it is *reproducing the kernel's own
configuration*, not working around it.

## The design decision, and why it is the load-bearing one

**The AES entries are real; everything else in the table is a stand-in that stops and names the field
it was called through.** The split is not a preference — it follows from what a stand-in *is* in this
instrument:

> A stand-in earns its keep by stopping and naming itself. `aes_encrypt_key128`'s call site cannot
> stop: it is unconditional inside `tcp_init`, so stopping there is stopping `domaininit` and
> therefore `bsd_init`. A stand-in that returned without writing would leave `tfo_ctx`
> uninitialized — 436's defect class exactly, a wrong value with nothing to report it. Real AES is
> the only option that is both honest and survivable, and the only one that can be *measured*.

`aes_encrypt_key128` needs exactly one thing from the table: `ccaes_cbc_encrypt`, a pointer to a
`struct ccmode_cbc`, and from that descriptor exactly one method — `init`, called as
`cccbc_init(cbc, cx[0].ctx, 16, key)`. So the work is one AES-128 key expansion of 16 random bytes.

The other 45 members are all non-NULL and all stand-ins, so a callback this image does not implement
reports `stub_hit=g_crypto_funcs.<field>` with a caller key instead of faulting on a NULL. Eleven
linked objects read 28 of those members (`libkern/crypto/corecrypto_{aes,aesxts,des,md5,rand,rsa,
sha1,sha2,chacha20poly1305}.o`, `bsd/netinet/flow_divert.o`, `osfmk/kern/btlog.o`); descriptor-valued
fields get a stand-in **descriptor** of the right type rather than a NULL, for the same reason.

**The one knowingly imperfect stand-in is named rather than left to be found:** `ccdigest_di_decl` and
`cchmac_di_decl` size a stack buffer from `di->state_size` *before* any method is called, so a
stand-in `ccdigest_info` with `state_size = 0` gives that declaration a zero-length context and the
stop arrives one statement later, at the stand-in method, with the right name and key. Nothing on the
boot path reaches a digest — `flow_divert_packet_compute_hmac` is its only user and it is a data-path
function behind a `g_crypto_funcs == NULL || group->token_key == NULL` guard — so this is recorded,
not fixed. A real SHA-1 is the step that reaches it.

## A value needs a check that stops the build

437 adds a **value** to the image, and a wrong value is the one failure mode here with nothing to
report it — `mi4-stand-in-size-is-not-value`, and 436's own stop, which was a zero no stub named. So
the AES is verified twice, and neither check is a claim:

- **On the host, against FIPS-197's published vectors, before the object is linked.** `build_entry.sh`
  compiles the very same `stage90_aes.c` with `-DSTAGE90_AES_SELFTEST` and runs it. 19 checks:
  Appendix A.1's **whole key expansion** (all 11 round keys — the key schedule is what the boot path
  actually calls, so a wrong `Rcon` or a wrong `RotWord` would be invisible in a block test), Appendix
  B (both directions), Appendix C.1 (both directions), and SP 800-38A F.2.1/F.2.2 (CBC over four
  blocks, both directions, plus the chained IV). A failure fails the build.
- **In the image, in the constructor, against FIPS-197 Appendix C.1** — the same vector run by the
  compiled-for-ARM code, reported as `xnu_entry_stage90_crypto_kat` with the first four ciphertext
  bytes in `..._kat_w0`. That is what covers the compiler and the target, which the host run cannot.

Two structural checks sit beside them: `_Static_assert(sizeof(struct stage90_aes_cbc_ctx) <=
sizeof(aes_encrypt_ctx))`, which makes the `panic` inside `aes_encrypt_key` unreachable at build time;
and a constructor scan of the table's 47 words for NULL, which is 432's idiom and the check on this
file's own completeness — a member this file forgot is a NULL that C zeroes silently, and a NULL in
this table is exactly what 436 died on.

## The prediction, and the measurement

**Prediction, written before the build and before the run:** the stub set does not move
(`44 function(s), 0 storage`, because the new objects' references resolve against names the image
already has); the constructor writes nine records and no `stub_hit`; and the stop is
**`stub_hit=kmstartup`, caller key `0x8003B238`** — `bsd_init + 0x848`, the return address of the
`bl <kmstartup>` at `bsd_init + 0x844`.

**Measured on hardware: nine records, exactly one `stub_hit`, and the key to the byte.**

    line 3912:  xnu_entry_image_bytes=0x004e7450
    line 3913:  xnu_entry_bss_start=0x804e7480
    line 3914:  xnu_entry_bss_end=0x805388f8
    line 3933:  xnu_entry_kv_written=0x00000226
    line 3935:  xnu_entry_kv_dropped=0x00000000
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973:  xnu_entry_stage90_crypto_funcs_ptr=0x00000000
    line 3974:  xnu_entry_stage90_crypto_table_addr=0x8047939c
    line 3975:  xnu_entry_stage90_crypto_words=0x0000002f
    line 3976:  xnu_entry_stage90_crypto_nulls=0x00000000
    line 3977:  xnu_entry_stage90_crypto_cbc_desc=0x80479518
    line 3978:  xnu_entry_stage90_crypto_cbc_size=0x000000c0
    line 3979:  xnu_entry_stage90_crypto_aes_ctx_size=0x000007c0
    line 3980:  xnu_entry_stage90_crypto_kat=0x00000001
    line 3981:  xnu_entry_stage90_crypto_kat_w0=0x69c4e0d8
    line 3982:  xnu_entry_stage90_pthread_functions_ptr=0x80478a84
    line 3983:  stub_hit=kmstartup
    line 3984:  xnu_entry_stub_caller=0x8003b238
    line 3990:  xnu_entry_stub_caller_e=0x8003b238
    line 3991:  No errors detected

`_w0`/`_w1` spell the key back as ASCII (`"8003"` = `0x33303038`, `"b238"` = `0x38333262`), and the
call site is confirmed against the linked image: `bsd_init` `0x8003A9F0`, the `bl <kmstartup>` at
`+0x844`, the key its return address `+0x848`. **436's `data abort` is gone** (`abort_entries=0`,
every `abort_first_*` word zero).

## The reading: the key is the proof, and the one record that is zero is the best evidence in the run

**`0x8003B238` cannot be reached unless `tcp_init` returned.** `kmstartup` is at `bsd_init + 0x844`;
the AES call is inside `tcp_init`, which `domaininit` (`bsd_init + 0x818`) calls. So the stop is a
*positive* measurement that AES-128 ran to completion on this device — `tcp_init` read
`g_crypto_funcs->ccaes_cbc_encrypt`, read `cbc->size` (0xC0), called `cbc->init(cbc, ctx, 16, key)`,
ran 10 rounds of key expansion over 16 bytes from `read_frandom`, returned — and then `iptap_init`,
`flow_divert_init`, `memorystatus_init` and `acct_init` ran too. Four calls past where 436 died, all
of them real code.

**And `xnu_entry_stage90_crypto_funcs_ptr=0x00000000` is the row the prediction got wrong, in the way
that is worth having wrong.** It was predicted to be `0x8047939C` — the table's address read back
through `g_crypto_funcs` — and it is zero **because the read happens at the top of the constructor,
before `register_crypto_functions()` is called**. That is the measurement of 436's central fact from
*inside the image*: nothing else in this kernel writes the table, so the pointer is still zero at the
moment this image takes responsibility for it. The pair completes the statement: the constructor's
closing `if (g_crypto_funcs != &stage90_crypto_functions)` did **not** fire (no `wrong_table`), so the
registration succeeded. Read together, `ptr = 0 at entry` and `no wrong_table at exit` bracket the
whole of 437's claim.

**The second prediction miss is arithmetic and its explanation is measured.**
`xnu_entry_stage90_crypto_aes_ctx_size` was predicted `0x00000120` (288) and measured
**`0x000007C0` (1984)**. `AES_CBC_CTX_MAX_SIZE` in `<libkern/crypto/aes.h>` has two branches and the
build takes the first:

    #if defined(__ARM_NEON__) && !defined(__arm64__)
    #define AES_CBC_CTX_MAX_SIZE (... + (14-1)*128+32)     <- this one

and `clang --target=armv7-unknown-netbsd-eabi -mfpu=neon-vfpv4 -mfloat-abi=softfp -dM -E` does define
`__ARM_NEON__ 1`. So the context corecrypto would use here is the **bit-sliced** one — 13 × 128 + 32
bytes of expanded keys — and my 0x120 was the branch for a build without NEON. The static assertion
passes either way (0xC0 ≤ 0x7C0), which is the point of asserting rather than predicting.

**`xnu_entry_stage90_crypto_words=0x2F`** is 47, the member count derived from
`sizeof(struct crypto_functions) / sizeof(void *)` — so the completeness scan covers Apple's whole
struct rather than a list this file keeps in step by hand, and it reports 0 NULLs.

**`xnu_entry_kv_written` moved `0x34 → 0x226`** (52 → 550 bytes), and that is `g_kv_len` in *bytes*
(434 recorded the same distinction): 436's image wrote none of these nine records and 437's writes
all of them, plus the stub name changed from none to `kmstartup`.

## Layout

    entry text   0x4B4AC0 (4934336)     <- was 0x4B2A80: +0x2040
    entry image  0x4E7450 (5141584)     <- was 0x4E344C: +0x4004
    .data        0x804B8000 (0x2E360)   <- was 0x804B4000; unmoved in size, placed 16 KB higher
    .sysctl_set  0x804E6360 (0xFD8)     <- was 0x804E2360
    .init_array  0x804E7338 (0x118)     <- was 0x804E3338 (0x114): +4, one more constructor
    bss          0x804E7480 .. 0x805388F8 (332920)   <- both ends +0x4000, size unmoved
    layout       args 0x8053A000, topOfKernelData 0x80700000, tree 0x80900000, window 16777216
    headroom     1865480 bytes below topOfKernelData
    payload      out/stage90/stage90-qcdt.img, sha256
                 5725e989042e9403a9078a2452721bb45fecb8a37c08c5ebbb45e928a68a5649 (8161280 bytes, 7970 KB)
    entry bin    5141584 bytes; sha256
                 a14eed81fac7d741a5da64586875f8f9f2d564d387c78590cded4680f1b4c9e7

**`.init_array` grew by exactly 4 bytes and that is the whole of the new constructor**, which is the
one row 437 was entitled to move. `.bss` **size** is unmoved at 0x51478 with both ends +0x4000 — the
two files bring no writable data at all, so the shift is entirely the `.data` boundary re-placing
itself around a 0x4020 text growth. And the descriptor's own bytes are in the image exactly as
predicted — `.size` `0x000000C0`, `.block_size` `0x00000010`, `custom` 0 — read out of the ELF at
`0x80479518` before the device ran.

`verify_sections` still reports exactly `.bss .data .init_array .sysctl_set .text`, and the build's
own line: *"the copied image ends at 0x804e7450, **48 bytes below** `__bss_start`"*.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, **`abort_entries=0`**,
`checks=5` / `failures=0`, **no `exception:` line and no `panic:` line**, `kv_dropped=0`. The only net
armed across the jump was the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Payload 7970 KB accepted by `fastboot boot`. Device
returned to Android on its own and was confirmed there (`ro.product.device` cancro,
`ro.build.version.release` 10). 302266 bytes / 3991 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run436_kmsg.txt`, `run437_kmsg.txt`.

## Where the frontier is now: `bsd_init` has **one** stub left, and it is a compile error

`bsd_init` makes **232 calls and exactly one of them is still a stub**:

    +0x844   key 0x8003B238   kmstartup

Everything else in `bsd_init`'s statement list is real code, and everything *after* `bsd_init` in
`kernel_bootstrap_thread` — `OSKextRemoveKextBootstrap`, `kdebug_free_early_buf`,
`serial_keyboard_init`, `vm_page_init_local_q`, `thread_bind`, `vm_pageout()` — has been real since
435. **So the goal's minimum bar is one function away, and that function is not a link and not a
value: it is `bsd/kern/subr_prof.c`, which does not compile.** It uses `STATIC`, which
`bsd/kern/kern_sysctl.c` defines at its own line 204 and no header `subr_prof.c` includes, so
`out/xnu_kernel_obj/bsd_kern_subr_prof.o` does not exist and `kmstartup` has been a stand-in since
425. Giving the file that one macro is a stage-owned change to the build's include set, not a change
to Apple's source — and it is 438.

**Still owed and unchanged: the timer** (`ml_init_timebase` plus an MSM8974 `tbd_ops_t` over the GPT
at `0xf9020000`, and 405's `IOCPUInterruptController`). Nothing on the path from here to `vm_pageout`
has yet taken a deadline, but `vm_pageout` is where one first appears.
