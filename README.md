# convcode
Convolutional coder and decoder in C, and an interleaver

I was in need of a convolutional coder and viterbi/BCJR decoder in C.
I needed something highly optimized that could be used in constrained
environments.  And, well, I couldn't find anything suitable.  So
unfortunately, I had to write one.

To avoid others having the same problem, I've uploaded mine.

This is a production-quality library with high performance and good
usability.  It also has ways to let you play with various parameters
and see how they perform.  If you compile this for this, compile for
tests (see below) you get a program that lets you play with various
parameters to see how the coder performs.

The API is described in the convcode.h file.

This supports tail-biting, soft decoding, partial trellis, recursive
coders, and BCJR (sort of) decoding.  See the API for a description.

Some references, optimum convolutional code values for various sizes:

https://komm.dev/res/convolutional-codes/

and note that the "degree" there is K - 1.  And some theory:

https://ocw.mit.edu/courses/6-451-principles-of-digital-communication-ii-spring-2005/43162a4e10d73639903282f4dd58001b_chap9.pdf

## Tests

Compile with -DCONVCODE_TESTS to enable tests and a main().  Search
for "Test code" in convcode.c for details on how to use it.  Compiling
with "make" here will compile with that enabled, "make check" will run
the tests.

The normal way you can use this is to input your own data and have it
encode or decode that data.

If run with -t, it runs a standard set of tests.

If run with -j, it does error injection tests on the parameters you
provide.  This is quite useful for testing performance.

## Memory Usage

A convolutional decoder can use a lot of memory.  "k" represents the
number of bits in the polynomial.  The trellis is 2 ^ (k -1) entries
wide, and your trellis must have as many entries as you expect to
decode.  The convcode.h file has details on memory usage.  If you want
a 7-bit k and 

You can save space at the expense of performance by using a partial
trellis.  See the discussion of the trellis\_width parameter to
alloc\_convcode() for details.  This reduced performance.

You can also code in blocks.  This shortens the trellis and so reduce
the amount of memory used.  It does add a tail to each block unless
you do no tail or tail biting.

## Fixed Implementations

If you want your tables to not be dynamically allocated so you can put
them in FLASH or something like that, it's possible.  See the
discussion in convcode.h on the convert and next\_state parameters to
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
optimized one I cut the CPU to less than 1/4 of what it was.  It's
probably better than that, that includes all the test code that's not
written very efficiently.  And I was careful to keep the code readable
and maintainable, so it's still fairly straightforward to follow.

The code size will be fairly large as it's aggressively inlined.  You
can remove some inlining if the code takes up too much space.  And you
can chop out functions that you don't need to save code space.

## SIMD

There is an implementation using SIMD at in base level decoder
function, search for DO_SIMD in the code.  At least on my system (AMD
Ryzen 7 5825U), this was substantially slower than just the non-vector
code.  I tried using sse2 and various extensions, but they didn't help.

Looking at the generated code, there's a lot of instructions spent
loading unloading the xmm registers, and just 5 instructions that are
doing parallel operations.  If there was an SIMD version of popcnt it
might break even or even be a little better, but there's not.

I have currently only tried this on one x86_64 processor.  It may be
better on ARM or RISC-V.  Testing needs to be done.

## Interleaver

There is also an interleaver here in case you need one.  The API is
defined in interleave.h.
