/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "convcode.h"

#define CONVCODE_DEBUG_STATES 0

/*
 * The trellis is a two-dimensional matrix, but the size is dynamic
 * based upon how it is created.  So we use a one-dimensional matrix
 * and do our own indexing with the below two functions/macros.
 */
static convcode_state *
get_trellis_column(struct convcode *ce, unsigned int column)
{
    return ce->trellis + column * ce->num_states * sizeof(*ce->trellis);
}

#define trellis_entry(ce, column, row) get_trellis_column(ce, column)[row]

void
reinit_convencode(struct convcode *ce, unsigned int start_state)
{
    ce->enc_state = start_state;
    ce->enc_out.out_bits = 0;
    ce->enc_out.out_bit_pos = 0;
    ce->enc_out.total_out_bits = 0;
}

int
reinit_convdecode(struct convcode *ce, unsigned int start_state,
		  unsigned int init_other_states)
{
    unsigned int i;

    if (start_state >= ce->num_states)
	return 1;

    ce->dec_out.out_bits = 0;
    ce->dec_out.out_bit_pos = 0;
    ce->dec_out.total_out_bits = 0;

    if (ce->num_states > 0) {
	ce->curr_path_values[start_state] = 0;
	for (i = 0; i < ce->num_states; i++) {
	    if (i == start_state)
		continue;
	    ce->curr_path_values[i] = init_other_states;
	}
	ce->ctrellis = 0;
    }
    ce->leftover_bits = 0;
    return 0;
}

void
reinit_convcode(struct convcode *ce)
{
    reinit_convencode(ce, CONVCODE_DEFAULT_START_STATE);
    reinit_convdecode(ce, CONVCODE_DEFAULT_START_STATE,
		      CONVCODE_DEFAULT_INIT_VAL);
}

static unsigned int
reverse_bits(unsigned int k, unsigned int val)
{
    unsigned int i, rv = 0;

    for (i = 0; i < k; i++) {
	rv <<= 1;
	rv |= val & 1;
	val >>= 1;
    }
    return rv;
}

/* Is the number of set bits in the value odd?  Return 1 if true, 0 if false */
static unsigned int
num_bits_is_odd(unsigned int v)
{
    unsigned int rv = 0;

    while (v) {
	if (v & 1)
	    rv = !rv;
	v >>= 1;
    }
    return rv;
}

void
free_convcode(struct convcode *ce)
{
    if (ce->convert[0])
	free(ce->convert[0]);
    if (ce->convert[1])
	free(ce->convert[1]);
    if (ce->next_state[0])
	free(ce->next_state[0]);
    if (ce->next_state[1])
	free(ce->next_state[1]);
    if (ce->trellis)
	free(ce->trellis);
    if (ce->curr_path_values)
	free(ce->curr_path_values);
    if (ce->next_path_values)
	free(ce->next_path_values);
    free(ce);
}

int
setup_convcode1(struct convcode *ce, unsigned int k,
		convcode_state *polynomials, unsigned int num_polynomials,
		unsigned int max_decode_len_bits,
		bool do_tail, bool recursive)
{
    unsigned int i;

    if (num_polynomials < 1 || num_polynomials > CONVCODE_MAX_POLYNOMIALS)
	return 1;
    if (k > CONVCODE_MAX_K)
	return 1;

    memset(ce, 0, sizeof(*ce));
    ce->k = k;
    ce->num_states = 1 << (k - 1);
    ce->do_tail = do_tail;
    ce->recursive = recursive;
    ce->uncertainty_100 = 100;

    /*
     * Polynomials come in as the first bit being the high bit.  We
     * have to spin them around because we process using the first bit
     * as the low bit because it's a lot more efficient.
     */
    ce->num_polys = num_polynomials;
    for (i = 0; i < ce->num_polys; i++)
	ce->polys[i] = reverse_bits(k, polynomials[i]);

    if (max_decode_len_bits > 0)
	ce->trellis_size = max_decode_len_bits + k * ce->num_polys;

    return 0;
}

void
setup_convcode2(struct convcode *ce)
{
    unsigned int val, i, j;
    convcode_state state_mask = ce->num_states - 1;

    /*
     * Calculate the encoder output arrays and the next state arrays.
     * These are pre-calculated so encoding is just a matter of using
     * the convert arrays to get the output and the next_state arrays
     * to get the next state.
     */
    if (!ce->recursive) {
	for (i = 0; i < ce->num_states; i++) {
	    ce->convert[0][i] = 0;
	    ce->convert[1][i] = 0;
	    /* Go through each polynomial to calculate the output. */
	    for (j = 0; j < ce->num_polys; j++) {
		val = num_bits_is_odd((i << 1) & ce->polys[j]);
		ce->convert[0][i] |= val << j;
		val = num_bits_is_odd(((i << 1) | 1) & ce->polys[j]);
		ce->convert[1][i] |= val << j;
	    }

	    /* Next state is easy, just shift in the value and mask. */
	    ce->next_state[0][i] = (i << 1) & state_mask;
	    ce->next_state[1][i] = ((i << 1) | 1) & state_mask;
	}
    } else {
	for (i = 0; i < ce->num_states; i++) {
	    convcode_state bval0, bval1;

	    /* In recursive, the first output bit is always the value. */
	    ce->convert[0][i] = 0;
	    ce->convert[1][i] = 1;

	    /*
	     * This is the recursive bit calculated from the feedback
	     * and the input.
	     */
	    bval0 = num_bits_is_odd((i << 1) & ce->polys[0]);
	    bval1 = num_bits_is_odd(((i << 1) | 1) & ce->polys[0]);

	    /*
	     * Generate output from the rest of the polynomials.
	     */
	    for (j = 1; j < ce->num_polys; j++) {
		val = num_bits_is_odd(((i << 1) | bval0) & ce->polys[j]);
		ce->convert[0][i] |= val << j;
		val = num_bits_is_odd(((i << 1) | bval1) & ce->polys[j]);
		ce->convert[1][i] |= val << j;
	    }

	    /* Shift the recursive bit in to get the next state. */
	    ce->next_state[0][i] = ((i << 1) | bval0) & state_mask;
	    ce->next_state[1][i] = ((i << 1) | bval1) & state_mask;
	}
    }
#if CONVCODE_DEBUG_STATES
    printf("S0:");
    for (i = 0; i < ce->num_states; i++)
	printf(" %4.4d", ce->next_state[0][i]);
    printf("\nS1:");
    for (i = 0; i < ce->num_states; i++)
	printf(" %4.4d", ce->next_state[1][i]);
    printf("\nC0:");
    for (i = 0; i < ce->num_states; i++)
	printf(" %4.4d", ce->convert[0][i]);
    printf("\nC1:");
    for (i = 0; i < ce->num_states; i++)
	printf(" %4.4d", ce->convert[1][i]);
    printf("\n");
#endif
}

