/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This is an interleaver for interleaving bits for transmission.
 */

#ifndef INTERLEAVE_H
#define INTERLEAVE_H

#include <stdint.h>

/*
 * Interleaving
 *
 * Codes often get interleaved on the input and/or the output.
 * Convolutional codes perform better if the errors are spread out,
 * and errors in the real world tend to be bunched together in events.
 *
 * Interleaving spreads the bits out so that consecutive bits coming
 * into the decoder are not consecutive bits from the receiver.  It
 * does this by treating the data as an array of bits that is
 * interleave_len wide and total_size / interleave_len long (plus an
 * extra entry for any leftover bits).
 *
 * For encoding, data is pulled out of the source array array starting
 * with column 0 and down the rows, and after a column is complete go
 * back to row 0 and go down column 1, etc. taking care with the
 * entries in the last row not to index past the end of the array.
 *
 * For decoding, the opposite is done, you feed bits into the decoder
 * and it puts it into the output bit array.
 *
 * Bits are numbered with the low bit in a byte first.
 */

struct interleaver {
    /*
     * Total length in bits of the data to interleave.
     */
    unsigned int total_bits;

    /*
     * The number of bits to jump when interleaving, or the number of
     * columns in the array.
     */
    unsigned int interleave;

    /*
     * Total number of rows in the array.  Note that the last row may
     * not be full.
     */
    unsigned int num_rows;

    /*
     * If the last row is not full (total_bits % interleave != 0) this
     * will be the last column that is valid in the last row.  If the
     * last row is full, this will be interleave, whic is still the
     * last valid row.
     */
    unsigned int last_full_col;

    uint8_t *data;

    /* Current row and column. */
    unsigned int row;
    unsigned int col;
};

/*
 * Initialize the interleaver structure.
 *
 * You must call this before using the interleaver object.
 *
 * interleave_len is the length of the interleave operation, or the
 * number of bits to skip for consectutive bits.
 *
 * data is the input data for interleave_bit, the output data for
 * deinterleave_bit.
 *
 * total_bits is the total size (in bits) of the data.  It does not
 * need to be byte aligned.
 */
void interleaver_init(struct interleaver *di,
		      unsigned int interleave_len,
		      uint8_t *data, unsigned int total_bits);


/*
 * Interleave an operation.  This particular one does not require an
 * interleaver object, it generated the output to a function a bit at
 * a time.  Same meanings for parameters as interleave_init() above.
 *
 * The output function is called on every bit in output order, that
 * function also get the user_data object passed in here.  bit will be
 * 0 or 1.
 */
void interleave(unsigned int interleave_len,
		uint8_t *data, unsigned int total_bits,
		void (*output)(void *user_data, unsigned int bit),
		void *user_data);

/*
 * Get the next output bit from the interleaver.  Returns 0 or 1.  Be
 * sure not to call this more than total_bits or you will overrun the
 * array.
 *
 * This function will pull bits out of the data in interleaved order.
 */
unsigned int interleave_bit(struct interleaver *di);

/*
 * Give an input bit to the deinterleaver and store the next bit in
 * the data.  Be sure not to call this more than total_bits or you
 * will overrun the array.
 *
 * The input bits should come in interleaved order and will be store
 * in the data array in original order.
 */
void deinterleave_bit(struct interleaver *di, unsigned int bitval);

#endif /* INTERLEAVE_H */
