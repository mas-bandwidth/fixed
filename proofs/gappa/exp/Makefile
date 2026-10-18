.PHONY: all kernel

all: kernel

kernel:
	gappa kernel.g
	@printf '%s\n' \
	  'exp        kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 1.143247004185311e-12 (2^-39.67 raw), budget 2^-39.67' \
	  'sinh/cosh  kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 1.7089819252105546e-12 (2^-39.09 raw), budget 2^-39.09' \
	  'tanh       kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 8.123258540033696e-27 (2^-86.67 raw), budget 2^-86.67'
