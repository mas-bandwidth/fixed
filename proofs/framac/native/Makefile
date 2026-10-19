# Frama-C WP proofs for the NATIVE __int128 path of mas-bandwidth/fixed.
# Runs nightly on a Linux bench only: no per-commit CI, no CTest entry, no CI file.
# See results.md. The main card's proofs/framac/nightly calls `make -C native nightly`.
#
#   make -C proofs/framac/native native   the native-path wrappers, then the summary
#   make -C proofs/framac/native nightly  same from a clean out/, for the main nightly
#   make -C proofs/framac/native branch   print the macros that select the native branch
#   make -C proofs/framac/native divq     just the two divq stub-contract wrappers
#   make -C proofs/framac/native summary  reprint the table from the logs in out/
#   make -C proofs/framac/native clean    remove out/
#
# One frama-c run per wrapper, so a goal is always reported against one function.

FRAMAC   ?= frama-c
MACHDEP  ?= -machdep gcc_x86_64
CC       ?= gcc
OUT      ?= out
# -wp-cache none: every run calls the provers, so a cached timeout or proof from an
# earlier run never stands in for tonight's.
WPFLAGS  ?= -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60 -wp-par 1 -wp-cache none
# The RTE arms prove the runtime-error and ensures goals. The frame goals are not part
# of this card (the library loops carry no loop assigns or variant; see the main card's
# results.md), so they are left out here.
RTEPROPS ?= -wp-prop=-@assigns,-@terminates
# Every frama-c run is bounded, so one wrapper cannot hold the bench.
LIMIT    ?= timeout 600s prlimit --as=8589934592 --
# The row for one log: WP's summary, or why there is none (no goal, or the run was cut
# off), or the front end's refusal of __int128.
STATUS    = { grep -E 'Proved goals:' $(1) | tail -1 | sed 's/^.*Proved goals: */Proved goals: /' | grep . || \
              { grep -q 'No goal generated' $(1) && echo '0 / 0 (no goal generated)'; } || \
              echo 'NO SUMMARY: run cut off by LIMIT or stopped (see log)'; }
# Inline every FIX_ALWAYS_INLINE helper into the wrapper that calls it, so each RTE goal
# of the seam and of the native lane arithmetic is generated and proved in its calling
# context. -wp-fct then limits WP to the one wrapper.
KERNEL   ?= -inline-calls @inline -remove-inlined @inline
CPP_BASE  = -I../../../include
# -D__SIZEOF_INT128__=16 is the whole point: Frama-C's preprocessing does not define it,
# so without it fixed_int128.h selects the emulated pair (the main card's silent bug).
# __x86_64__ stays defined: undefining it makes the system headers select 32-bit stubs.
# The signed divq asm is therefore in the front end for proveInt128Div; its contract is
# in int128_native_stub.h, and the INT128_MIN / -1 undefined behaviour is shown with a
# C program under UBSan in results.md rather than by forcing the generic fallback.
NATIVE_CPP = $(CPP_BASE) -D__SIZEOF_INT128__=16

PRIMITIVES = proveInt128MulI64 proveInt128Add proveInt128Shr proveInt128ShiftLeft \
             proveUInt128Shl proveUInt128Shr proveUInt128MulU64 proveUInt128Add \
             proveUInt128Sub proveInt128Div proveInt128ToI64 proveInt128Compare \
             proveUInt128Compare
WRAPPERS   = proveFixMul proveFixISqrt128High $(PRIMITIVES)
DIVQ       = proveUInt128DivRemBy64 proveInt128Divq

NAT_LOGS   = $(patsubst %,$(OUT)/%.native.log,$(WRAPPERS)) $(patsubst %,$(OUT)/%.native.log,$(DIVQ)) \
             $(OUT)/proveFixMul.native-saturate.log

.PHONY: all native nightly branch divq summary clean
.NOTPARALLEL:

all: native

native: branch $(NAT_LOGS)
	@$(MAKE) --no-print-directory summary

# The main card's nightly calls this. Clean first, so every goal is rerun.
nightly:
	rm -rf $(OUT)
	@$(MAKE) --no-print-directory native

# Proof that the native branch is the one compiled. FIX_INT128_EMULATED is 0 only in the
# __SIZEOF_INT128__ branch of fixed_int128.h:567, and the #error in native_acsl.c fails a
# Frama-C run that gets this wrong.
branch:
	@echo "native branch selection (-D__SIZEOF_INT128__=16, FIX_FORCE_EMULATED_INT128 unset):"
	@$(CC) -E -dM $(CPP_BASE) -D__SIZEOF_INT128__=16 native_acsl.c | \
		grep -E '^#define (__SIZEOF_INT128__|FIX_HAS_INT128|FIX_INT128_EMULATED) '

