# Frama-C WP proof of the production fixDiv, Linux nightly only.
FRAMAC ?= frama-c
OUT ?= out
LIMIT ?= timeout 600
CPP_BASE = -I../../../include
# Selectively inline fixDiv, not every @inline helper. The latter pulls the
# 128-iteration emulated divider into one VC and blocks goal generation.
WPFLAGS ?= -inline-calls fixDiv -wp -wp-rte -wp-prover alt-ergo,z3 \
           -wp-timeout 30 -wp-par 1 -wp-cache none \
           -wp-prop=-@assigns,-@terminates

.PHONY: fixdiv nightly clean

fixdiv: | $(OUT)
	@$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" \
		fixdiv_acsl.c $(WPFLAGS) -wp-fct proveFixDiv > $(OUT)/fixdiv.emulated.log 2>&1; \
	status=$$?; \
	summary=$$(grep 'Proved goals:' $(OUT)/fixdiv.emulated.log | tail -1); \
	if test -n "$$summary"; then echo "$$summary"; \
	else echo 'NO SUMMARY: see $(OUT)/fixdiv.emulated.log'; status=1; fi; \
	exit $$status

nightly: fixdiv

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
