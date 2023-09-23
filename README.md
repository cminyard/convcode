# convcode
Convolutional coder and decoder in C

I was in need of a convolutional coder and viterbi decoder in C, and,
well, I couldn't find anything suitable.  So unfortunately, I had to
write one.

To avoid others having the same problem, I've uploaded mine.

The API is described in the convcode.h file.

Compile with -DCONVCODE_TESTS to enable tests and a main().  Search
for "Test code" in convcode.c for details on how to use it.  Compiling
with "make" here will compile with that enabled, "make check" will run
the tests.

You can use this and do your own allocation, if you like.  See the
discussion at the end of convcode.h for details.  It is recommended
that you use alloc_convcode, though, unless you really need to do your
own allocation.