
CFLAGS = -g -Wall -O2

convcode: convcode.c convcode.h
	gcc $(CFLAGS) -DCONVCODE_TESTS -o convcode convcode.c

check: convcode
	./convcode -t
	./convcode -t -x

clean:
	rm -f convcode
