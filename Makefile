
CFLAGS = -g -Wall -O2 -DCONVCODE_TESTS

convcode: convcode.o convcode_os_funcs.o
	gcc $(CFLAGS) -o $@ $^

convcode.o: convcode.c convcode.h convcode_os_funcs.h

convcode_os_funcs.o: convcode_os_funcs.c convcode_os_funcs.h

check: convcode
	./convcode -t
	./convcode -t -x

clean:
	rm -f convcode convcode.o convcode_os_funcs.o