struct convcode *
alloc_convcode(unsigned int k, convcode_state *polynomials,
	       unsigned int num_polynomials,
	       unsigned int max_decode_len_bits,
	       bool do_tail, bool recursive,
	       convcode_output enc_output, void *enc_out_user_data,
	       convcode_output dec_output, void *dec_out_user_data)
{
    struct convcode *ce;

    ce = malloc(sizeof(*ce));
    if (!ce)
	return NULL;
    if (setup_convcode1(ce, k, polynomials, num_polynomials,
			max_decode_len_bits, do_tail, recursive)) {
	free(ce);
	return NULL;
    }

    ce->enc_out.output = enc_output;
    ce->enc_out.user_data = enc_out_user_data;
    ce->dec_out.output = dec_output;
    ce->dec_out.user_data = dec_out_user_data;

    ce->convert[0] = malloc(sizeof(*ce->convert) * ce->num_states);
    if (!ce->convert[0])
	goto out_err;

    ce->convert[1] = malloc(sizeof(*ce->convert) * ce->num_states);
    if (!ce->convert[1])
	goto out_err;

    ce->next_state[0] = malloc(sizeof(*ce->next_state) * ce->num_states);
    if (!ce->next_state[0])
	goto out_err;

    ce->next_state[1] = malloc(sizeof(*ce->next_state) * ce->num_states);
    if (!ce->next_state[1])
	goto out_err;

    if (max_decode_len_bits > 0) {
	/* Add on a bit for the stuff at the end. */
	ce->trellis = malloc(sizeof(*ce->trellis) *
			     ce->trellis_size * ce->num_states);
	if (!ce->trellis)
	    goto out_err;

	ce->curr_path_values = malloc(sizeof(*ce->curr_path_values)
				      * ce->num_states);
	if (!ce->curr_path_values)
	    goto out_err;
	ce->next_path_values = malloc(sizeof(*ce->next_path_values)
				      * ce->num_states);
	if (!ce->next_path_values)
	    goto out_err;
    }

    setup_convcode2(ce);
    reinit_convcode(ce);

    return ce;

 out_err:
    free_convcode(ce);
    return NULL;
}

void
set_encode_output_per_symbol(struct convcode *ce, bool val)
{
    ce->enc_out.output_symbol_size = val;
}

void
set_decode_max_uncertainty(struct convcode *ce, uint8_t max_uncertainty)
{
    ce->uncertainty_100 = max_uncertainty;
}

static int
output_bits(struct convcode *ce, struct convcode_outdata *of,
	    unsigned int bits, unsigned int len)
{
    int rv = 0;

    if (of->output_symbol_size)
	return of->output(ce, of->user_data, bits, len);

    of->out_bits |= bits << of->out_bit_pos;
    while (of->out_bit_pos + len >= 8) {
	unsigned int used = 8 - of->out_bit_pos;

	rv = of->output(ce, of->user_data, of->out_bits, 8);
	if (rv)
	    return rv;

	of->total_out_bits += used;
	bits >>= used;
	len -= used;
	of->out_bit_pos = 0;
	of->out_bits = bits;
    }
    of->out_bit_pos += len;
    of->total_out_bits += len;
    return rv;
}

static int
encode_bit(struct convcode *ce, unsigned int bit)
{
    convcode_state state = ce->enc_state;
    ce->enc_state = ce->next_state[bit][state];
    return output_bits(ce, &ce->enc_out,
		       ce->convert[bit][state], ce->num_polys);
}

int
convencode_data(struct convcode *ce,
		const unsigned char *bytes, unsigned int nbits)
{
    unsigned int i, j;
    int rv;

    for (i = 0; nbits > 0; i++) {
	unsigned char byte = bytes[i];

	for (j = 0; nbits > 0 && j < 8; j++) {
	    rv = encode_bit(ce, byte & 1);
	    byte >>= 1;
	    if (rv)
		return rv;
	    nbits--;
	}
    }
    return 0;
}

int
convencode_finish(struct convcode *ce, unsigned int *total_out_bits)
{
    unsigned int i;
    int rv;

    if (ce->do_tail) {
	for (i = 0; i < ce->k - 1; i++) {
	    rv = encode_bit(ce, 0);
	    if (rv)
		return rv;
	}
    }
    if (ce->enc_out.out_bit_pos > 0)
	ce->enc_out.output(ce, ce->enc_out.user_data,
			   ce->enc_out.out_bits, ce->enc_out.out_bit_pos);
    if (total_out_bits)
	*total_out_bits = ce->enc_out.total_out_bits;
    return 0;
}

