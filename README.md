# convcode
Convolutional coder and decoder in C, and an interleaver

I was in need of a convolutional coder and viterbi/BCJR decoder in C.
I needed something highly optimized that could be used in constrained
environments.  And, well, I couldn't find anything suitable.  So
unfortunately, I had to write one.

To avoid others having the same problem, I've uploaded mine.

The API is described in the convcode.h file.

This supports tail-biting, soft decoding, recursive coders, and BCJR
(sort of) decoding.  See the API for a description.

## Tests

Compile with -DCONVCODE_TESTS to enable tests and a main().  Search
for "Test code" in convcode.c for details on how to use it.  Compiling
with "make" here will compile with that enabled, "make check" will run
the tests.

## Fixed Implementations

If you want your tables to not be dynamically allocated so you can put
them in FLASH or something like that, it's possible.  See the
discussion on the convert and next\_state parameters to
alloc\_convcode().

## Memory allocation

When you allocate a convcode structure, you pass in an os\_funcs
structure that has memory allocation calls.  It uses those to allocate
and free memory.  The os\_funcs structure is defined in
convcode\_os\_funcs.h and there is a test implementation in
convcode\_os\_funcs.c used by the test code to make sure all memory
gets freed.

This way you can pass in your own memory allocation routines to fit
your needs.  All allocation is done initially; it does not do any
allocation while processing data, and only frees in the free function.

You can allocate all the memory yourself, if you like.  See the
discussion at the end of convcode.h for details.  It is recommended
that you use alloc\_convcode(), though, unless you really need to do your
own allocation.

## Optimization

This is highly optimized and will detect optimizations it can do and
install those.  It has an SIMD implementation but it is not enabled by
default, see the SIMD section below for details.

The code is designed so some functions pass in constant bools to
control what they do.  The compiler should use these bools to optimize
away all the checks based on those constants.

If your CPU supports a popcount instruction (count the number of bits
set) and you are doing hard decoding, make sure to set the processor
version properly to get a \_\_builtin\_popcount() that is a single
instruction.  It makes a huge difference.

From an initial basic, nicely coded implementation to the highly
optimized one I cut the CPU to less than 1/4 of what it was.

## SIMD

There is an implementation using SIMD at in base level decoder
function, search for DO_SIMD in the code.  At least on my system (AMD
Ryzen 7 5825U), this was substantially slower than just the non-vector
code.  I tried using sse2 and various extensions, but they didn't help.

Looking at the generated code, there's a lot of instructions spent
loading unloading the xmm registers, and just 5 instructions that are
doing parallel operations.  If there was an SIMD version of popcnt it
might break even or even be a little better, but there's not.

## Interleaver

There is also an interleaver here in case you need one.  The API is
defined in interleave.h.