$(OUT):
	mkdir -p $(OUT)

# Native path: compiler __int128, selected by fixed_int128.h only when __SIZEOF_INT128__
# is defined. A parse error from the front end is reported, not hidden.
# The sqrt seed carries 11 floating-point RTE goals; bound each prover call so this
# wrapper still produces a complete summary within the 600-second process cap.
$(OUT)/proveFixISqrt128High.native.log: native_acsl.c int128_native_stub.h | $(OUT)
	$(LIMIT) $(FRAMAC) $(MACHDEP) -cpp-extra-args="$(NATIVE_CPP)" native_acsl.c \
		$(KERNEL) $(subst -wp-timeout 60,-wp-timeout 10,$(WPFLAGS)) $(RTEPROPS) -wp-fct proveFixISqrt128High > $@ 2>&1 || true
	@printf "%-28s %-8s %s\\n" proveFixISqrt128High native "$$($(call STATUS,$@))"

$(OUT)/%.native.log: native_acsl.c int128_native_stub.h | $(OUT)
	$(LIMIT) $(FRAMAC) $(MACHDEP) -cpp-extra-args="$(NATIVE_CPP)" native_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct $* > $@ 2>&1 || true
	@printf '%-28s %-8s %s%s\n' $* native "$$($(call STATUS,$@))" "$$(grep -qiE 'syntax error|unknown type name .__int128|unsupported.*__int128' $@ && echo ' FRONT END REJECTS __int128 (see log)')"

# fixMul under FIX_SATURATE, the saturating tail, on the native arm.
$(OUT)/proveFixMul.native-saturate.log: native_acsl.c int128_native_stub.h | $(OUT)
	$(LIMIT) $(FRAMAC) $(MACHDEP) -cpp-extra-args="$(NATIVE_CPP) -DFIX_SATURATE" native_acsl.c \
		$(KERNEL) $(WPFLAGS) $(RTEPROPS) -wp-fct proveFixMul > $@ 2>&1 || true
	@printf '%-28s %-8s %s%s\n' proveFixMul native-saturate "$$($(call STATUS,$@))" "$$(grep -qiE 'syntax error|unsupported.*__int128' $@ && echo ' FRONT END REJECTS __int128 (see log)')"

# The divq stub contract is a declaration only (no library header), so this translation
# unit proves that the documented precondition implies the documented quotient and
# remainder. It does not and cannot prove the asm body; that is a trusted assumption,
# recorded in results.md.
$(OUT)/proveUInt128DivRemBy64.native.log: native_acsl.c int128_native_stub.h | $(OUT)
	$(LIMIT) $(FRAMAC) $(MACHDEP) -cpp-extra-args="$(CPP_BASE) -DFIX_NATIVE_STUB_ONLY" native_acsl.c \
		$(WPFLAGS) $(RTEPROPS) -wp-fct proveUInt128DivRemBy64 > $@ 2>&1 || true
	@printf '%-28s %-8s %s%s\n' proveUInt128DivRemBy64 native "$$($(call STATUS,$@))" "$$(grep -qiE 'syntax error|unsupported.*__int128' $@ && echo ' FRONT END REJECTS __int128 (see log)')"

$(OUT)/proveInt128Divq.native.log: native_acsl.c int128_native_stub.h | $(OUT)
	$(LIMIT) $(FRAMAC) $(MACHDEP) -cpp-extra-args="$(CPP_BASE) -DFIX_NATIVE_STUB_ONLY" native_acsl.c \
		$(WPFLAGS) $(RTEPROPS) -wp-fct proveInt128Divq > $@ 2>&1 || true
	@printf '%-28s %-8s %s%s\n' proveInt128Divq native "$$($(call STATUS,$@))" "$$(grep -qiE 'syntax error|unsupported.*__int128' $@ && echo ' FRONT END REJECTS __int128 (see log)')"

divq: $(OUT)/proveUInt128DivRemBy64.native.log $(OUT)/proveInt128Divq.native.log

summary:
	@echo "wrapper                      arm      proved / total"
	@for f in $(OUT)/*.log; do \
		b=$$(basename $$f .log); fn=$${b%%.*}; arm=$${b#*.}; \
		line=$$($(call STATUS,$$f)); \
		grep -qiE 'syntax error|unknown type name .__int128|unsupported.*__int128' $$f && line="not run: front end rejects __int128"; \
		grep -qiE 'prover .*(not|un)(found|available|known)|no prover' $$f && line="$$line PROVER MISSING: Qed only"; \
		printf '%-28s %-8s %s\n' $$fn $$arm "$$line"; \
	done

clean:
	rm -rf $(OUT)
