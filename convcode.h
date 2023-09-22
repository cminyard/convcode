/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This is an implementation of a convolutional coder and a viterbi
 * decoder
 */

#ifndef CONVCODE_H
#define CONVCODE_H

#include <stdint.h>
#include <stdbool.h>

struct convcode;

/*
 * If you want to use the coder again after an encode or decode, you
 * must re-initialize it.  You cannot use the coder for encoding and
 * decoding simultaneiously.
 */
void reinit_convcode(struct convcode *ce);

/*
 * Allocate a convolutional coder for coding or decoding.
 *
 * k is the constraint (the size of the polynomials in bits).  The
 * maximum value is 15.
 *
 * The polynomials are givin in the array.  They are coded where the
 * low bit handles the first bit fed into the state machine.  This
 * seems to be the standard used, but may be backwards from what you
 * expect.  There may be up to 16 polynomials.
 *
 * max_decode_len_bits is the maximum number of bits that can be
 * decoded.  You can get a pretty big matrix from this.  If you say 0
 * here, you can only use the coder for encoding.
 *
 * Data is generated to the output function a byte at a time.  You
 * will generally get full bytes (nbits = 8) for all the data except
 * the last one, which may be smaller than 8.  Data is encoded low bit
 * first.
 *
 * If output returns an error, the operation is stopped and the error
 * will be returned from the various functions.
 */
struct convcode *alloc_convcode(unsigned int k, uint16_t *polynomials,
				unsigned int num_polynomials,
				unsigned int max_decode_len_bits,
				bool do_tail,
				int (*output)(struct convcode *ce,
					      void *output_data,
					      unsigned char byte,
					      unsigned int nbits),
				void *output_data);

/*
 * Free an allocated coder.
 */
void free_convcode(struct convcode *ce);

/*
 * Feed some data into encoder.  The size is given in bits, the data
 * goes in low bit first.  The last byte may not be completely full,
 * and that's fine, it will only use the low nbits % 8.
 *
 * You can feed data in with multiple calls.  Returns an error
 */
int convencode_data(struct convcode *ce,
		    unsigned char *bytes, unsigned int nbits);

/*
 * Once all the data has been fed for encoding, you must call this to
 * finish the operation.  The last output will be done from here.  The
 * total number of bits generated is returned in total_out_bits;
 *
 * If the output function (see above) returns an error, that error will be
 * returned here.
 */
int convencode_finish(struct convcode *ce, unsigned int *total_out_bits);

/*
 * Feed some data into decoder.  The size is given in bits, the data
 * goes in low bit first.  The last byte may not be completely full,
 * and that's fine, it will only use the low nbits % 8.
 *
 * You can feed data in with multiple calls.
 */
int convdecode_data(struct convcode *ce,
		    unsigned char *bytes, unsigned int nbits);

/*
 * Once all the data has been fed for encoding, you must call this to
 * finish the operation.  Output will be done from here.  The total
 * number of bits generated is returned in total_out_bits;  The total
 * number of errors encounter is returns in num_errs.
 *
 * If the output function (see above) returns an error, that error will be
 * returned here.  This will also return 1 the data exceeds the available
 * size given in max_decode_len_bits above.
 */
int convdecode_finish(struct convcode *ce, unsigned int *total_out_bits,
		      unsigned int *num_errs);
    
#endif /* CONVCODE_H */