static void
convencode_block_bit(struct convcode *ce, unsigned int bit,
		     unsigned char **ioutbytes,
		     unsigned int *ioutbitpos)
{
    unsigned int outbits, bits_left;
    convcode_state state = ce->enc_state;
    unsigned char *outbytes = *ioutbytes;
    unsigned int outbitpos = *ioutbitpos;
    unsigned int nbytebits = 8 - outbitpos;

    ce->enc_state = ce->next_state[bit][state];

    outbits = ce->convert[bit][state];
    bits_left = ce->num_polys;

    /* Now comes the messy job of putting the bits into outbytes. */
    while (bits_left > nbytebits) {
	/* Bits going into this byte. */
	unsigned int cbits = outbits & ((1 << nbytebits) - 1);

	*outbytes++ |= cbits << outbitpos;
	outbitpos = 0;
	outbits >>= nbytebits;
	bits_left -= nbytebits;
	nbytebits = 8 - outbitpos;
    }
    *outbytes |= outbits << outbitpos;
    outbitpos += bits_left;
    if (outbitpos >= 8) {
	outbytes++;
	outbitpos = 0;
    }
    *ioutbytes = outbytes;
    *ioutbitpos = outbitpos;
}

void
convencode_block(struct convcode *ce,
		 const unsigned char *bytes, unsigned int nbits,
		 unsigned char *outbytes)
{
    unsigned int i, j, outbitpos = 0;

    for (i = 0; nbits > 0; i++) {
	unsigned char byte = bytes[i];

	for (j = 0; nbits > 0 && j < 8; j++) {
	    convencode_block_bit(ce, byte & 1, &outbytes, &outbitpos);

	    nbits--;
	    byte >>= 1;
	}
    }
    if (ce->do_tail) {
	for (i = 0; i < ce->k - 1; i++)
	    convencode_block_bit(ce, 0, &outbytes, &outbitpos);
    }
}

static unsigned int
num_bits_set(unsigned int v)
{
    unsigned int count = 0;

    while (v) {
	count += v & 1;
	v >>= 1;
    }
    return count;
}

/*
 * This returns how far we think we are away from the actual value.
 * When not using uncertainties, this is the mumber of bits that are
 * different between v1 and v2.  When using uncertainties, if the bits
 * are the same we use the uncertainty of the bits being correct.  If
 * the bits are different, we use the uncertainty of the bits being
 * different (which is 100% - uncertainty).
 */
static unsigned int
hamming_distance(struct convcode *ce, unsigned int v1, unsigned int v2,
		 const uint8_t *uncertainty)
{
    unsigned int i, rv = 0;

    if (!uncertainty)
	return num_bits_set(v1 ^ v2);

    for (i = 0; i < ce->num_polys; i++) {
	if ((v1 & 1) == (v2 & 1)) {
	    rv += uncertainty[i];
	} else {
	    rv += ce->uncertainty_100 - uncertainty[i];
	}
	v1 >>= 1;
	v2 >>= 1;
    }
    return rv;
}

/*
 * Return the bit that got us here from pstate (prev state) to cstate
 * (curr state).  For non-recursive mode, that's always the low bit of
 * cstate.  For recursive mode, you have to look at pstate to see what
 * it's next state is for each bit.
 */
static int
get_prev_bit(struct convcode *ce, convcode_state pstate, convcode_state cstate)
{
    if (!ce->recursive)
	return cstate & 1;

    if (ce->next_state[0][pstate] == cstate)
	return 0;
    else
	return 1;
#if 0
    /* For debugging */
    else if (ce->next_state[1][pstate] == cstate)
	return 1;
    else
	printf("ERR!: %x %x %x %x\n", pstate, cstate);
    return 0;
#endif
}

static int
decode_bits(struct convcode *ce, unsigned int bits, const uint8_t *uncertainty)
{
    unsigned int *currp = ce->curr_path_values;
    unsigned int *nextp = ce->next_path_values;
    unsigned int i;

    if (ce->ctrellis + ce->num_polys > ce->trellis_size)
	return 1;

    for (i = 0; i < ce->num_states; i++) {
	convcode_state pstate1 = i >> 1, pstate2, bit;
	unsigned int dist1, dist2;

	/*
	 * This state could have come from two different states, one
	 * with the top bit set (pstate2) and with with the top bit
	 * clear (pstate1).  We check both of those.
	 */
	pstate2 = pstate1 | (1 << (ce->k - 2));

	dist1 = currp[pstate1];
	bit = get_prev_bit(ce, pstate1, i);
	dist1 += hamming_distance(ce, ce->convert[bit][pstate1],
				  bits, uncertainty);
	dist2 = currp[pstate2];
	bit = get_prev_bit(ce, pstate2, i);
	dist2 += hamming_distance(ce, ce->convert[bit][pstate2],
				  bits, uncertainty);

	if (dist2 < dist1) {
	    trellis_entry(ce, ce->ctrellis, i) = pstate2;
	    nextp[i] = dist2;
	} else {
	    trellis_entry(ce, ce->ctrellis, i) = pstate1;
	    nextp[i] = dist1;
	}
    }
#if CONVCODE_DEBUG_STATES
    printf("T(%u) %x\n", ce->ctrellis, bits);
    for (i = 0; i < ce->num_states; i++) {
	printf(" %4.4u", trellis_entry(ce, ce->ctrellis, i));
    }
    printf("\n");
    for (i = 0; i < ce->num_states; i++) {
	printf(" %4.4u", nextp[i]);
    }
    printf("\n");
#endif
    ce->ctrellis++;
    ce->next_path_values = currp;
    ce->curr_path_values = nextp;
    return 0;
}

