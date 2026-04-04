
# See the README for comments on this.
DO_SIMD = 0

# X86 support
# popcnt was added in the Nehalem CPU
ARCH = -march=nehalem
# If you set DO_SIMD, adding -msse2 will be required.
#ARCH += -msse2

CFLAGS = -g -Wall -DCONVCODE_TESTS -DDO_SIMD=$(DO_SIMD) $(ARCH)

all: convcode interleave

convcode: convcode.o convcode_os_funcs.o
	gcc $(CFLAGS) -o $@ $^

convcode.o: convcode.c convcode.h convcode_os_funcs.h voyager_tab.h

convcode_os_funcs.o: convcode_os_funcs.c convcode_os_funcs.h

interleave: interleave.o
	gcc $(CFLAGS) -o $@ $^

interleave.o: interleave.c interleave.h

check: convcode
	./convcode -t
	./convcode -t -x
	./interleave -t

clean:
	rm -f convcode convcode.o convcode_os_funcs.o \
		interleave interleave.o
