# CBMC: native __int128 arm == emulated int128 arm. Bench-only; not part of CTest or CI.
# One target per property. Each runs
#   cbmc --function <harness> --unwind <n> --unwinding-assertions --bounds-check
#        --signed-overflow-check --undefined-shift-check --slice-formula
#        --external-sat-solver cadical
# under GNU timeout (CBMC 6.6.0 has no --timeout option). The bench CBMC 6.6.0 builds have
# no CaDiCaL compiled in, so CaDiCaL runs as an external DIMACS solver (`cadical` on PATH).
# WIDTH_<target> is the input narrowing results.md names (64 or 128 = the full domain,
# see int128_equiv.c).
# `make plant` reruns every property and sign run with -D CBMC_PLANT (a fault planted in
# the emulated arm) and --stop-on-fail, and passes only if each run's violated property is
# an "arms agree" assertion: every proof can fail, and no sign run is vacuous.
# Override any of them on the command line, e.g. make fixmul WIDTH_fixmul=12 SIGNS=0 TIMEOUT=600.

CBMC ?= cbmc
TIMEOUT ?= 1200
SOLVER ?= --external-sat-solver cadical
PLANT_TIMEOUT ?= 600
LOGDIR ?= $(or $(TMPDIR),/tmp)/fixed-cbmc-int128-logs
SRC := $(CURDIR)/int128_equiv.c
INC := $(CURDIR)/../../include
FLAGS := --unwinding-assertions --bounds-check --signed-overflow-check --undefined-shift-check --slice-formula

PROPS := addsub compares shifts \
	mul_u64 mul_u64_wide mul_i64 mul_i64_wide mul_128 mul_128_wide \
	udivmod sdiv sdiv_min divrem64 fixmul fixmul_wide fixdiv fixdiv_wide fixdiv_big \
	isqrt_low isqrt_mid isqrt_mid_top isqrt_high

# Per property: WIDTH_ (every input, or the first when WIDTHB_ is set), WIDTHB_ (the
# second input), SIGNS_ (the CBMC_SIGNS combinations run; the union of the runs is the
# domain results.md states), UNWIND_ and DEFS_. 64 for a 64-bit input or 128 for a
# 128-bit input is the full domain. These are the narrowings results.md records.
HARNESS_mul_u64_wide := mul_u64
HARNESS_mul_i64_wide := mul_i64
HARNESS_mul_128_wide := mul_128
HARNESS_fixmul_wide := fixmul
HARNESS_fixdiv_wide := fixdiv
HARNESS_isqrt_mid_top := isqrt_mid
WIDTH_addsub := 128
WIDTH_compares := 128
WIDTH_shifts := 128
WIDTH_mul_u64 := 8
SIGNS_mul_u64 := 0 1 2 3
WIDTH_mul_u64_wide := 64
WIDTHB_mul_u64_wide := 4
WIDTH_mul_i64 := 8
SIGNS_mul_i64 := 0 1 2 3
WIDTH_mul_i64_wide := 64
WIDTHB_mul_i64_wide := 4
WIDTH_mul_128 := 6
SIGNS_mul_128 := 0 1 2 3
WIDTH_mul_128_wide := 128
WIDTHB_mul_128_wide := 2
WIDTH_udivmod := 6
SIGNS_udivmod := 0 1 2 3
WIDTH_sdiv := 8
SIGNS_sdiv := 0 1 2 3
WIDTH_sdiv_min := 8
SIGNS_sdiv_min := 0 1
WIDTH_divrem64 := 4
# hi < d: runs 1 and 3 (hi ones-extended, d zero-extended) leave no input; not run.
SIGNS_divrem64 := 0 2 4 5 6 7
WIDTH_fixmul := 8
SIGNS_fixmul := 0 1 2 3
WIDTH_fixmul_wide := 64
WIDTHB_fixmul_wide := 4
WIDTH_fixdiv := 8
SIGNS_fixdiv := 0 1 2 3
WIDTH_fixdiv_wide := 64
WIDTHB_fixdiv_wide := 2
WIDTH_fixdiv_big := 4
SIGNS_fixdiv_big := 0 1 2 3
WIDTH_isqrt_low := 8
WIDTH_isqrt_mid := 8
WIDTH_isqrt_mid_top := 8
DEFS_isqrt_mid_top := -D CBMC_SQRT_BASE=0xFFFFFFFFFFF00
WIDTH_isqrt_high := 8
SIGNS_isqrt_high := 0 1 2 3
UNWIND_DEFAULT := 12
UNWIND_udivmod := 130
UNWIND_sdiv := 130
UNWIND_sdiv_min := 130
UNWIND_divrem64 := 130
UNWIND_fixdiv := 130
UNWIND_fixdiv_wide := 130
UNWIND_fixdiv_big := 130
UNWIND_isqrt_low := 6
UNWIND_isqrt_mid := 6
UNWIND_isqrt_mid_top := 6
UNWIND_isqrt_high := 66

