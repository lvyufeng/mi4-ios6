# Convenience entry point for the Xiaomi Mi 4 (cancro) / MSM8974 XNU bring-up.
#
#   make                 build the newest stage snapshot
#   make stage89         build one specific stage
#   make list            show the retained snapshots
#   make restore STAGE=50   bring an archived stage back (see tools/stage-archive.sh)
#   make clean           remove the build products of the retained stages
#
# Nothing needs editing here when stage91 is added: the newest snapshot is
# discovered from the directory listing.
#
# Booting is deliberately NOT a make target. Flashing is a privileged, per-action
# decision - run the printed `sudo fastboot boot` command yourself.

STAGES_DIR := stages
STAGES     := $(shell ls -d $(STAGES_DIR)/stage* 2>/dev/null | sort -V)
NEWEST     := $(lastword $(STAGES))

.DEFAULT_GOAL := all

# Only the aggregate targets are declared phony. The stageNN targets must NOT be
# listed here: an explicit .PHONY entry for `stage90` would shadow the pattern
# rule below and make the build a no-op.
.PHONY: all build list clean restore help

all: build

build:
	@if [ -z "$(NEWEST)" ]; then echo "make: no stage snapshots under $(STAGES_DIR)/"; exit 1; fi
	@$(MAKE) --no-print-directory $(notdir $(NEWEST))

# Matches any retained snapshot, e.g. `make stage89`.
stage%:
	@if [ ! -x "$(STAGES_DIR)/$@/build.sh" ]; then \
	  echo "make: no such stage: $@ (retained: $(notdir $(STAGES)))"; \
	  echo "     for an archived stage: tools/stage-archive.sh restore <N>"; \
	  exit 1; \
	fi
	@echo "== building $@ =="
	cd $(STAGES_DIR)/$@ && ./build.sh
	@echo
	@echo "$@ built into out/$@/"
	@echo "Verify it non-persistently with:"
	@echo "    sudo fastboot boot out/$@/$@-qcdt.img"

list:
	@echo "Retained stage snapshots under $(STAGES_DIR)/:"
	@for d in $(STAGES); do \
	  n=$$(basename $$d); \
	  if [ "$$n" = "$(notdir $(NEWEST))" ]; then mark=" <- current"; else mark=""; fi; \
	  printf '  %-9s %3s files%s\n' "$$n" "$$(ls -1 $$d | wc -l)" "$$mark"; \
	done
	@echo "Archived: stage85 - stage89 are under archive/stages/ (read-only history);"
	@echo "          stage0  - stage84 live at the tag: tools/stage-archive.sh list"

clean:
	@for d in $(STAGES); do \
	  n=$$(basename $$d); \
	  rm -rf out/$$n; \
	done
	@echo "removed the build products of: $(notdir $(STAGES))"

restore:
	@if [ -z "$(STAGE)" ]; then \
	  echo "usage: make restore STAGE=<N>   (e.g. make restore STAGE=50)"; \
	  exit 1; \
	fi
	tools/stage-archive.sh restore $(STAGE)

help:
	@echo "make                     build the newest stage snapshot"
	@echo "make stage89             build one specific stage"
	@echo "make list                show the retained snapshots"
	@echo "make restore STAGE=50    bring an archived stage back"
	@echo "make clean               remove the build products of the retained stages"
