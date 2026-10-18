.PHONY: all kernel

all: kernel

kernel:
	gappa kernel.g
	@printf '%s\n' \
		'design budgets, raw (docs/design/transcendentals.md 2.1-2.3), against ln_full_raw, log2_raw, log10_raw above:' \
		'  fixLnExact    2^-85.039 = 2.51599746228771e-26' \
		'  fixLog2Exact  2^-84.5  = 3.65565637751430e-26' \
		'  fixLog10Exact 2^-86.2  = 1.12516023157136e-26'
