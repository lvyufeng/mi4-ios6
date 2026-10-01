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
#   read_storage_key_order.py --selftest
#                           re-derives the two landed *moments* defects - m732 (a rung-3/4 cell
#                           read for a moment that belongs to rung 7) and m763 (the key quoted
#                           for "after the power byte" is published by the reset stage, first) -
#                           from the call order of `entry_storage.c` alone. It is a fifth check
#                           rather than a fourth because the tool it guards was WRONG TWICE
#                           while it was being written: once naming every function `__attribute__`,
#                           once truncating a macro-generated key to half a name. Both times the
#                           table it printed looked plausible. A reference tool that can lie like
#                           that has to carry a self-test, and the self-test has to run.
#   check_count_citations.py
#                           reads every tracked file outside archive/ and refuses a count quoted in two
#                           bases that do not agree with each other. The hexadecimal word is the reading
#                           and the decimal beside it is a transcription of it, and the transcription had
#                           never been checked: three were wrong at once in one sentence, and one of them
#                           is the count the ladder's `#error` text, the record and this repository's
#                           readiness narration all quote for CMD1 - the device printed 5,091,328 there and
#                           the decimal written down was 5,088,000 (778). **This comment quotes that pair
#                           in prose and NOT as the form the tool reads, deliberately**: a citation of a
#                           defect and the defect are the same characters, so the note in
#                           records/revert-set.txt that refutes the wrong pair spells its two halves in
#                           separate sentences, and so does this one.
#                           It REFUSES on `tools/`, `scripts/`, `records/`, `Makefile` and `src/` minus
#                           `src/entry/` - exempt for a COST reason and not a lane reason, because an
#                           entry source is a member of the entry image's recorded source manifest and a
#                           comment there makes the gate refuse the arm a press is waiting on - and it
#                           only PRINTS what it finds under docs/experiments/, which is the historical
#                           record and is superseded rather than rewritten. It carries a self-test, run by
#                           the line above it, because it is a reference tool like the one above - and the
#                           self-test asserts the four idioms that look like a citation and are not:
#                           `offset (step)`, a tick count beside a millisecond figure, a decimal with no
#                           separators, and a pair three orders of magnitude apart.
#   report_int_enable_windows.py
#                           the census of every store to INT_ENABLE 0x34: which word each command
#                           window writes, resolved by NAME through the assignment that builds it and
#                           never by line number. Reports the SCOPE of the rung-23 widening (one window
#                           of three) and refuses a store whose value is a bare literal, because a
#                           literal there is one value with two definitions.
#   derive_sdc1_pads.py --selftest
#                           the pad arithmetic and the census rules, as cells. The ptr-only cells need
#                           no input at all; the four census cells run against the device's own dt.img and
#                           are SKIPPED BY NAME where it does not exist, so a cell that cannot run never
#                           reads as one that ran and agreed.
#   check_line_citations.py
#                           reads the press path's own prose - `tools/verify_press_ready.sh` and
#                           `records/revert-set.txt` - and refuses a `file.c:NNNN` citation whose site has
#                           MOVED. The baseline is GIT and not a file, because a tool that records its own
#                           expected values is a self-written record: for each citation it takes the commit
#                           that WROTE it (`git log -p --reverse`, one history walk per citing file) and
#                           asks what that line said then. 802 section 7 measured by hand that the ladder's
#                           CMD2 gate had moved from `:4785` to `:4964` and the single CMD1 call from
#                           `:4702` to `:4808`; the same question over this tree finds THIRTY-THREE moved
#                           citations in the press path, so this is a population and not three instances.
#                           **It refuses only the prose that PRINTS FOR THE LIVE ARM** - the `rung_para N`
#                           line and the `entry_conseq+=` lines for the rung the artifact's own switch
#                           record names - because a paragraph for another rung does not print on this arm
#                           and the `#` comments never print; and it only PRINTS what it finds in the
#                           historical record for the same reason check_count_citations.py does. It does
#                           NOT decide a citation whose text now appears at more than one line (147 lines of
#                           `entry_storage.c` are duplicated), because guessing which one it moved to is
#                           the defect it exists to catch. `--live-rung N` examines another rung's paragraph,
#                           which is how the refusal path is demonstrated without editing the tool.
#   check_sdc1_pad_expectation.py
#                           reads the TRACKED record records/sdc1-pad-candidates.txt and refuses an
#                           `ST_TLMM_SDC1_EXPECT` that matches no board the DEVICE'S OWN device tree
#                           declares. It reads the record and not the device tree because the device
#                           tree is `.gitignore`d (`xiaomi4-cancro-backup-*/`, like `external/`), so a
#                           clone cannot have it - and that is the point of the record: the source's
#                           comment cites `msm8974pro-ac-pm8941-mtp-v5.dts` as *the Mi 4's own board
#                           file*, and the vendor checkout holds no cancro device tree at all, only
#                           Qualcomm's reference boards. The tool that writes the record
#                           (`tools/derive_sdc1_pads.py --write-record`) does need the dt.img, and says
#                           so when it is missing. It does NOT refuse on ambiguity: this device declares
#                           two different pad words and the bootloader's choice is made on the device,
#                           so an ambiguous record prints its candidates and passes, and a no-match is
#                           the only refusal (779).
#   check_response_word_order.py
#                           the 136-bit response has ONE word order and it is the driver's. The vendor's
#                           `sdhci_finish_command` (external/.../sdhci.c:1163-1172) reads the four words
#                           WORD 3 FIRST (`SDHCI_RESPONSE + (3-i)*4`), shifts each left by 8, and ORs the
#                           stripped CRC's low byte back in from the address ONE BELOW the word, except for
#                           the last (`i != 3`). Every part of that is invisible in the register's name, and
#                           a copy that gets it wrong CANNOT FAIL LOUDLY: ascending word order, or a dropped
#                           byte, still yields 128 plausible bits that decode to a zeroed-out CSD or CID -
#                           no fault, no error, no cell. Rung 33 read a real SanDisk CID through this
#                           arithmetic and the tree holds SIX response-reading functions, in three shapes
#                           (the four-word assembly, the word-0 pair `:3995`'s comment calls "one arithmetic
#                           at three times", and the four raw words as a freshness witness). It refuses an
#                           offset the register does not have, an ascending or repeated word inside one
#                           command segment, and a CRC byte that is not the one below its own word. The
#                           NEXT rung is CMD9 (SEND_CSD, `ac R2`), which is the next place a fifth copy
#                           would be written: experiment 809's instruction to call the existing assembler
#                           rather than re-derive it was, until this runs, only a sentence in a document.
#                           `--selftest` holds nine fixtures - the four shapes this tree holds, and each of
#                           the five ways the arithmetic goes wrong - and exits 2 for a failed selftest,
#                           which is a different failure from the tree being wrong (experiment 810).
#   check_hfs_staged.sh
#                           the HFS+ port's tracked additions (src/shims/hfs/hfs_port_force.h,
#                           src/shims/hfs/hfs_cprotect_port.h and src/supply/stage90_hfs_shims.c) are
#                           the ONE definition the host-only probe AND tools/stage_hfs.sh both use; the
#                           untracked 4570 tree is a re-provisionable checkout that can fall behind them.
#                           It refuses a force-header #define that 4570 already defines at a different
#                           value - the "one value, two definitions" defect, silent when it happens - and
#                           a shims file that no longer defines the ten symbols 871 measured the port at.
#                           It also checks that the build force-includes the cprotect header second (877),
#                           and checks src/supply/hfs_files.txt (the manifest's HFS additions) against the
#                           staged tree, SKIPPING that part with a printed note when the tree is not
#                           staged rather than passing quietly. No compiler, no device. (874/875/876/877.)
check:
	@tools/check_stage_paths.sh
	@tools/check_set_name_rule.sh
	@tools/check_backtick_messages.sh --selftest >/dev/null
	@tools/check_backtick_messages.sh
	@tools/check_payload_config_entry.sh
	@tools/read_storage_key_order.py --selftest >/dev/null
	@tools/check_count_citations.py --selftest >/dev/null
	@tools/check_count_citations.py
	@tools/report_int_enable_windows.py --selftest >/dev/null
	@tools/report_int_enable_windows.py
	@tools/derive_sdc1_pads.py --selftest >/dev/null
	@tools/check_sdc1_pad_expectation.py --selftest >/dev/null
	@tools/check_sdc1_pad_expectation.py
	@tools/check_line_citations.py --selftest >/dev/null
	@tools/check_line_citations.py
	@tools/check_response_word_order.py --selftest >/dev/null
	@tools/check_response_word_order.py
	@tools/check_hfs_staged.sh

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
	@echo "  ./build.sh is reproducible from the tree AND this arm's switch set (666 section 5: three"
	@echo "  builds, twice plain and identical, and a parked arm rebuilt file-for-file) - but not"
	@echo "  from a tree that has since moved, and not without STAGE90_XNU_ENTRY=1, which the build"
	@echo "  leaves OFF by default. So a deleted payload is a press that cannot be re-verified."
	@echo "  763 corrected the 408 citation this line used to carry. To clear only the reproducible"
	@echo "  half - the object directories, the payload's"
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
	@echo "make check               the repository's own bookkeeping: the retired layout, set names, backticked"
	@echo "                         messages (a code span that would run a command is refused too), the"
	@echo "                         payload's entry switch, every count quoted in two bases, the board-derived"
	@echo "                         pad constant against the device's own device tree, and a cited line"
	@echo "                         number whose site has moved out from under it"
	@echo "make clean               refused; the payload and the parks cannot be rebuilt"
	@echo
	@echo "Booting is not a make target: flash nothing, and boot non-persistently with"
	@echo "  sudo fastboot boot $(LIVE_OUT)/stage90-qcdt.img"
	@echo "Gate a run first: ./scripts/preflight_boot_check.sh --allow-xnu-entry"