/*
 * Extract nbits bits from bytes at offset curr.
 */
static unsigned int
extract_bits(const unsigned char *bytes, unsigned int curr, unsigned int nbits)
{
    unsigned int pos = curr / 8;
    unsigned int opos = 0;
    unsigned int bit = curr % 8, bits_left = nbits;
    unsigned int v = 0, byte_avail;

    byte_avail = 8 - bit;
    while (byte_avail <= bits_left) {
	v |= ((unsigned int) (bytes[pos] >> bit)) << opos;
	bits_left -= byte_avail;
	opos += byte_avail;
	bit = 0;
	byte_avail = 8;
	bytes++;
    }
    if (bits_left)
	v |= ((unsigned int) (bytes[pos] >> bit)) << opos;
    v &= (1 << nbits) - 1;
    return v;
}

int
convdecode_data(struct convcode *ce,
		const unsigned char *bytes, unsigned int nbits,
		const uint8_t *uncertainty)
{
    unsigned int curr_bit = 0, i;
    int rv;

    if (ce->leftover_bits) {
	unsigned int newbits, extract_size;

	if (nbits + ce->leftover_bits < ce->num_polys) {
	    /* Not enough bits for a full symbol, just store these. */
	    ce->leftover_bits_data |= bytes[0] << ce->leftover_bits;
	    if (uncertainty) {
		for (i = 0; i < nbits; i++)
		    ce->leftover_uncertainty[ce->leftover_bits++] =
			uncertainty[i];
	    } else {
		ce->leftover_bits += nbits;
	    }
	    ce->leftover_bits_data &= (1 << ce->leftover_bits) - 1;
	    return 0;
	}
	/* We got enough bits for a full symbol, process it. */
	extract_size = ce->num_polys - ce->leftover_bits;
	newbits = extract_bits(bytes, curr_bit, extract_size);
	curr_bit += extract_size;
	nbits -= extract_size;
	ce->leftover_bits_data |= newbits << ce->leftover_bits;
	if (uncertainty) {
	    for (i = 0; i < extract_size; i++)
		ce->leftover_uncertainty[ce->leftover_bits++] =
		    uncertainty[i];
	    rv = decode_bits(ce, ce->leftover_bits_data,
			     ce->leftover_uncertainty);
	} else {
	    rv = decode_bits(ce, ce->leftover_bits_data, NULL);
	}
	ce->leftover_bits = 0;
    }

    while (nbits >= ce->num_polys) {
	unsigned int bits = extract_bits(bytes, curr_bit, ce->num_polys);

	if (uncertainty)
	    rv = decode_bits(ce, bits, uncertainty + curr_bit);
	else
	    rv = decode_bits(ce, bits, NULL);
	if (rv)
	    return rv;
	curr_bit += ce->num_polys;
	nbits -= ce->num_polys;
    }
    ce->leftover_bits = nbits;
    if (nbits) {
	ce->leftover_bits_data = bytes[curr_bit / 8] >> (curr_bit % 8);
	ce->leftover_bits_data &= (1 << nbits) - 1;
	if (uncertainty) {
	    for (i = 0; i < ce->leftover_bits; i++)
		ce->leftover_uncertainty[i] = uncertainty[curr_bit++];
	}
    }
    return 0;
}

int
convdecode_finish(struct convcode *ce, unsigned int *total_out_bits,
		  unsigned int *num_errs)
{
    unsigned int i, extra_bits = 0;
    unsigned int min_val = ce->curr_path_values[0], cstate = 0;

    /* Find the minimum value in the final path. */
    for (i = 1; i < ce->num_states; i++) {
	if (ce->curr_path_values[i] < min_val) {
	    cstate = i;
	    min_val = ce->curr_path_values[i];
	}
    }

    /* Go backwards through the trellis to find the full path. */
    for (i = ce->ctrellis; i > 0; ) {
	convcode_state pstate; /* Previous state */

	i--;
	pstate = trellis_entry(ce, i, cstate);
	/*
	 * Store the bit values in position 0 so we can play it back
	 * forward easily.
	 */
	trellis_entry(ce, i, 0) = get_prev_bit(ce, pstate, cstate);
	cstate = pstate;
    }

    /* We've stored the values in index 0 of each column, play it forward. */
    if (ce->do_tail)
	extra_bits = ce->k - 1;
    for (i = 0; i < ce->ctrellis - extra_bits; i++) {
	int rv = output_bits(ce, &ce->dec_out, trellis_entry(ce, i, 0), 1);
	if (rv)
	    return rv;
    }
    if (ce->dec_out.out_bit_pos > 0)
	ce->dec_out.output(ce, ce->dec_out.user_data,
			   ce->dec_out.out_bits, ce->dec_out.out_bit_pos);
    if (num_errs)
	*num_errs = min_val;
    if (total_out_bits)
	*total_out_bits = ce->dec_out.total_out_bits;
    return 0;
}

