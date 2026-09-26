# Convenience entry point for the Xiaomi Mi 4 (cancro) / MSM8974 XNU bring-up.
#
#   make                 build the live tree -> out/stage90/stage90-qcdt.img
#   make list            the archived stage snapshots and where they live now
#   make restore STAGE=50   bring an archived snapshot back (tools/stage-archive.sh)
#   make check           no tracked file still names the retired snapshot layout
#   make clean           REFUSED - see below, and read it before changing it
#
# **There is one tree and it is not a snapshot.** Until 2026-09-26 this repository was one directory
# per stage and `make` discovered the newest one from the directory listing. That model was retired:
# the live code is `src/`, the scripts that build and fire it are `scripts/`, the arm record is
# `records/`, and the five snapshots that were still in the working tree are `archive/stages/`
# (`stage0` .. `stage84` live at the tag `stage-archive-base`). Read `archive/stages/README.md`.
# There is therefore no `stage%` pattern rule any more: a second stage does not exist to match it, and
# the rule that used to build one was a directory-discovery trick rather than a build step.
#
# Booting is deliberately NOT a make target, unchanged from before. `fastboot boot` is a per-action
# decision and it is non-persistent; nothing here flashes.

LIVE_OUT := out/stage90

.DEFAULT_GOAL := all

.PHONY: all build list restore check clean help

all: build

build:
	@./scripts/build.sh
	@echo
	@echo "built into $(LIVE_OUT)/"
	@echo "Verify it non-persistently with:"
	@echo "    sudo fastboot boot $(LIVE_OUT)/stage90-qcdt.img"

list:
	@tools/stage-archive.sh list

# The retired model's own escape hatch, kept because the archived snapshots are still readable and
# restorable. A restored snapshot lands at the repository ROOT so that its pre-2026-09-26 scripts
# resolve `../out`, `../external` and `../tools`; see tools/stage-archive.sh.
restore:
	@if [ -z "$(STAGE)" ]; then \
	  echo "usage: make restore STAGE=<N>   (e.g. make restore STAGE=50)"; \
	  exit 1; \
	fi
	tools/stage-archive.sh restore $(STAGE)

# Two claims that are commit-message prose otherwise, and both are about the repository's own bookkeeping
# rather than about the artifact:
#   check_stage_paths.sh    reads every tracked file outside docs/ and archive/ and refuses an executable
#                           literal that names the retired snapshot layout.
#   check_set_name_rule.sh  reads records/revert-set.txt and refuses a set NAME whose 8 hex characters are
#                           not the sha256 prefix of one of that set's own members - and, for the storage
#                           family, not the entry image's. The rule was prose inside a `role=` string until
#                           732 named an arm after its qcdt and nothing noticed (see that file's header).
# Neither one touches the device, `out/`, or the gate.
check:
	@tools/check_stage_paths.sh
	@tools/check_set_name_rule.sh
	@tools/check_backtick_messages.sh
	@tools/check_payload_config_entry.sh

# **`make clean` IS REFUSED, AND THAT IS THE POINT OF IT.** It used to `rm -rf out/stageNN` for every
# retained snapshot. There is exactly one `out/` now and it is not reproducible: `./build.sh` does not
# produce byte-identical output (experiment 408 measured that), so `out/stage90/stage90-qcdt.img` and
# the parked arms under `out/stage90/frozen/` are the only in-tree copies of images this device was
# already booted from, and `out/stage90/captures/` holds the logs those runs came back with. A target
# that deletes all three is one keystroke from destroying the record, and the keystroke is a word every
# operator types from habit.
#
# Removing the recoverable half is still available and is spelled out rather than hidden behind a flag:
# the object directories and the entry image rebuild from the tree. Nothing here deletes a park, a
# capture, or the payload.
clean:
	@echo "make clean is refused: $(LIVE_OUT)/ holds artifacts this repository cannot rebuild."
	@echo
	@echo "  not reproducible   $(LIVE_OUT)/stage90-qcdt.img, $(LIVE_OUT)/stage90.img,"
	@echo "                     $(LIVE_OUT)/stage90.bin, $(LIVE_OUT)/stage90.elf,"
	@echo "                     $(LIVE_OUT)/stage90-build-config.txt, $(LIVE_OUT)/SHA256SUMS.txt,"
	@echo "                     $(LIVE_OUT)/frozen/ (the parked arms),"
	@echo "                     $(LIVE_OUT)/captures/ (the run logs)"
	@echo "  reproducible       $(LIVE_OUT)/xnu_arm_entry.bin, the object directories, out/xnu_*_obj/"
	@echo
	@echo "  ./build.sh does NOT reproduce (408), so a deleted payload is a press that cannot be"
	@echo "  re-verified. To clear only the reproducible half - the object directories, the payload's"
	@echo "  own objects, and the entry image, which was measured byte-for-byte reproducible across"
	@echo "  the 2026-09-26 restructure - run:"
	@echo "      rm -rf out/stage90/xnu-objects out/xnu_*_obj out/xnu_assym \\"
	@echo "             out/stage90/*.o out/stage90/xnu_arm_entry.bin out/stage90/xnu_arm_entry.elf"
	@echo
	@echo "  That list is every glob here that resolves today; if one of them stops matching, the"
	@echo "  directory moved and this target's remedy is stale rather than the tree's."
	@exit 1

help:
	@echo "make                     build the live tree into $(LIVE_OUT)/"
	@echo "make list                the archived snapshots and where they live now"
	@echo "make restore STAGE=50    bring an archived snapshot back"
	@echo "make check               no executable literal names the retired layout, and every set name is a hash of its own bytes"
	@echo "make clean               refused; the payload and the parks cannot be rebuilt"
	@echo
	@echo "Booting is not a make target: flash nothing, and boot non-persistently with"
	@echo "  sudo fastboot boot $(LIVE_OUT)/stage90-qcdt.img"
	@echo "Gate a run first: ./scripts/preflight_boot_check.sh --allow-xnu-entry"
