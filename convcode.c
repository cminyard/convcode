/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "convcode.h"

struct convcode {
    /* The constraint, or polynomial size in bits.  Max is 16. */
    unsigned int k;

    /* Polynomials. */
    uint16_t *polys;
    unsigned int num_polys;

    /* Current state. */
    uint16_t state;
    uint16_t state_mask;

    /*
     * Output bit processing.  Bits are collected in out_bits until we
     * get 8, then we send it to the output.
     */
    unsigned char out_bits;
    unsigned int out_bit_pos;

    /* Total number of output bits we have generated. */
    unsigned int total_out_bits;

    /* For the given state, what is the output? */
    uint16_t *convert;
    unsigned int convert_size;

    unsigned int num_states;

    uint16_t **trellis;
    unsigned int trellis_size;
    unsigned int ctrellis; /* Current trellis value */

    unsigned int *curr_path_values;
    unsigned int *next_path_values;

    unsigned int leftover_bits;
    unsigned char leftover_bits_data;

    /*
     * Used to report output bytes as they are collected.  The last time
     * this is called from convencode_finish() nbits may be < 8.
     */
    int (*output)(struct convcode *ce, void *output_data, unsigned char byte,
		  unsigned int nbits);
    void *output_data;
};

void
reinit_convcode(struct convcode *ce)
{
    unsigned int i;

    ce->state = 0;
    ce->out_bits = 0;
    ce->out_bit_pos = 0;
    ce->total_out_bits = 0;

    if (ce->num_states > 0) {
	ce->curr_path_values[0] = 0;
	for (i = 1; i < ce->num_states; i++)
	    ce->curr_path_values[i] = UINT_MAX;
	ce->ctrellis = 0;
    }
    ce->leftover_bits = 0;
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
    if (ce->polys)
	free(ce->polys);
    if (ce->convert)
	free(ce->convert);

    if (ce->trellis) {
	unsigned int i;

	for (i = 0; i < ce->trellis_size; i++) {
	    if (ce->trellis[i])
		free(ce->trellis[i]);
	}
	free(ce->trellis);
    }
    if (ce->curr_path_values)
	free(ce->curr_path_values);
    if (ce->next_path_values)
	free(ce->next_path_values);
    free(ce);
}

struct convcode *
alloc_convcode(unsigned int k, uint16_t *polynomials,
	       unsigned int num_polynomials,
	       unsigned int max_decode_len_bits,
	       int (*output)(struct convcode *ce, void *output_data,
			     unsigned char byte, unsigned int nbits),
	       void *output_data)
{
    struct convcode *ce;
    unsigned int i, j;

    ce = malloc(sizeof(*ce));
    if (!ce)
	return NULL;
    memset(ce, 0, sizeof(*ce));
    ce->k = k;
    ce->output = output;
    ce->output_data = output_data;
    ce->state_mask = (1 << k) - 1;

    ce->polys = malloc(sizeof(*ce->polys) * num_polynomials);
    if (!ce->polys)
	goto out_err;
    ce->num_polys = num_polynomials;

    for (i = 0; i < ce->num_polys; i++)
	ce->polys[ce->num_polys - i - 1] = reverse_bits(k, polynomials[i]);

    ce->convert_size = 1 << k;
    ce->convert = malloc(sizeof(*ce->convert) * ce->convert_size);
    if (!ce->convert)
	goto out_err;
    memset(ce->convert, 0, sizeof(*ce->convert) * ce->convert_size);

    for (i = 0; i < ce->convert_size; i++) {
	for (j = 0; j < ce->num_polys; j++) {
	    ce->convert[i] |= num_bits_is_odd(i & ce->polys[j]) << j;
	}
    }

    if (max_decode_len_bits > 0) {
	ce->num_states = 1 << (k - 1);
	/* Add on a bit for the stuff at the end. */
	ce->trellis_size = max_decode_len_bits + k * num_polynomials;
	ce->trellis = malloc(sizeof(*ce->trellis) * ce->trellis_size);
	if (!ce->trellis)
	    goto out_err;
	for (i = 0; i < ce->trellis_size; i++) {
	    ce->trellis[i] = malloc(sizeof(**ce->trellis) * ce->num_states);
	    if (!ce->trellis[i])
		goto out_err;
	}

	ce->curr_path_values = malloc(sizeof(*ce->curr_path_values)
				      * ce->num_states);
	if (!ce->curr_path_values)
	    goto out_err;
	ce->next_path_values = malloc(sizeof(*ce->next_path_values)
				      * ce->num_states);
	if (!ce->next_path_values)
	    goto out_err;
    }

    reinit_convcode(ce);

    return ce;

 out_err:
    free_convcode(ce);
    return NULL;
}

