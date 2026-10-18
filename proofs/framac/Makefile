# Frama-C WP runtime-error proofs for the integer kernels of mas-bandwidth/fixed.
# Runs nightly on a Linux bench only: no per-commit CI, no CTest entry. See README.md.
#
#   make -C proofs/framac nightly   the proved set, from a clean out/: fixmul, sqrt and
#                                   primitives, then the summary. Leaves fixDiv out (its
#                                   WP goal generation does not end; results.md)
#   make -C proofs/framac fixmul    fixMul: emulated, emulated + FIX_SATURATE, frame
#   make -C proofs/framac sqrt      fixISqrt128High: emulated, frame
#   make -C proofs/framac primitives  the 13 int128 primitives: emulated, and frame for
#                                   all but Int128Div (its frame run does not finish)
#   make -C proofs/framac all       every wrapper on the emulated 128-bit path, then the
#                                   native path, then the frame goals, then the summary
#   make -C proofs/framac emulated  emulated path only (RTE and ensures goals)
#   make -C proofs/framac native    no define: meant as the native __int128 path, but under
#                                   Frama-C 33 it reaches the emulated path (results.md)
#   make -C proofs/framac frame     assigns and terminates goals, emulated path, short timeout
#   make -C proofs/framac summary   reprint the table from the logs in out/
#
# One frama-c run per wrapper, so a goal is always reported against one function.

FRAMAC   ?= frama-c
OUT      ?= out
# -wp-cache none: every run calls the provers, so a cached timeout or proof from an earlier
# run never stands in for tonight's.
WPFLAGS  ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 4 -wp-cache none
# The RTE arms prove the runtime-error and ensures goals. The frame goals (assigns and
# terminates) are split out: the library loops carry no loop assigns and no loop variant
# (no header is edited here), so WP cannot discharge them, and at 60 s each they would
# dominate the run. The frame arm runs them at a short timeout so they are still listed.
RTEPROPS ?= -wp-prop=-@assigns,-@terminates
FRAMEFLAGS ?= -wp -wp-prover alt-ergo,z3 -wp-timeout 10 -wp-par 4 -wp-cache none -wp-prop=@assigns,@terminates
# Every frama-c run is bounded, so one wrapper cannot hold the bench.
LIMIT    ?= timeout 1800
# The row for one log: WP's summary, or why there is none (no goal, or the run was cut off).
STATUS    = { grep -E 'Proved goals:' $(1) | tail -1 | sed 's/^.*Proved goals: */Proved goals: /' | grep . || \
              { grep -q 'No goal generated' $(1) && echo '0 / 0 (no goal generated)'; } || \
              echo 'NO SUMMARY: run cut off by LIMIT or stopped (see log)'; }
# Inline every FIX_ALWAYS_INLINE helper into the wrapper that calls it, so each RTE
# goal of the seam and of the lane arithmetic is generated and proved in its calling
# context. -wp-fct then limits WP to the one wrapper.
KERNEL   ?= -inline-calls @inline -remove-inlined @inline
CPP_BASE  = -I../../include

KERNELS    = proveFixMul proveFixDiv proveFixISqrt128High
PRIMITIVES = proveInt128MulI64 proveInt128Add proveInt128Shr proveInt128ShiftLeft \
             proveUInt128Shl proveUInt128Shr proveUInt128MulU64 proveUInt128Add \
             proveUInt128Sub proveInt128Div proveInt128ToI64 proveInt128Compare \
             proveUInt128Compare
WRAPPERS   = $(KERNELS) $(PRIMITIVES)

EMU_LOGS   = $(patsubst %,$(OUT)/%.emulated.log,$(WRAPPERS)) $(OUT)/proveFixMul.emulated-saturate.log
NAT_LOGS   = $(patsubst %,$(OUT)/%.native.log,$(WRAPPERS))
FRAME_LOGS = $(patsubst %,$(OUT)/%.frame.log,$(WRAPPERS))

.PHONY: all emulated native frame summary clean nightly fixmul sqrt primitives
.NOTPARALLEL:

all: emulated native frame summary

# The proved set. fixDiv is left out: its WP goal generation does not end
# (fixed-framac-fixdiv-goal-generation). So is the Int128Div frame run, whose loop
# without annotations keeps it from finishing (fixed-framac-isqrt-loop-annotations).
FRAME_PRIMITIVES = $(filter-out proveInt128Div,$(PRIMITIVES))

fixmul: $(OUT)/proveFixMul.emulated.log $(OUT)/proveFixMul.emulated-saturate.log $(OUT)/proveFixMul.frame.log

sqrt: $(OUT)/proveFixISqrt128High.emulated.log $(OUT)/proveFixISqrt128High.frame.log

primitives: $(patsubst %,$(OUT)/%.emulated.log,$(PRIMITIVES)) $(patsubst %,$(OUT)/%.frame.log,$(FRAME_PRIMITIVES))

# Nightly on a bench only, never per commit. Starts from a clean out/ so every goal is rerun.
nightly:
	rm -rf $(OUT)
	$(MAKE) fixmul sqrt primitives
	$(MAKE) summary

# Logs depend on the wrapper file only: run `make clean` after changing a header or a flag.

emulated: $(EMU_LOGS)

native: $(NAT_LOGS)

frame: $(FRAME_LOGS)

$(OUT):
	mkdir -p $(OUT)

# Emulated path: the two-lane fixEmuInt128 arithmetic, as plain MSVC builds it.
$(OUT)/%.emulated.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" kernels_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s%s\n' $* emulated "$$($(call STATUS,$@))" "$$(grep -qiE 'prover .*(not|un)(found|available|known)|no prover' $@ && echo ' PROVER MISSING: Qed only')"

# fixMul under FIX_SATURATE, the saturating tail.
$(OUT)/proveFixMul.emulated-saturate.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128 -DFIX_SATURATE" kernels_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct proveFixMul > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' proveFixMul emulated-saturate "$$($(call STATUS,$@))"

# Frame goals (assigns, terminates), emulated path.
$(OUT)/%.frame.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" kernels_acsl.c \
		$(KERNEL) $(FRAMEFLAGS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* frame "$$($(call STATUS,$@))"

# Native path: compiler __int128, selected by fixed_int128.h only when __SIZEOF_INT128__ is
# defined. Frama-C 33's preprocessing does not appear to define it, so this arm currently
# repeats the emulated path (results.md); a parse error, as Frama-C 25 gave, is reported.
$(OUT)/%.native.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE)" kernels_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* native "$$($(call STATUS,$@))$$(grep -q 'syntax error' $@ && echo 'front end rejects __int128 (see log)')"

summary:
	@echo "wrapper                  arm                proved / total"
	@for f in $(OUT)/*.log; do \
		b=$$(basename $$f .log); fn=$${b%%.*}; arm=$${b#*.}; \
		line=$$($(call STATUS,$$f)); \
		grep -q 'syntax error' $$f && line="not run: front end rejects __int128"; \
		grep -qiE 'prover .*(not|un)(found|available|known)|no prover' $$f && line="$$line PROVER MISSING: Qed only"; \
		printf '%-24s %-18s %s\n' $$fn $$arm "$$line"; \
	done

clean:
	rm -rf $(OUT)
