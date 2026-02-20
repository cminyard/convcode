
CFLAGS = -g -Wall -O2 -DCONVCODE_TESTS

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