static int
output_bits(struct convcode *ce, unsigned int bits, unsigned int len)
{
    int rv = 0;

    ce->out_bits |= bits << ce->out_bit_pos;
    while (ce->out_bit_pos + len >= 8) {
	unsigned int used = 8 - ce->out_bit_pos;

	rv = ce->output(ce, ce->output_data, ce->out_bits, 8);
	if (rv)
	    return rv;

	ce->total_out_bits += used;
	bits >>= used;
	len -= used;
	ce->out_bit_pos = 0;
	ce->out_bits = bits;
    }
    ce->out_bit_pos += len;
    ce->total_out_bits += len;
    return rv;
}

static int
encode_bit(struct convcode *ce, unsigned int bit)
{
    ce->state = ((ce->state << 1) | bit) & ce->state_mask;
    return output_bits(ce, ce->convert[ce->state], ce->num_polys);
}

int
convencode_data(struct convcode *ce, unsigned char *bytes, unsigned int nbits)
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

    for (i = 0; i < ce->k - 1; i++) {
	rv = encode_bit(ce, 0);
	if (rv)
	    return rv;
    }
    if (ce->out_bit_pos > 0)
	ce->output(ce, ce->output_data, ce->out_bits, ce->out_bit_pos);
    if (total_out_bits)
	*total_out_bits = ce->total_out_bits;
    return 0;
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

/* Number of bits that are different between v1 and v2. */
static unsigned int
hamming_distance(unsigned int v1, unsigned int v2)
{
    return num_bits_set(v1 ^ v2);
}

static int
decode_bits(struct convcode *ce, unsigned int bits)
{
    unsigned int *currp = ce->curr_path_values;
    unsigned int *nextp = ce->next_path_values;
    unsigned int i;

    if (ce->ctrellis >= ce->trellis_size)
	return 1;

    for (i = 0; i < ce->num_states; i++) {
	uint16_t pstate1 = i, pstate2;
	unsigned int dist1, dist2;

	pstate2 = i | (1 << (ce->k - 1));

	dist1 = currp[pstate1 >> 1];
	if (dist1 != UINT_MAX)
	    dist1 += hamming_distance(ce->convert[pstate1], bits);
	dist2 = currp[pstate2 >> 1];
	if (dist2 != UINT_MAX)
	    dist2 += hamming_distance(ce->convert[pstate2], bits);

	if (dist2 < dist1) {
	    ce->trellis[ce->ctrellis][i] = pstate2 >> 1;
	    nextp[i] = dist2;
	} else {
	    ce->trellis[ce->ctrellis][i] = pstate1 >> 1;
	    nextp[i] = dist1;
	}
    }
#if 0
    printf("T(%u) %x\n", ce->ctrellis, bits);
    for (i = 0; i < ce->num_states; i++) {
	printf(" %4.4u", ce->trellis[ce->ctrellis][i]);
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

static unsigned int
extract_bits(unsigned char *bytes, unsigned int curr, unsigned int nbits)
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
    }
    if (bits_left)
	v |= ((unsigned int) (bytes[pos] >> bit)) << opos;
    v &= (1 << nbits) - 1;
    return v;
}

