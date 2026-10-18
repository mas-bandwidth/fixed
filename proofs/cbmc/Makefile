# CBMC: native __int128 arm == emulated int128 arm. Bench-only; not part of CTest or CI.
# One target per property. Each runs
#   cbmc --function <harness> --unwind <n> --unwinding-assertions --bounds-check
#        --signed-overflow-check --undefined-shift-check --slice-formula --sat-solver cadical
# under GNU timeout (CBMC 6.6.0 has no --timeout option). The bench CBMC 6.6.0 builds have
# no CaDiCaL compiled in and fall back to MiniSat 2 (CBMC says so in each log). WIDTH_<target> is the input
# narrowing results.md names (64 or 128 = the full domain, see int128_equiv.c).
# Override any of them on the command line, e.g. make fixmul WIDTH_fixmul=12 SIGNS=0 TIMEOUT=600.

CBMC ?= cbmc
TIMEOUT ?= 1200
SOLVER ?= --sat-solver cadical
LOGDIR ?= $(or $(TMPDIR),/tmp)/fixed-cbmc-int128-logs
SRC := $(CURDIR)/int128_equiv.c
INC := $(CURDIR)/../../include
FLAGS := --unwinding-assertions --bounds-check --signed-overflow-check --undefined-shift-check --slice-formula

PROPS := addsub compares shifts \
	mul_u64 mul_u64_wide mul_i64 mul_i64_wide mul_128 mul_128_wide \
	udivmod sdiv divrem64 fixmul fixmul_wide fixdiv fixdiv_wide fixdiv_big \
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
WIDTH_divrem64 := 4
SIGNS_divrem64 := 0 1 2 3 4 5 6 7
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
UNWIND_divrem64 := 130
UNWIND_fixdiv := 130
UNWIND_fixdiv_wide := 130
UNWIND_fixdiv_big := 130
UNWIND_isqrt_low := 6
UNWIND_isqrt_mid := 6
UNWIND_isqrt_mid_top := 6
UNWIND_isqrt_high := 66

.PHONY: all $(PROPS)
all: $(PROPS)

$(PROPS):
	@set -eu; mkdir -p "$(LOGDIR)"; tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	{ echo '#define CBMC_NATIVE_ARM'; cat "$(SRC)"; } > "$$tmp/native.c"; \
	{ echo '#define CBMC_EMULATED_ARM'; cat "$(SRC)"; } > "$$tmp/emulated.c"; \
	{ echo '#define CBMC_HARNESS_TU'; cat "$(SRC)"; } > "$$tmp/harness.c"; \
	unwind=$(or $(UNWIND_$@),$(UNWIND_DEFAULT)); \
	h=$(or $(HARNESS_$@),$@); wa=$(WIDTH_$@); wb=$(or $(WIDTHB_$@),$(WIDTH_$@)); rc=0; \
	for signs in $(or $(SIGNS),$(SIGNS_$@),0); do \
	cmd="$(CBMC) -I $(INC) -D CBMC_WIDTH=$$wa -D CBMC_WIDTH_B=$$wb -D CBMC_SIGNS=$$signs $(DEFS_$@) --function harness_$$h --unwind $$unwind $(FLAGS) $(SOLVER)"; \
	log="$(LOGDIR)/$@.s$$signs.log"; \
	echo "== $@ width=$$wa width_b=$$wb signs=$$signs unwind=$$unwind timeout=$(TIMEOUT)s"; \
	echo "Command: timeout $(TIMEOUT) $$cmd native.c emulated.c harness.c"; \
	start=$$(date +%s); set +e; \
	timeout $(TIMEOUT) $$cmd "$$tmp/native.c" "$$tmp/emulated.c" "$$tmp/harness.c" > "$$log" 2>&1; \
	status=$$?; set -e; end=$$(date +%s); \
	grep -E '^\*\* [0-9]+ of [0-9]+ failed|^VERIFICATION|FAILURE$$' "$$log" | tail -n 20 || true; \
	echo "$@ signs=$$signs: exit $$status after $$(( end - start ))s (log $$log)"; \
	if [ $$status -eq 124 ]; then echo "$@ signs=$$signs: TIMED OUT at $(TIMEOUT)s"; fi; \
	if [ $$status -ne 0 ]; then rc=$$status; fi; \
	done; exit $$rc