int
convdecode_block(struct convcode *ce, const unsigned char *bytes,
		 unsigned int nbits, const uint8_t *uncertainty,
		 unsigned char *outbytes, unsigned int *output_uncertainty,
		 unsigned int *num_errs)
{
    unsigned int i, extra_bits = 0;
    unsigned int min_val, cuncertainty, cstate;

    if (convdecode_data(ce, bytes, nbits, uncertainty))
	return 1;

    /* Find the minimum value in the final path. */
    min_val = ce->curr_path_values[0];
    cstate = 0;
    for (i = 1; i < ce->num_states; i++) {
	if (ce->curr_path_values[i] < min_val) {
	    cstate = i;
	    min_val = ce->curr_path_values[i];
	}
    }

    /* Go backwards through the trellis to find the full path. */
    if (ce->do_tail)
	extra_bits = ce->k - 1;
    cuncertainty = min_val;
    for (i = ce->ctrellis; i > 0; ) {
	convcode_state pstate; /* Previous state */
	unsigned int bit, bits, inpos;
	const uint8_t *u = NULL;

	i--;
	pstate = trellis_entry(ce, i, cstate);
	bit = get_prev_bit(ce, pstate, cstate);

	/*
	 * Store the bit values in the user-supplied data.
	 */
	if (extra_bits == 0)
	    outbytes[i / 8] |= bit << (i % 8);

	if (output_uncertainty) {
	    if (extra_bits == 0)
		output_uncertainty[i] = cuncertainty;

	    /*
	     * Subtract off the distance we had computed to here to get the
	     * previous uncertainty value.
	     */
	    inpos = i * ce->num_polys;
	    bits = extract_bits(bytes, inpos, ce->num_polys);
	    if (uncertainty)
		u = uncertainty + inpos;
	    cuncertainty -= hamming_distance(ce, ce->convert[bit][pstate],
					     bits, u);
	}
	if (extra_bits > 0)
	    extra_bits--;

	cstate = pstate;
    }

    if (num_errs)
	*num_errs = min_val;

    return 0;
}

#ifdef CONVCODE_TESTS

/*
 * Test code.
 *
 * Compile and run with -t to run tests.
 *
 * To supply your own input and output, run as:
 *
 * ./convcode [-t] [-x] [-s start state] [-i init_val]
 *        -p <poly1> [ -p <poly2> ... ] k <bits>
 *
 * where bits is a sequence of 0 or 1.  The -x option disables the
 * "tail" of the encoder and expectation of the tail in the decoder.
 * (see the convcode.h file about do_tail).  -x works with -t to run
 * the tests that way.  Otherwise, no other options have an effect
 * with -t.
 *
 * The -s and -i options set the start state of the encoder/decoder,
 * and for decoding the init value for the probability matrix for
 * values besides the start state.  See the discussion on tails in
 * convcode.h for detail.
 *
 * For instance, to decode some data with the Voyager coder, do:
 *
 * $ ./convcode -p 0171 -p 0133 7 00110011
 *   0000111010000000111111100111
 *   bits = 28
 *
 * To then decode that data, do:
 *
 * $ ./convcode -p 0171 -p 0133 -d 7 0000111010000000111111100111
 *   00110011
 *   errors = 0
 *   bits = 8
 *
 * The tests themselves are stolen from
 * https://github.com/xukmin/viterbi.git
 */

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

static int
handle_output(struct convcode *ce, void *output_data, unsigned char byte,
	      unsigned int nbits)
{
    unsigned int i;

    for (i = 0; i < nbits; i++) {
	if (byte & 1)
	    printf("1");
	else
	    printf("0");
	byte >>= 1;
    }
    return 0;
}

static void
do_encode_data(struct convcode *ce, const char *input, unsigned int *total_bits)
{
    unsigned int i, nbits;
    unsigned char byte = 0;

    for (i = 0, nbits = 0; input[i]; i++) {
	if (input[i] == '1')
	    byte |= 1 << nbits;
	nbits++;
	if (nbits == 8) {
	    convencode_data(ce, &byte, 8);
	    nbits = 0;
	    byte = 0;
	}
    }
    if (nbits > 0)
	convencode_data(ce, &byte, nbits);
    convencode_finish(ce, total_bits);
}

static void
do_decode_data(struct convcode *ce, const char *input, unsigned int *total_bits,
	       unsigned int *num_errs, uint8_t *uncertainty)
{
    unsigned int i, nbits;
    unsigned char byte = 0;

    for (i = 0, nbits = 0; input[i]; i++) {
	if (input[i] == '1')
	    byte |= 1 << nbits;
	nbits++;
	if (nbits == 8) {
	    convdecode_data(ce, &byte, 8, uncertainty);
	    nbits = 0;
	    byte = 0;
	    if (uncertainty)
		uncertainty += 8;
	}
    }
    if (nbits > 0)
	convdecode_data(ce, &byte, nbits, uncertainty);
    convdecode_finish(ce, total_bits, num_errs);
}

struct test_data {
    char output[1024];
    unsigned char enc_bytes[1024];
    unsigned char dec_bytes[1024];
    unsigned int uncertainties[1024];
    unsigned int outpos;
};

static int
handle_test_output(struct convcode *ce, void *output_data, unsigned char byte,
		   unsigned int nbits)
{
    struct test_data *t = output_data;
    unsigned int i;

    for (i = 0; i < nbits; i++) {
	assert(t->outpos < sizeof(t->output) - 1);
	if (byte & 1)
	    t->output[t->outpos++] = '1';
	else
	    t->output[t->outpos++] = '0';
	byte >>= 1;
    }
    return 0;
}

