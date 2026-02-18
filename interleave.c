
#include "interleave.h"

void
interleaver_init(struct interleaver *di,
		 unsigned int interleave,
		 uint8_t *data, unsigned int total_bits)
{
    di->interleave = interleave;
    di->total_bits = total_bits;
    di->num_rows = total_bits / interleave;
    di->data = data;
    di->row = 0;
    di->col = 0;

    if (total_bits % interleave == 0) {
	di->last_full_col = interleave;
    } else {
	di->last_full_col = total_bits % interleave - 1;
	di->num_rows++;
    }
}

static void
interleaver_calc_pos(struct interleaver *di,
		     unsigned int *byte, unsigned int *bit)
{
    unsigned int bitpos = di->row * di->interleave + di->col;

    *byte = bitpos / 8;
    *bit = bitpos % 8;
}

static void
interleaver_next_bit(struct interleaver *di)
{
    di->row++;
    if (di->row >= di->num_rows) {
	if (di->col == di->last_full_col)
	    /* Last bit that will go into the last byte. */
	    di->num_rows--;
	di->col++;
	di->row = 0;
    }
}

void
interleave(unsigned int interleave,
	   uint8_t *data, unsigned int total_bits,
	   void (*output)(void *user_data, unsigned int bit),
	   void *user_data)
{
    struct interleaver did, *di = &did;
    unsigned int i;

    interleaver_init(di, interleave, data, total_bits);

    for (i = 0; i < total_bits; i++) {
	unsigned int byte, bit;

	interleaver_calc_pos(di, &byte, &bit);
	output(user_data, (data[byte] >> bit) & 1);
	interleaver_next_bit(di);
    }
}

unsigned int
interleave_bit(struct interleaver *di)
{
    unsigned int byte, bit;
    unsigned int rv;

    interleaver_calc_pos(di, &byte, &bit);

    rv = (di->data[byte] >> bit) & 1;

    interleaver_next_bit(di);

    return rv;
}

void
deinterleave_bit(struct interleaver *di, unsigned int bitval)
{
    unsigned int byte, bit;

    interleaver_calc_pos(di, &byte, &bit);

    di->data[byte] |= bitval << bit;

    interleaver_next_bit(di);
}

#ifdef CONVCODE_TESTS

/*
 * Test code.
 *
 * Compile and run with -t to run tests.
 *
 * To supply your own input and output, run as:
 *
 * ./interleave -t | <interleave> <bits>
 *
 * where bits is a sequence of 0 or 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

static void
print_data(uint8_t *data, unsigned int nbits)
{
    unsigned int i, byte, bit;

    for (i = 0, byte = 0, bit = 0; i < nbits; i++) {
	if (data[byte] & (1 << bit))
	    printf("1");
	else
	    printf("0");
	bit++;
	if (bit >= 8) {
	    byte++;
	    bit = 0;
	}
    }
}

static uint8_t *
get_data(const char *input, unsigned int *rlen)
{
    uint8_t *data;
    unsigned int i, nbits, len, byte, bit;

    len = strlen(input);
    data = calloc(len, 1);
    if (!data)
	return data;

    for (i = 0, byte = 0, bit = 0; input[i]; i++) {
	if (input[i] == '1')
	    data[byte] |= 1 << bit;
	nbits++;
	bit++;
	if (bit >= 8) {
	    byte++;
	    bit = 0;
	}
    }
    *rlen = len;
    return data;
}

static int
rand_test(void)
{
    unsigned int len, i, interleave_len;
    uint8_t *idata, *odata, *data;
    struct interleaver di;
    int rv = 0;

    len = rand() % 256 + 1;
    interleave_len = rand() % 32 + 1;
    printf("Running test interleave size %u length %d\n", interleave_len, len);

    data = calloc(len, 1);
    idata = calloc(len / 8 + 1, 1);
    odata = calloc(len / 8 + 1, 1);

    for (i = 0; i < len; i++)
	idata[i / 8] |= (rand() & 1) << (i % 8);
    
    interleaver_init(&di, interleave_len, idata, len);
    for (i = 0; i < len; i++)
	data[i] = interleave_bit(&di);

    interleaver_init(&di, interleave_len, odata, len);
    for (i = 0; i < len; i++)
	deinterleave_bit(&di, data[i]);

    if (memcmp(idata, odata, len / 8  + 1) != 0) {
	printf("  Failed\n");
	rv = 1;
    }

    free(data);
    free(idata);
    free(odata);

    return rv;
}

static int
run_tests(void)
{
    int errs = 0, i;

    srand(time(NULL));

    for (i = 0; i < 32; i++)
	errs += rand_test();

    printf("%u errors\n", errs);
    return !!errs;
}

static void
out_bit(void *user_data, unsigned int bit)
{
    printf("%d", bit);
}

int
main(int argc, char *argv[])
{
    unsigned int interleave_len;
    uint8_t *data;
    int arg;
    unsigned int i, len;
    bool decode = false, test = false;

    for (arg = 1; arg < argc; arg++) {
	if (argv[arg][0] != '-')
	    break;
	if (strcmp(argv[arg], "-d") == 0) {
	    decode = true;
	} else if (strcmp(argv[arg], "-e") == 0) {
	    decode = false;
	} else if (strcmp(argv[arg], "-t") == 0) {
	    test = true;
	} else {
	    fprintf(stderr, "unknown option: %s\n", argv[arg]);
	    return 1;
	}
    }

    if (test)
	return run_tests();

    if (arg >= argc) {
	fprintf(stderr, "No interleave size given\n");
	return 1;
    }

    interleave_len = strtoul(argv[arg++], NULL, 0);

    if (arg >= argc) {
	fprintf(stderr, "No data given\n");
	return 1;
    }

    if (decode) {
	len = strlen(argv[arg]);
	data = calloc(len, 1);
    } else {
	data = get_data(argv[arg], &len);
    }
    if (!data) {
	printf("Unable to allocate data block\n");
	return 1;
    }
    if (decode) {
	struct interleaver di;

	interleaver_init(&di, interleave_len, data, len);
	for (i = 0; argv[arg][i]; i++) {
	    if (argv[arg][i] == '1')
		deinterleave_bit(&di, 1);
	    else
		deinterleave_bit(&di, 0);
	}
	print_data(data, len);
    } else {
	interleave(interleave_len, data, len, out_bit, NULL);
    }

    free(data);

    printf("\n  bits = %u\n", len);
    return 0;
}
#endif
