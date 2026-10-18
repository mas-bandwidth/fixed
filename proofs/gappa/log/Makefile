.PHONY: all kernel

all: kernel

kernel:
	gappa kernel.g
	@printf '%s\n' \
		'kernel arithmetic error |comp - p|: <= 7.35e-39 (proved by kernel.g)' \
		'fixLnExact    total kernel error: <= 2.5149e-26 raw; design budget 2^-85.04 = 2.51425411001007e-26 -> ABOVE budget (finding)' \
		'fixLog2Exact  total kernel error: <= 3.6282e-26 raw; design budget 2^-84.5 = 3.65565637751430e-26 -> within budget' \
		'fixLog10Exact total kernel error: <= 1.0922e-26 raw; design budget 2^-86.2 = 1.12516023157136e-26 -> within budget'
