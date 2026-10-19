# CBMC: fixRsqrtExact, fixFmod and fixMod meet their integer specifications, on
# both 128-bit arms. Bench-only; not part of CTest or CI.
#
# Each property is built and proved once per arm: the native arm (CBMC_NATIVE_ARM,
# with __x86_64__ undefined so the library division takes its plain C spelling)
# and the emulated arm (CBMC_EMULATED_ARM, FIX_FORCE_EMULATED_INT128). Each run is
#   cbmc -I include -D <arm> -D CBMC_WIDTH=.. -D CBMC_SIGNS=.. --function harness_<p>
#        --unwind <n> --unwinding-assertions --bounds-check --signed-overflow-check
#        --div-by-zero-check --undefined-shift-check --slice-formula
#        --external-sat-solver cadical <spec.c>
# under GNU timeout (CBMC has no --timeout option here). Logs go to $(LOGDIR)
# (default $TMPDIR/fixed-cbmc-rsqrt-mod-logs, outside the tree). WIDTH_<target> and
# SIGNS_<target> are the narrowing results.md names; 64 with both signs is the
# whole signed 64-bit domain for one input.
# Override on the command line, e.g. make rsqrt WIDTH_rsqrt=12 SIGNS_rsqrt=0 TIMEOUT=600.

CBMC ?= cbmc
TIMEOUT ?= 900
SOLVER ?= --external-sat-solver cadical
LOGDIR ?= $(or $(TMPDIR),/tmp)/fixed-cbmc-rsqrt-mod-logs
INC := $(CURDIR)/../../../include
FLAGS := --unwinding-assertions --bounds-check --signed-overflow-check --div-by-zero-check --undefined-shift-check --slice-formula

PROPS := rsqrt rsqrt_high rsqrt_edges mod mod_edges

SRC_rsqrt := $(CURDIR)/rsqrt_spec.c
SRC_rsqrt_high := $(CURDIR)/rsqrt_spec.c
SRC_rsqrt_edges := $(CURDIR)/rsqrt_spec.c
SRC_mod := $(CURDIR)/mod_spec.c
SRC_mod_edges := $(CURDIR)/mod_spec.c

# Narrowed positive raw: its low CBMC_WIDTH bits are free, zero- or ones-extended;
# the two sign runs are the union.
WIDTH_rsqrt := 4
SIGNS_rsqrt := 0 1
UNWIND_rsqrt := 10
# Top of the positive range: raw = INT64_MAX - u with u the free low bits.
WIDTH_rsqrt_high := 4
SIGNS_rsqrt_high := 0
UNWIND_rsqrt_high := 70
# Fixed constants: 1, 2, 2^50 (tie), 2^50+1, INT64_MAX.
WIDTH_rsqrt_edges := 1
SIGNS_rsqrt_edges := 0
UNWIND_rsqrt_edges := 70
# Both inputs narrowed by CBMC_WIDTH, all four sign combinations.
WIDTH_mod := 6
SIGNS_mod := 0 1 2 3
UNWIND_mod := 2
# The trapping / zero-divisor pairs, as constants.
WIDTH_mod_edges := 1
SIGNS_mod_edges := 0
UNWIND_mod_edges := 2

.PHONY: all $(PROPS)

all: $(PROPS)

# run_arm <target> <arm> <signs> <extra flags>
define run_arm
	@mkdir -p "$(LOGDIR)"; \
	unwind=$(or $(UNWIND_$(1)),12); w=$(or $(WIDTH_$(1)),8); rc=0; \
	for signs in $(3); do \
	cmd="$(CBMC) -I $(INC) $(4) -D CBMC_WIDTH=$$w -D CBMC_SIGNS=$$signs $(FLAGS) --function harness_$(1) --unwind $$unwind $(SOLVER)"; \
	log="$(LOGDIR)/$(1).$(2).s$$signs.log"; \
	echo "== prove $(1) arm=$(2) width=$$w signs=$$signs unwind=$$unwind timeout=$(TIMEOUT)s"; \
	echo "Command: timeout $(TIMEOUT) $$cmd $(SRC_$(1))"; \
	start=$$(date +%s); set +e; \
	timeout $(TIMEOUT) $$cmd "$(SRC_$(1))" > "$$log" 2>&1; status=$$?; set -e; \
	end=$$(date +%s); \
	grep -E '^VERIFICATION|^\*\* [0-9]+ of [0-9]+ failed' "$$log" | tail -n 5 || true; \
	echo "prove $(1) arm=$(2) signs=$$signs: exit $$status after $$(( end - start ))s (log $$log)"; \
	if [ $$status -eq 124 ]; then echo "prove $(1) arm=$(2) signs=$$signs: TIMED OUT at $(TIMEOUT)s"; rc=124; \
	elif [ $$status -ne 0 ]; then rc=$$status; fi; \
	done; exit $$rc
endef

$(PROPS):
	$(call run_arm,$@,native,$(SIGNS_$@),-D CBMC_NATIVE_ARM)
	$(call run_arm,$@,emulated,$(SIGNS_$@),-D CBMC_EMULATED_ARM)
