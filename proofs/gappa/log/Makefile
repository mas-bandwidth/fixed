.PHONY: all kernel

all: kernel

kernel:
	gappa kernel.g
	@printf '%s\n' \
		'log kernel arithmetic error: <= 1.5e-38 (proved by kernel.g)' \
		'fixLnExact conservative raw bound: <= 2.515e-26; design budget: 2^-85.04' \
		'fixLog2Exact conservative raw bound: <= 3.63e-26; design budget: 2^-84.5' \
		'fixLog10Exact conservative raw bound: <= 1.10e-26; design budget: 2^-86.2'