static unsigned int
run_test(unsigned int k, convcode_state *polys, unsigned int npolys,
	 bool do_tail, const char *encoded, const char *decoded,
	 unsigned int expected_errs, uint8_t *uncertainty,
	 unsigned int *out_uncertainties)
{
    struct test_data t;
    struct convcode *ce = alloc_convcode(k, polys, npolys, 128,
					 do_tail, false,
					 handle_test_output, &t,
					 handle_test_output, &t);
    unsigned int i, enc_nbits, dec_nbits, num_errs, rv = 0;

    printf("Test k=%u err=%u polys={ 0%o", k, expected_errs, polys[0]);
    for (i = 1; i < npolys; i++)
	printf(", 0%o", polys[i]);
    printf(" }\n");
    t.outpos = 0;
    if (expected_errs == 0) {
	do_encode_data(ce, decoded, &enc_nbits);
	t.output[t.outpos] = '\0';
	if (strcmp(encoded, t.output) != 0) {
	    printf("  encode failure, expected\n    %s\n  got\n    %s\n",
		   encoded, t.output);
	    rv = 1;
	    goto out;
	}
	if (enc_nbits != strlen(encoded)) {
	    printf("  encode failure, got %u output bits, expected %u\n",
		   enc_nbits, (unsigned int) strlen(encoded));
	    rv++;
	}
	t.outpos = 0;
    }
    do_decode_data(ce, encoded, &dec_nbits, &num_errs, uncertainty);
    t.output[t.outpos] = '\0';
    if (strcmp(decoded, t.output) != 0) {
	printf("  decode failure, expected\n    %s\n  got\n    %s\n",
	       decoded, t.output);
	rv++;
    }
    if (num_errs != expected_errs) {
	printf("  decode failure, got %u errors, expected %u\n",
	       num_errs, expected_errs);
	rv++;
    }
    if (dec_nbits != strlen(decoded)) {
	printf("  decode failure, got %u output bits, expected %u\n",
	       dec_nbits, (unsigned int) strlen(decoded));
	rv++;
    }
    if (rv)
	goto out;

    reinit_convcode(ce);
    memset(t.enc_bytes, 0, sizeof(t.enc_bytes));
    if (expected_errs == 0) {
	memset(t.dec_bytes, 0, sizeof(t.dec_bytes));
	for (i = 0, dec_nbits = 0; decoded[i]; i++, dec_nbits++) {
	    unsigned int bit = decoded[i] == '0' ? 0 : 1;
	    t.dec_bytes[i / 8] |= bit << (i % 8);
	}
	enc_nbits = dec_nbits;
	if (do_tail)
	    enc_nbits += k - 1;
	enc_nbits *= npolys;

	convencode_block(ce, t.dec_bytes, dec_nbits, t.enc_bytes);
	for (i = 0; i < enc_nbits; i++) {
	    unsigned int bit = encoded[i] == '0' ? 0 : 1;

	    if (((t.enc_bytes[i / 8] >> (i % 8)) & 1) != bit) {
		printf("  block encode failure at bit %u\n", i);
		rv++;
		goto out;
	    }
	}
    } else {
	for (i = 0, enc_nbits = 0; encoded[i]; i++, enc_nbits++) {
	    unsigned int bit = encoded[i] == '0' ? 0 : 1;
	    t.enc_bytes[i / 8] |= bit << (i % 8);
	}
    }

    memset(t.dec_bytes, 0, sizeof(t.dec_bytes));
    if (convdecode_block(ce, t.enc_bytes, enc_nbits, uncertainty,
			 t.dec_bytes, t.uncertainties, &num_errs)) {
	printf("  block decode error return\n");
	rv++;
	goto out;
    }
    if (num_errs != expected_errs) {
	printf("  decode failure, got %u errors, expected %u\n",
	       num_errs, expected_errs);
	rv++;
    }
    for (i = 0; i < dec_nbits; i++) {
	unsigned int bit = decoded[i] == '0' ? 0 : 1;

	if (((t.dec_bytes[i / 8] >> (i % 8)) & 1) != bit) {
	    printf("  block decode failure at bit %u\n", i);
	    rv++;
	    goto out;
	}
	if (out_uncertainties && (t.uncertainties[i] != out_uncertainties[i])) {
	    printf("  block decode invalid uncertainty at bit %u\n", i);
	    rv++;
	    goto out;
	}
    }

 out:
    free_convcode(ce);
    return rv;
}

static unsigned int
rand_block_test(struct convcode *ce,
		const char *encoded, const char *decoded)
{
    struct test_data t;
    unsigned int i, dec_nbits, enc_nbits;
    unsigned int rv;

    reinit_convcode(ce);
    memset(t.enc_bytes, 0, sizeof(t.enc_bytes));
    memset(t.dec_bytes, 0, sizeof(t.dec_bytes));
    for (i = 0, dec_nbits = 0; decoded[i]; i++, dec_nbits++) {
	unsigned int bit = decoded[i] == '0' ? 0 : 1;
	t.dec_bytes[i / 8] |= bit << (i % 8);
    }
    enc_nbits = dec_nbits;
    if (ce->do_tail)
	enc_nbits += ce->k - 1;
    enc_nbits *= ce->num_polys;

    convencode_block(ce, t.dec_bytes, dec_nbits, t.enc_bytes);
    for (i = 0; i < enc_nbits; i++) {
	unsigned int bit = encoded[i] == '0' ? 0 : 1;

	if (((t.enc_bytes[i / 8] >> (i % 8)) & 1) != bit) {
	    printf("  block encode failure at bit %u\n", i);
	    rv++;
	    goto out;
	}
    }

    memset(t.dec_bytes, 0, sizeof(t.dec_bytes));
    if (convdecode_block(ce, t.enc_bytes, enc_nbits, NULL,
			 t.dec_bytes, NULL, NULL)) {
	printf("  block decode error return\n");
	rv++;
	goto out;
    }
    for (i = 0; i < dec_nbits; i++) {
	unsigned int bit = decoded[i] == '0' ? 0 : 1;

	if (((t.dec_bytes[i / 8] >> (i % 8)) & 1) != bit) {
	    printf("  block decode failure at bit %u\n", i);
	    rv++;
	    goto out;
	}
    }
 out:
    return rv;
}

