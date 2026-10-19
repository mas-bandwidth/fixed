# Frama-C WP proof of the production fixDiv, Linux nightly only.
FRAMAC ?= frama-c
OUT ?= out
LIMIT ?= timeout 600
CPP_BASE = -I../../../include
# Selectively inline fixDiv, not every @inline helper. The latter pulls the
# 128-iteration emulated divider into one VC and blocks goal generation.
WPFLAGS ?= -inline-calls fixDiv -wp -wp-rte -wp-prover alt-ergo,z3 \
           -wp-timeout 30 -wp-par 1 -wp-cache none

# Helper contracts (helpers_acsl.h). PROVED_HELPERS lists every helper whose whole body WP
# proves, as <plant id>:<name>; FIX_PLANT(id) in helpers_acsl.h falsifies that helper's main
# ensures for the `planted` target. The six ASSUMED helpers (fixEmuUInt128DivMod,
# fixEmuInt128DivMod, fixEmuInt128IsNegative, fixEmuUInt128Shl, fixShiftLeft,
# fixInt128ShiftLeft) are not listed; results.md gives each one's reason.
PROVED_HELPERS = 1:fixEmuUInt128Make 2:fixEmuInt128Make 3:fixEmuUInt128FromU64 \
                 4:fixEmuInt128FromI64 5:fixInt128FromI64 6:fixEmuToUnsigned 7:fixEmuToSigned \
                 8:fixInt128ToUnsigned 9:fixInt128FromUnsigned 10:fixEmuInt128ToI64 \
                 11:fixInt128ToI64 12:fixEmuUInt128Or 14:fixUInt128Shl 15:fixEmuUInt128Sub \
                 16:fixEmuUInt128Neg 20:fixEmuInt128Div 21:fixInt128Div 23:fixEmuUInt128Lt \
                 24:fixEmuInt128Lt 25:fixEmuInt128Gt 26:fixInt128Lt 27:fixInt128Gt 28:fixEmuUInt128Ge
PROVED_NAMES = $(foreach e,$(PROVED_HELPERS),$(word 2,$(subst :, ,$(e))))
HELPER_WPFLAGS ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 30 -wp-par 1 -wp-cache none
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
COMMA := ,

.PHONY: fixdiv helpers planted planted-bug nightly clean

fixdiv: | $(OUT)
	@$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" \
		fixdiv_acsl.c $(WPFLAGS) -wp-fct proveFixDiv \
		-wp-report-json $(OUT)/fixdiv.json > $(OUT)/fixdiv.emulated.log 2>&1; \
	status=$$?; \
	summary=$$(grep 'Proved goals:' $(OUT)/fixdiv.emulated.log | tail -1); \
	if test -n "$$summary"; then echo "$$summary"; \
	else echo 'NO SUMMARY: see $(OUT)/fixdiv.emulated.log'; status=1; fi; \
	./strict_check.sh $(OUT)/fixdiv.json || status=1; \
	exit $$status

# A single false wrapper ensures is a negative control. WP plus strict_check must
# name its actual non-Valid verdict and this target deliberately exits nonzero.
planted-bug: | $(OUT)
	@$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" \
		planted_bug_acsl.c $(WPFLAGS) -wp-fct proveFixDiv \
		-wp-report-json $(OUT)/planted-bug.json > $(OUT)/planted-bug.log 2>&1; \
	status=$$?; \
	summary=$$(grep 'Proved goals:' $(OUT)/planted-bug.log | tail -1); \
	if test -n "$$summary"; then echo "$$summary"; \
	else echo 'NO SUMMARY: see $(OUT)/planted-bug.log'; status=1; fi; \
	./strict_check.sh $(OUT)/planted-bug.json || status=1; \
	exit $$status

# Prove every PROVED helper body against its contract (callees by their contracts). Frama-C
# exits 0 with goals open, so the target fails unless every goal is proved.
helpers: | $(OUT)
	@$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" \
		fixdiv_acsl.c $(HELPER_WPFLAGS) -wp-fct $(subst $(SPACE),$(COMMA),$(strip $(PROVED_NAMES))) \
		> $(OUT)/helpers.log 2>&1; \
	status=$$?; \
	summary=$$(grep 'Proved goals:' $(OUT)/helpers.log | tail -1); \
	if test -n "$$summary"; then echo "$$summary"; \
		line=$$(echo "$$summary" | sed 's/.*Proved goals: *//'); \
		if test "$${line%% /*}" != "$${line##*/ }"; then echo 'NOT ALL PROVED: see $(OUT)/helpers.log'; status=1; fi; \
	else echo 'NO SUMMARY: see $(OUT)/helpers.log'; status=1; fi; \
	exit $$status

# Planted faults: helper k is proved with FIX_PLANT(k) falsifying its main ensures, then
# proveFixDiv with plants 100 and 101. Each run must leave a goal unproved; a run that
# proves everything means that proof is vacuous. Not in nightly: it runs about 25 provers.
planted: | $(OUT)
	@fail=0; for e in $(PROVED_HELPERS); do k=$${e%%:*}; f=$${e#*:}; \
		$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128 -DFIX_ACSL_PLANT=$$k" \
			fixdiv_acsl.c $(HELPER_WPFLAGS) -wp-fct $$f > $(OUT)/planted.$$k.log 2>&1; \
		line=$$(grep 'Proved goals:' $(OUT)/planted.$$k.log | tail -1 | sed 's/.*Proved goals: *//'); \
		got=$${line%% /*}; all=$${line##*/ }; \
		if test -n "$$line" && test "$$got" -lt "$$all"; then echo "planted $$k $$f: fails as required ($$line)"; \
		else echo "planted $$k $$f: NOT CAUGHT ($$line)"; fail=1; fi; \
	done; \
	for k in 100 101; do \
		$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128 -DFIX_ACSL_PLANT=$$k" \
			fixdiv_acsl.c $(WPFLAGS) -wp-fct proveFixDiv > $(OUT)/planted.$$k.log 2>&1; \
		line=$$(grep 'Proved goals:' $(OUT)/planted.$$k.log | tail -1 | sed 's/.*Proved goals: *//'); \
		got=$${line%% /*}; all=$${line##*/ }; \
		if test -n "$$line" && test "$$got" -lt "$$all"; then echo "planted $$k proveFixDiv: fails as required ($$line)"; \
		else echo "planted $$k proveFixDiv: NOT CAUGHT ($$line)"; fail=1; fi; \
	done; exit $$fail

nightly: helpers fixdiv

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