int
convdecode_data(struct convcode *ce, unsigned char *bytes, unsigned int nbits)
{
    unsigned int curr_bit = 0;
    int rv;

    if (ce->leftover_bits) {
	unsigned int newbits, extract_size;

	if (nbits + ce->leftover_bits < ce->num_polys) {
	    ce->leftover_bits_data |= bytes[0] << ce->leftover_bits;
	    ce->leftover_bits += nbits;
	    ce->leftover_bits_data &= (1 << ce->leftover_bits) - 1;
	    return 0;
	}
	extract_size = ce->num_polys - ce->leftover_bits;
	newbits = extract_bits(bytes, curr_bit, extract_size);
	curr_bit += extract_size;
	nbits -= extract_size;
	ce->leftover_bits_data |= newbits << ce->leftover_bits;
	rv = decode_bits(ce, ce->leftover_bits_data);
	ce->leftover_bits = 0;
    }

    while (nbits >= ce->num_polys) {
	unsigned int bits = extract_bits(bytes, curr_bit, ce->num_polys);
	rv = decode_bits(ce, bits);
	if (rv)
	    return rv;
	curr_bit += ce->num_polys;
	nbits -= ce->num_polys;
    }
    ce->leftover_bits = nbits;
    if (nbits) {
	ce->leftover_bits_data = bytes[curr_bit / 8] >> (curr_bit % 8);
	ce->leftover_bits_data &= (1 << nbits) - 1;
    }
    return 0;
}

int
convdecode_finish(struct convcode *ce, unsigned int *total_out_bits,
		  unsigned int *num_errs)
{
    unsigned int i;
    unsigned int min_val = ce->curr_path_values[0], min_pos = 0;

    for (i = 1; i < ce->num_states; i++) {
	if (ce->curr_path_values[i] < min_val) {
	    min_pos = i;
	    min_val = ce->curr_path_values[i];
	}
    }

    for (i = ce->ctrellis; i > 0; ) {
	uint16_t pstate;

	i--;
	pstate = ce->trellis[i][min_pos];
	/*
	 * Store the bit values in position 0 so we can play it back
	 * forward easily.
	 */
	ce->trellis[i][0] = min_pos & 1;
	min_pos = pstate;
    }

    for (i = 0; i < ce->ctrellis - (ce->k - 1); i++) {
	int rv = output_bits(ce, ce->trellis[i][0], 1);
	if (rv)
	    return rv;
    }
    if (ce->out_bit_pos > 0)
	ce->output(ce, ce->output_data, ce->out_bits, ce->out_bit_pos);
    if (num_errs)
	*num_errs = min_val;
    if (total_out_bits)
	*total_out_bits = ce->total_out_bits;
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
 * ./convcode [-t] -p <poly1> [ -p <poly2> ... ] k <bits>
 *
 * where bits is a sequence of 0 or 1.
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
    unsigned char byte;

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
	       unsigned int *num_errs)
{
    unsigned int i, nbits;
    unsigned char byte;

    for (i = 0, nbits = 0; input[i]; i++) {
	if (input[i] == '1')
	    byte |= 1 << nbits;
	nbits++;
	if (nbits == 8) {
	    convdecode_data(ce, &byte, 8);
	    nbits = 0;
	    byte = 0;
	}
    }
    if (nbits > 0)
	convdecode_data(ce, &byte, nbits);
    convdecode_finish(ce, total_bits, num_errs);
}

struct test_data {
    char output[1024];
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
run_test(unsigned int k, uint16_t *polys, unsigned int npolys,
	 const char *encoded, const char *decoded,
	 unsigned int expected_errs)
{
    struct test_data t;
    struct convcode *ce = alloc_convcode(k, polys, npolys, 128,
					 handle_test_output, &t);
    unsigned int i, total_bits, num_errs, rv = 0;

    printf("Test k=%u err=%u polys={ 0%o", k, expected_errs, polys[0]);
    for (i = 1; i < npolys; i++)
	printf(", 0%o", polys[i]);
    printf(" }\n");
    t.outpos = 0;
    if (expected_errs == 0) {
	do_encode_data(ce, decoded, &total_bits);
	t.output[t.outpos] = '\0';
	if (strcmp(encoded, t.output) != 0) {
	    printf("  encode failure, expected\n    %s\n  got\n    %s\n",
		   encoded, t.output);
	    rv = 1;
	    goto out;
	}
	if (total_bits != strlen(encoded)) {
	    printf("  encode failure, got %u output bits, expected %u\n",
		   total_bits, (unsigned int) strlen(encoded));
	    rv++;
	}
	t.outpos = 0;
	reinit_convcode(ce);
    }
    do_decode_data(ce, encoded, &total_bits, &num_errs);
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
    if (total_bits != strlen(decoded)) {
	printf("  decode failure, got %u output bits, expected %u\n",
	       total_bits, (unsigned int) strlen(decoded));
	rv++;
    }
 out:
    free_convcode(ce);
    return rv;
}

static unsigned int
rand_test(unsigned int k, uint16_t *polys, unsigned int npolys)
{
    struct test_data t;
    struct convcode *ce = alloc_convcode(k, polys, npolys, 128,
					 handle_test_output, &t);
    unsigned int i, j, bit, total_bits, num_errs, rv = 0;
    char decoded[33];
    char encoded[1024];

    printf("Random test k=%u polys={ 0%o", k, polys[0]);
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
	    reinit_convcode(ce);
	    do_decode_data(ce, encoded, &total_bits, &num_errs);
	    t.output[t.outpos] = '\0';
	    if (strcmp(t.output, decoded) != 0) {
		printf("  decode failure, expected\n    %s\n  got\n    %s\n",
		       decoded, t.output);
		rv++;
	    }
	}
    }
    free_convcode(ce);
    return rv;
}