static unsigned int
rand_test(unsigned int k, convcode_state *polys, unsigned int npolys,
	  bool do_tail, bool recursive)
{
    struct test_data t;
    struct convcode *ce = alloc_convcode(k, polys, npolys, 128,
					 do_tail, recursive,
					 handle_test_output, &t,
					 handle_test_output, &t);
    unsigned int i, j, bit, total_bits, num_errs, rv = 0;
    char decoded[33];
    char encoded[1024];

    if (recursive)
	set_encode_output_per_symbol(ce, true);

    printf("Random test k=%u %s %s polys={ 0%o", k,
	   do_tail ? "tail" : "notail",
	   recursive ? "recursive" : "non-recursive",
	   polys[0]);
    for (i = 1; i < npolys; i++)
	printf(", 0%o", polys[i]);
    printf(" }\n");

    for (i = 8; i < 32; i++) {
	for (j = 0; j < 10; j++) {
	    for (bit = 0; bit < i; bit++)
		decoded[bit] = rand() & 1 ? '1' : '0';
	    decoded[bit] = 0;
	    t.outpos = 0;
	    reinit_convcode(ce);
	    do_encode_data(ce, decoded, &total_bits);
	    memcpy(encoded, t.output, t.outpos);
	    encoded[t.outpos] = '\0';
	    t.outpos = 0;
	    do_decode_data(ce, encoded, &total_bits, &num_errs, NULL);
	    t.output[t.outpos] = '\0';
	    if (strcmp(t.output, decoded) != 0) {
		printf("  decode failure, expected\n    %s\n  got\n    %s\n",
		       decoded, t.output);
		rv++;
	    }
	    rv += rand_block_test(ce, encoded, decoded);
	}
    }
    free_convcode(ce);
    return rv;
}

static int
run_tests(bool do_tail)
{
    unsigned int errs = 0;
    srand(time(NULL));

    {
	convcode_state polys[2] = { 5, 7 };
	static unsigned int out_uncertainties[15] = {
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1
	};
	if (do_tail) {
	    errs += run_test(3, polys, 2, do_tail,
			     "0011010010011011110100011100110111",
			     "010111001010001", 0, NULL, NULL);
	    errs += run_test(3, polys, 2, do_tail,
			     "0011010010011011110000011100110111",
			     "010111001010001", 1, NULL, out_uncertainties);
	} else {
	    errs += run_test(3, polys, 2, do_tail,
			     "001101001001101111010001110011",
			     "010111001010001", 0, NULL, NULL);
	    errs += run_test(3, polys, 2, do_tail,
			     "001101001001101111000001110011",
			     "010111001010001", 1, NULL, out_uncertainties);
	}
	errs += rand_test(3, polys, 2, do_tail, false);
    }
    {
	convcode_state polys[2] = { 3, 7 };
	if (do_tail) {
	    errs += run_test(3, polys, 2, do_tail,
			     "0111101000110000", "101100", 0, NULL, NULL);
	} else {
	    errs += run_test(3, polys, 2, do_tail,
			     "011110100011", "101100", 0, NULL, NULL);
	}
	errs += rand_test(3, polys, 2, do_tail, false);
    }
    {
	convcode_state polys[2] = { 5, 3 };
	static uint8_t uncertainties[18] = {
	    0, 0, 100, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0,
	};
	static unsigned int out_uncertainties1[7] = {
	    1, 1, 1, 1, 1, 2, 2
	};
	static unsigned int out_uncertainties2[7] = {
	    0, 100, 100, 100, 100, 100, 100
	};
	if (do_tail) {
	    errs += run_test(3, polys, 2, do_tail,
			     "100111101110010111", "1001101", 0, NULL, NULL);
	    errs += run_test(3, polys, 2, do_tail,
			     "110111101100010111", "1001101", 2, NULL,
			     out_uncertainties1);
	    errs += run_test(3, polys, 2, do_tail,
			     "100111101110010111", "1001101",
			     100, uncertainties, out_uncertainties2);
	} else {
	    errs += run_test(3, polys, 2, do_tail,
			     "10011110111001", "1001101", 0, NULL, NULL);
	    errs += run_test(3, polys, 2, do_tail,
			     "11011110110001", "1001101", 2, NULL,
			     out_uncertainties1);
	    errs += run_test(3, polys, 2, do_tail,
			     "10011110111001", "1001101",
			     100, uncertainties, out_uncertainties2);
	}
	errs += rand_test(3, polys, 2, do_tail, false);
    }
    { /* Voyager */
	convcode_state polys[2] = { 0171, 0133 };
	static uint8_t uncertainties[28] = {
	    0, 0, 0, 0, 100, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0
	};
	static unsigned int out_uncertainties[8] = {
	    0, 0, 100, 100, 100, 100, 100, 100
	};
	if (do_tail) {
	    errs += run_test(7, polys, 2, do_tail,
			     "0011100010011010100111011100", "01011010",
			     100, uncertainties, out_uncertainties);
	} else {
	    errs += run_test(7, polys, 2, do_tail,
			     "0011100010011010", "01011010",
			     100, uncertainties, out_uncertainties);
	}
	errs += rand_test(7, polys, 2, do_tail, false);
    }
    { /* LTE */
	convcode_state polys[3] = { 0117, 0127, 0155 };
	static unsigned int out_uncertainties1[8] = {
	    2, 2, 2, 2, 2, 2, 2, 3
	};
	static unsigned int out_uncertainties2[8] = {
	    2, 2, 2, 3, 3, 4, 4, 4
	};
	if (do_tail) {
	    errs += run_test(7, polys, 3, do_tail,
			     "111001101011100110011101111111100110001111",
			     "10110111", 0, NULL, NULL);
	    errs += run_test(7, polys, 3, do_tail,
			     "001001101011100110011100111111100110001011",
			     "10110111", 4, NULL, out_uncertainties1);
	} else {
	    errs += run_test(7, polys, 3, do_tail,
			     "111001101011100110011101",
			     "10110111", 0, NULL, NULL);
	    errs += run_test(7, polys, 3, do_tail,
			     "001001101010100010011101",
			     "10110111", 4, NULL, out_uncertainties2);
	}
	errs += rand_test(7, polys, 3, do_tail, false);
    }
    { /* CDMA 2000 */
	convcode_state polys[4] = { 0671, 0645, 0473, 0537 };
	errs += rand_test(9, polys, 4, do_tail, false);
    }
    { /* Cassini / Mars Pathfinder */
	convcode_state polys[7] = { 074000, 046321, 051271, 070535,
	    063667, 073277, 076513 };
	errs += rand_test(15, polys, 7, do_tail, false);
    }
    /*
     * Recursive tests, taken from:
     * https://en.wikipedia.org/wiki/Convolutional_code#Recursive_and_non-recursive_codes.
     */
    {
	convcode_state polys[2] = { 5, 5 };
	errs += rand_test(3, polys, 2, do_tail, true);
    }
    { /* Constituent code in 3GPP 25.212 Turbo Code */
	convcode_state polys[2] = { 012, 015 };
	errs += rand_test(4, polys, 2, do_tail, true);
    }
    {
	convcode_state polys[2] = { 022, 021 };
	errs += rand_test(5, polys, 2, do_tail, true);
    }

    printf("%u errors\n", errs);
    return !!errs;
}

