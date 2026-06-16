# Stage89 Quick Start

## Device Recovery (if hung)

```bash
# 1. Long press power button (10 sec) until device powers off
# 2. Short press power button to boot
# 3. Wait for Android to start
# 4. Verify: adb devices
```

## One-Command Test

```bash
cd /mnt/data/mi4-ios6 && /tmp/wait_and_test_stage89.sh
```

This automatically:
- Waits for device
- Reboots to fastboot
- Boots Stage89
- Captures and displays results

## Manual Test

```bash
cd /mnt/data/mi4-ios6

# Enter fastboot
adb reboot bootloader
# OR: Power off, then Power+Vol Down

# Boot Stage89
sudo fastboot -s 4a2fe00b boot out/stage89/stage89-qcdt.img

# Wait and capture
adb wait-for-device && sleep 5
adb shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage89.txt

# Check results
grep "stage89_xnu_macho_loader_status" /tmp/stage89.txt
grep "kernel_entry returned" /tmp/stage89.txt
```

## Success Indicators

✅ `stage89_xnu_macho_loader_status=0x89000001`  
✅ `segments_loaded=5` (or 6)  
✅ `ready_for_handoff=0x00000001`  
✅ `kernel_entry returned success`

## If Dry-Run Succeeds

Enable actual loading in `xnu_macho_loader.c`:

```c
// Uncomment these lines in load_segment():
if (copy_size > 0) {
    memcpy(dest, src, copy_size);
}
if (zero_size > 0) {
    memset(dest + copy_size, 0, zero_size);
}
```

Then rebuild and test again.

## Current Status

- **Mode**: DRY-RUN (safe, no memory writes)
- **Build**: Ready (hash: 5dd16bb6...)
- **Device**: Needs manual reboot

## Next Phase

Once actual loading works:
1. Set r0 = boot_args pointer
2. Jump to XNU entry VA
3. Catch first crash
4. Debug and iterate
