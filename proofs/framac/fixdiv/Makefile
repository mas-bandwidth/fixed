# Frama-C WP runtime-error proof for fixDiv (mas-bandwidth/fixed).
# Runs nightly on a Linux bench only: no per-commit CI, no CTest entry.
#
#   make -C proofs/framac/fixdiv fixdiv   bounded run; prints the summary line
#   make -C proofs/framac/fixdiv clean    remove out/
#
# nightly deliberately does NOT call fixdiv: goal generation has never been shown to end
# within the cap (notes.md). Add it here only after a bench run shows a summary line inside
# LIMIT.

FRAMAC   ?= frama-c
OUT      ?= out
# No -inline-calls: keeping fixEmuUInt128DivMod (fixed_int128.h:417, 128-iteration loop
# with no loop assigns and no variant) out of the proveFixDiv VC is the point of this
# wrapper. Its own goals are proveInt128Div's, run by the main card.
WPFLAGS  ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 4 -wp-cache none \
            -wp-prop=-@assigns,-@terminates
CPP_BASE  = -I../../../include
LIMIT    ?= timeout 600
STATUS    = { grep -E 'Proved goals:' $(1) | tail -1 | sed 's/^.*Proved goals: */Proved goals: /' | grep . || \
              echo 'NO SUMMARY: run cut off by LIMIT or stopped (see log)'; }

.PHONY: fixdiv nightly clean

# Emulated 128-bit path, one function, bounded by LIMIT (600 s).
fixdiv: | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" fixdiv_acsl.c \
		$(WPFLAGS) -wp-fct proveFixDiv > $(OUT)/fixdiv.emulated.log 2>&1 || true
	@printf '%-24s %-18s %s\n' proveFixDiv emulated "$$($(call STATUS,$(OUT)/fixdiv.emulated.log))"

nightly:
	@echo "fixdiv: not run; generation has not been shown to end within $(LIMIT) (notes.md)"

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