PLANTS := $(addprefix plant-,$(PROPS))
# The seed model's own checks, each shown able to fail: a window of 0 must violate a
# window check, and a bound of 0 must violate the seed lemma.
SEED_PLANTS := plant-seed-window-low plant-seed-window-mid plant-seed-bound-low plant-seed-bound-mid

.PHONY: all plant $(PROPS) $(PLANTS) $(SEED_PLANTS)
all: $(PROPS)
plant: $(PLANTS) $(SEED_PLANTS)

# run_prop <target> <extra flags> <timeout> <mode: prove|plant> <plant: the violated property expected>
define run_prop
	@set -eu; mkdir -p "$(LOGDIR)"; tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	{ echo '#define CBMC_NATIVE_ARM'; cat "$(SRC)"; } > "$$tmp/native.c"; \
	{ echo '#define CBMC_EMULATED_ARM'; cat "$(SRC)"; } > "$$tmp/emulated.c"; \
	{ echo '#define CBMC_HARNESS_TU'; cat "$(SRC)"; } > "$$tmp/harness.c"; \
	unwind=$(or $(UNWIND_$(1)),$(UNWIND_DEFAULT)); \
	h=$(or $(HARNESS_$(1)),$(1)); wa=$(WIDTH_$(1)); wb=$(or $(WIDTHB_$(1)),$(WIDTH_$(1))); rc=0; \
	for signs in $(or $(SIGNS),$(SIGNS_$(1)),0); do \
	cmd="$(CBMC) -I $(INC) -D CBMC_WIDTH=$$wa -D CBMC_WIDTH_B=$$wb -D CBMC_SIGNS=$$signs $(DEFS_$(1)) $(2) --function harness_$$h --unwind $$unwind $(FLAGS) $(SOLVER)"; \
	log="$(LOGDIR)/$@.s$$signs.log"; \
	echo "== $(4) $(1) width=$$wa width_b=$$wb signs=$$signs unwind=$$unwind timeout=$(3)s"; \
	echo "Command: timeout $(3) $$cmd native.c emulated.c harness.c"; \
	start=$$(date +%s); set +e; \
	timeout $(3) $$cmd "$$tmp/native.c" "$$tmp/emulated.c" "$$tmp/harness.c" > "$$log" 2>&1; \
	status=$$?; set -e; end=$$(date +%s); \
	grep -E '^\*\* [0-9]+ of [0-9]+ failed|^VERIFICATION|FAILURE$$' "$$log" | tail -n 20 || true; \
	echo "$(4) $(1) signs=$$signs: exit $$status after $$(( end - start ))s (log $$log)"; \
	if [ $$status -eq 124 ]; then echo "$(4) $(1) signs=$$signs: TIMED OUT at $(3)s"; rc=124; \
	elif [ "$(4)" = plant ]; then \
	  if grep -A4 '^Violated property' "$$log" | grep -q '$(5)'; then echo "plant $(1) signs=$$signs: CAUGHT ($(5) is violated)"; \
	  else echo "plant $(1) signs=$$signs: NOT CAUGHT (vacuous or unreachable)"; rc=1; fi; \
	elif [ $$status -ne 0 ]; then rc=$$status; fi; \
	done; exit $$rc
endef

$(PROPS):
	$(call run_prop,$@,,$(TIMEOUT),prove)

$(PLANTS): plant-%:
	$(call run_prop,$*,-D CBMC_PLANT --stop-on-fail,$(PLANT_TIMEOUT),plant,arms agree)

plant-seed-window-low:
	$(call run_prop,isqrt_low,-D CBMC_SEED_WINDOW=0 --stop-on-fail,$(PLANT_TIMEOUT),plant,window)
plant-seed-window-mid:
	$(call run_prop,isqrt_mid,-D CBMC_SEED_WINDOW=0 --stop-on-fail,$(PLANT_TIMEOUT),plant,window)
plant-seed-bound-low:
	$(call run_prop,isqrt_low,-D CBMC_SEED_BOUND=0 --stop-on-fail,$(PLANT_TIMEOUT),plant,seed within)
plant-seed-bound-mid:
	$(call run_prop,isqrt_mid,-D CBMC_SEED_BOUND=0 --stop-on-fail,$(PLANT_TIMEOUT),plant,seed within)
