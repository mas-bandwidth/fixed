CBMC ?= cbmc
TIMEOUT ?= 1200
WIDTH ?= 64
SOLVER ?= --sat-solver cadical
EVIDENCE ?= /home/glenn/stella-jobs/resumed-1508/fixed-cbmc-int128-paths-agree.w2/evidence
SRC := $(CURDIR)/int128_equiv.c
INC := $(CURDIR)/../../include
FLAGS := --unwinding-assertions --bounds-check --signed-overflow-check --undefined-shift-check --slice-formula

.PHONY: all fixmul fixdiv isqrt multiply shifts compares divisions
all: fixmul fixdiv isqrt multiply shifts compares divisions

# CBMC 6.6.0 has no --timeout option. GNU timeout enforces the same wall limit.
define PROVE
	@set -eu; mkdir -p "$(EVIDENCE)"; tmp=$$(mktemp -d "$(CURDIR)/.cbmc-work.XXXXXX"); \
	trap 'rm -f "$$tmp"/*.c; rmdir "$$tmp"' EXIT HUP INT TERM; \
	{ printf '%s\n' '#define CBMC_NATIVE_ARM'; cat "$(SRC)"; } > "$$tmp/native.c"; \
	{ printf '%s\n' '#define CBMC_EMULATED_ARM'; cat "$(SRC)"; } > "$$tmp/emulated.c"; \
	{ printf '%s\n' '#define CBMC_HARNESS_TU'; cat "$(SRC)"; } > "$$tmp/harness.c"; \
	printf 'Command: timeout %ss $(CBMC) -I %s --function %s --unwind %s -D CBMC_WIDTH=$(WIDTH) $(FLAGS) $(SOLVER) <three generated translation units>\n' "$(TIMEOUT)" "$(INC)" "$(1)" "$(2)"; \
	set +e; /usr/bin/time -f "ELAPSED_SECONDS=%e" timeout --signal=TERM "$(TIMEOUT)s" $(CBMC) -I "$(INC)" --function "$(1)" --unwind "$(2)" -D CBMC_WIDTH=$(WIDTH) $(FLAGS) $(SOLVER) "$$tmp/native.c" "$$tmp/emulated.c" "$$tmp/harness.c" > "$(EVIDENCE)/cbmc.$@.log" 2>&1; status=$$?; set -e; \
	cat "$(EVIDENCE)/cbmc.$@.log"; \
	if [ $$status -ne 0 ]; then printf 'CBMC/timeout exit status: %s\n' "$$status"; exit $$status; fi
endef

fixmul:
	$(call PROVE,harness_fixmul,2)
fixdiv:
	$(call PROVE,harness_fixdiv,132)
isqrt:
	$(call PROVE,harness_isqrt,132)
multiply:
	$(call PROVE,harness_multiply,2)
shifts:
	$(call PROVE,harness_shifts,8)
compares:
	$(call PROVE,harness_compares,8)
divisions:
	$(call PROVE,harness_divisions,132)
