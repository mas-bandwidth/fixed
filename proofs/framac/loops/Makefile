# Frama-C WP loop-annotation proofs for fixISqrt128High and the emulated
# fixInt128Div chain. Runs nightly on a Linux bench only: no per-commit CI, no
# CTest entry. See results.md.
#
#   make -C proofs/framac/loops sqrt     fixISqrt128High: emulated (RTE + loop) and frame (assigns + terminates)
#   make -C proofs/framac/loops prims    Int128Div: emulated (RTE + loop) and frame (assigns + terminates)
#   make -C proofs/framac/loops nightly  both, from a clean out/, then the summary
#   make -C proofs/framac/loops summary  reprint the table from the logs in out/
#
# wrappers_acsl.c copies the two functions and the divide chain, with the loop
# annotations the headers do not carry. loops_acsl.acsl defines the 128-bit
# value used as the shift-subtract variants. No library header is edited.

FRAMAC     ?= frama-c
OUT        ?= out
WPFLAGS    ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 4 -wp-cache none
RTEPROPS   ?= -wp-prop=-@assigns,-@terminates
FRAMEFLAGS ?= -wp -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 4 -wp-cache none -wp-prop=@assigns,@terminates
# Inline every FIX_ALWAYS_INLINE helper into the function under proof, so the RTE
# goals of the lane arithmetic are generated in the calling context and the divide
# chain keeps its annotated loop when it is inlined.
KERNEL     ?= -inline-calls @inline -remove-inlined @inline
CPP_BASE    = -I../../../include
EMU         = -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128"
# Every frama-c run is bounded, so one function cannot hold the bench.
LIMIT      ?= timeout 1800
STATUS      = { grep -E 'Proved goals:' $(1) | tail -1 | sed 's/^.*Proved goals: */Proved goals: /' | grep . || \
                { grep -q 'No goal generated' $(1) && echo '0 / 0 (no goal generated)'; } || \
                echo 'NO SUMMARY: run cut off by LIMIT or stopped (see log)'; }

.PHONY: all sqrt prims nightly summary clean
.NOTPARALLEL:

all: sqrt prims summary

sqrt: $(OUT)/proveFixISqrt128High.emulated.log $(OUT)/proveFixISqrt128High.frame.log

prims: $(OUT)/proveInt128Div.emulated.log $(OUT)/proveInt128Div.frame.log

# Nightly on a bench only, never per commit. Starts from a clean out/ so every goal is rerun.
nightly:
	rm -rf $(OUT)
	$(MAKE) sqrt prims
	$(MAKE) summary

$(OUT):
	mkdir -p $(OUT)

# Logs depend on the wrapper and the ACSL file only: run `make clean`
# after changing a header or a flag.
$(OUT)/%.emulated.log: wrappers_acsl.c loops_acsl.acsl | $(OUT)
	$(LIMIT) $(FRAMAC) $(EMU) wrappers_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* emulated "$$($(call STATUS,$@))"

$(OUT)/%.frame.log: wrappers_acsl.c loops_acsl.acsl | $(OUT)
	$(LIMIT) $(FRAMAC) $(EMU) wrappers_acsl.c \
		$(KERNEL) $(FRAMEFLAGS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* frame "$$($(call STATUS,$@))"

summary:
	@echo "wrapper                  arm                proved / total"
	@for f in $(OUT)/*.log; do \
		[ -e "$$f" ] || continue; \
		b=$$(basename $$f .log); fn=$${b%%.*}; arm=$${b#*.}; \
		printf '%-24s %-18s %s\n' $$fn $$arm "$$($(call STATUS,$$f))"; \
	done

clean:
	rm -rf $(OUT)