int
main(int argc, char *argv[])
{
    convcode_state polys[CONVCODE_MAX_POLYNOMIALS];
    unsigned int num_polys = 0;
    unsigned int k;
    struct convcode *ce;
    unsigned int arg, total_bits, num_errs = 0;
    bool decode = false, test = false, do_tail = true, recursive = false;
    unsigned int start_state = 0, init_val = CONVCODE_DEFAULT_INIT_VAL;

    for (arg = 1; arg < argc; arg++) {
	if (argv[arg][0] != '-')
	    break;
	if (strcmp(argv[arg], "-d") == 0) {
	    decode = true;
	} else if (strcmp(argv[arg], "-e") == 0) {
	    decode = false;
	} else if (strcmp(argv[arg], "-t") == 0) {
	    test = true;
	} else if (strcmp(argv[arg], "-x") == 0) {
	    do_tail = false;
	} else if (strcmp(argv[arg], "-r") == 0) {
	    recursive = true;
	} else if (strcmp(argv[arg], "-s") == 0) {
	    arg++;
	    if (arg >= argc) {
		fprintf(stderr, "No data supplied for -s\n");
		return 1;
	    }
	    start_state = strtoul(argv[arg], NULL, 0);
	} else if (strcmp(argv[arg], "-i") == 0) {
	    arg++;
	    if (arg >= argc) {
		fprintf(stderr, "No data supplied for -i\n");
		return 1;
	    }
	    init_val = strtoul(argv[arg], NULL, 0);
	} else if (strcmp(argv[arg], "-p") == 0) {
	    if (num_polys == CONVCODE_MAX_POLYNOMIALS) {
		fprintf(stderr, "Too many polynomials\n");
		return 1;
	    }
	    arg++;
	    if (arg >= argc) {
		fprintf(stderr, "No data supplied for -p\n");
		return 1;
	    }
	    polys[num_polys++] = strtoul(argv[arg], NULL, 0);
	} else {
	    fprintf(stderr, "unknown option: %s\n", argv[arg]);
	    return 1;
	}
    }

    if (test)
	return run_tests(do_tail);

    if (num_polys == 0) {
	fprintf(stderr, "No polynomials (-p) given\n");
	return 1;
    }

    if (arg >= argc) {
	fprintf(stderr, "No constraint (k) given\n");
	return 1;
    }

    k = strtoul(argv[arg++], NULL, 0);
    if (k == 0 || k > CONVCODE_MAX_K) {
	fprintf(stderr, "Constraint (k) must be from 1 to %d\n",
		CONVCODE_MAX_POLYNOMIALS);
	return 1;
    }

    ce = alloc_convcode(k, polys, num_polys, 128, do_tail, recursive,
			handle_output, NULL,
			handle_output, NULL);
    if (start_state)
	reinit_convencode(ce, start_state);
    if (start_state || init_val != CONVCODE_DEFAULT_INIT_VAL)
	reinit_convdecode(ce, start_state, init_val);

    if (arg >= argc) {
	fprintf(stderr, "No data given\n");
	return 1;
    }

    printf("  ");
    if (decode)
	do_decode_data(ce, argv[arg], &total_bits, &num_errs, NULL);
    else
	do_encode_data(ce, argv[arg], &total_bits);

    if (decode)
	printf("\n  errors = %u", num_errs);
    printf("\n  bits = %u\n", total_bits);
    return 0;
}
#endif
