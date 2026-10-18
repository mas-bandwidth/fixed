# Frama-C WP runtime-error proofs for the integer kernels of mas-bandwidth/fixed.
# Runs on a Linux bench only, outside CTest and CI. See README.md.
#
#   make -C proofs/framac all       every wrapper on the emulated 128-bit path, then the
#                                   native path, then the frame goals, then the summary
#   make -C proofs/framac emulated  emulated path only (RTE and ensures goals)
#   make -C proofs/framac native    native __int128 path only (RTE and ensures goals)
#   make -C proofs/framac frame     assigns and terminates goals, emulated path, short timeout
#   make -C proofs/framac summary   reprint the table from the logs in out/
#
# One frama-c run per wrapper, so a goal is always reported against one function.

FRAMAC   ?= frama-c
OUT      ?= out
WPFLAGS  ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 4
# The RTE arms prove the runtime-error and ensures goals. The frame goals (assigns and
# terminates) are split out: the library loops carry no loop assigns and no loop variant
# (no header is edited here), so WP cannot discharge them, and at 60 s each they would
# dominate the run. The frame arm runs them at a short timeout so they are still listed.
RTEPROPS ?= -wp-prop=-@assigns,-@terminates
FRAMEFLAGS ?= -wp -wp-prover alt-ergo,z3 -wp-timeout 10 -wp-par 4 -wp-prop=@assigns,@terminates
# Every frama-c run is bounded, so one wrapper cannot hold the bench.
LIMIT    ?= timeout 1800
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

.PHONY: all emulated native frame summary clean
.NOTPARALLEL:

all: emulated native frame summary

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
	@printf '%-24s %-18s %s%s\n' $* emulated "$$(grep -E 'Proved goals:' $@ | tail -1 | sed 's/^ *//')" "$$(grep -qiE 'prover .*(not|un)(found|available|known)|no prover' $@ && echo ' PROVER MISSING: Qed only')"

# fixMul under FIX_SATURATE, the saturating tail.
$(OUT)/proveFixMul.emulated-saturate.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128 -DFIX_SATURATE" kernels_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct proveFixMul > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' proveFixMul emulated-saturate "$$(grep -E 'Proved goals:' $@ | tail -1 | sed 's/^ *//')"

# Frame goals (assigns, terminates), emulated path.
$(OUT)/%.frame.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE) -DFIX_FORCE_EMULATED_INT128" kernels_acsl.c \
		$(KERNEL) $(FRAMEFLAGS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* frame "$$(grep -E 'Proved goals:' $@ | tail -1 | sed 's/^ *//')"

# Native path: compiler __int128. The front end must accept the type for this to run;
# when it does not, the log holds the parse error and the summary says so.
$(OUT)/%.native.log: kernels_acsl.c | $(OUT)
	$(LIMIT) $(FRAMAC) -cpp-extra-args="$(CPP_BASE)" kernels_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-24s %-18s %s\n' $* native "$$(grep -E 'Proved goals:' $@ | tail -1 | sed 's/^ *//')$$(grep -q 'syntax error' $@ && echo 'front end rejects __int128 (see log)')"

summary:
	@echo "wrapper                  arm                proved / total"
	@for f in $(OUT)/*.log; do \
		b=$$(basename $$f .log); fn=$${b%%.*}; arm=$${b#*.}; \
		line=$$(grep -E 'Proved goals:' $$f | tail -1 | sed 's/^.*Proved goals: *//'); \
		grep -q 'syntax error' $$f && line="not run: front end rejects __int128"; \
		grep -qiE 'prover .*(not|un)(found|available|known)|no prover' $$f && line="$$line PROVER MISSING: Qed only"; \
		printf '%-24s %-18s %s\n' $$fn $$arm "$${line:-no WP summary (see $$f)}"; \
	done

clean:
	rm -rf $(OUT)