static int
run_tests(void)
{
    unsigned int errs = 0;
    srand(time(NULL));

    {
	uint16_t polys[2] = { 5, 7 };
	errs += run_test(3, polys, 2,
			 "0011100001100111111000101100111011",
			 "010111001010001", 0);
	errs += run_test(3, polys, 2,
			 "0011100001100111110000101100111011",
			 "010111001010001", 1);
	errs += rand_test(3, polys, 2);
    }
    {
	uint16_t polys[2] = { 3, 7 };
	errs += run_test(3, polys, 2,
			 "1011010100110000", "101100", 0);
	errs += rand_test(3, polys, 2);
    }
    {
	uint16_t polys[2] = { 5, 3 };
	errs += run_test(3, polys, 2,
			 "011011011101101011", "1001101", 0);
	errs += run_test(3, polys, 2,
			 "111011011100101011", "1001101", 2);
	errs += rand_test(3, polys, 2);
    }
    { /* Voyager */
	uint16_t polys[2] = { 0171, 0133 };
	errs += rand_test(7, polys, 2);
    }
    { /* LTE */
	uint16_t polys[3] = { 0117, 0127, 0155 };
	errs += run_test(7, polys, 3,
			 "111100101110001011110101111111001011100111",
			 "10110111", 0);
	errs += run_test(7, polys, 3,
			 "100100101110001011110101110111001011100110",
			 "10110111", 4);
	errs += rand_test(7, polys, 3);
    }
    { /* CDMA 2000 */
	uint16_t polys[4] = { 0671, 0645, 0473, 0537 };
	errs += rand_test(9, polys, 4);
    }
    { /* Cassini / Mars Pathfinder */
	uint16_t polys[7] = { 074000, 046321, 051271, 070535,
	    063667, 073277, 076513 };
	errs += rand_test(15, polys, 7);
    }

    printf("%u errors\n", errs);
    return !!errs;
}

int
main(int argc, char *argv[])
{
    uint16_t polys[16];
    unsigned int num_polys = 0;
    unsigned int k;
    struct convcode *ce;
    unsigned int arg, total_bits, num_errs = 0;
    bool decode = false, test = false;

    for (arg = 1; arg < argc; arg++) {
	if (argv[arg][0] != '-')
	    break;
	if (strcmp(argv[arg], "-d") == 0) {
	    decode = true;
	} else if (strcmp(argv[arg], "-t") == 0) {
	    test = true;

	} else if (strcmp(argv[arg], "-p") == 0) {
	    if (num_polys == 16) {
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
	return run_tests();

    if (num_polys == 0) {
	fprintf(stderr, "No polynomials (-p) given\n");
	return 1;
    }

    if (arg >= argc) {
	fprintf(stderr, "No constraint (k) given\n");
	return 1;
    }

    k = strtoul(argv[arg++], NULL, 0);
    if (k == 0 || k > 16) {
	fprintf(stderr, "Constraint (k) must be from 1 to 16\n");
	return 1;
    }

    ce = alloc_convcode(k, polys, num_polys, 128, handle_output, NULL);

    if (arg >= argc) {
	fprintf(stderr, "No data given\n");
	return 1;
    }

    printf("  ");
    if (decode)
	do_decode_data(ce, argv[arg], &total_bits, &num_errs);
    else
	do_encode_data(ce, argv[arg], &total_bits);

    if (decode)
	printf("\n  errors = %u", num_errs);
    printf("\n  bits = %u\n", total_bits);
    return 0;
}
#endif