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

#define CONVCODE_MAX_POLYNOMIALS 16

struct convcode;

/*
 * Used to report output bits as they are generated.
 */
typedef int (*convcode_output)(struct convcode *ce, void *user_data,
			       unsigned char byte, unsigned int nbits);

/*
 * Allocate a convolutional coder for coding or decoding.
 *
 * k is the constraint (the size of the polynomials in bits).  The
 * maximum value is 16,
 *
 * The polynomials are given in the array.  They are coded where the
 * low bit handles the first bit fed into the state machine.  This
 * seems to be the standard used, but may be backwards from what you
 * expect.  There may be up to 16 polynomials.
 *
 * max_decode_len_bits is the maximum number of bits that can be
 * decoded.  You can get a pretty big matrix from this.  If you say 0
 * here, you can only use the coder for encoding.
 *
 * Two separate output functions are set, one for the encoder and one
 * for the decoder.  You can set one you don't use to NULL.
 *
 * Data is generated to the output functions a byte at a time.  You
 * will generally get full bytes (nbits = 8) for all the data except
 * the last one, which may be smaller than 8.  Data is encoded low bit
 * first.
 *
 * If output function returns an error, the operation is stopped and the
 * error will be returned from the various functions.
 */
struct convcode *alloc_convcode(unsigned int k, uint16_t *polynomials,
				unsigned int num_polynomials,
				unsigned int max_decode_len_bits,
				convcode_output enc_output,
				void *enc_out_user_data,
				convcode_output dec_output,
				void *dec_out_user_data);

/*
 * Free an allocated coder.
 */
void free_convcode(struct convcode *ce);

/*
 * Reinit the encoder or decoder.  If you want to use the coder again
 * after and encode or decode operation, you must reinitialize the
 * part you are using.  Encoding and decoding may be done
 * simultaneously with the same structure.
 */
void reinit_enconvcode(struct convcode *ce);
void reinit_deconvcode(struct convcode *ce);

/*
 * Call both of the the above functions
 */
void reinit_convcode(struct convcode *ce);

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
 * Once all the data has been fed for decoding, you must call this to
 * finish the operation.  Output will be done from here.  The total
 * number of bits generated is returned in total_out_bits;  The total
 * number of errors encounter is returns in num_errs.
 *
 * If the output function (see above) returns an error, that error
 * will be returned here.  This will also return 1 if the data exceeds
 * the available size given in max_decode_len_bits above.
 */
int convdecode_finish(struct convcode *ce, unsigned int *total_out_bits,
		      unsigned int *num_errs);

    
/***********************************************************************
 * Here and below is more internal stuff.  You can sort of use this,
 * but it may be subject to change.
 */

/*
 * Both the decoder and encoder use the following structure to report
 * output bits.
 */
struct convcode_outdata {
    /*
     * Used to report output bytes as they are collected.  The last
     * time this is called from the finish function nbits may be < 8.
     */
    convcode_output output;
    void *user_data;

    /*
     * Output bit processing.  Bits are collected in out_bits until we
     * get 8, then we send it to the output.
     */
    unsigned char out_bits;
    unsigned int out_bit_pos;

    /* Total number of output bits we have generated. */
    unsigned int total_out_bits;
};

/*
 * The data structure for encoding and decoding.  Note that if you use
 * alloc_convcode(), you don't need to mess with this.  But you can
 * change output and output_data if you like in enc_out and dec_out.
 * But don't mess with anything else if you use alloc_convcode().
 * output and output_data will always be first so you can change them
 * even if the structure changes underneath.
 */
struct convcode {
    struct convcode_outdata enc_out;
    struct convcode_outdata dec_out;

    /*
     * Used to report output bytes as they are collected for encoding.
     * The last time this is called from convencode_finish() nbits may
     * be < 8.
     */
    convcode_output enc_output;
    void *enc_user_data;

    /* The constraint, or polynomial size in bits.  Max is 16. */
    unsigned int k;

    /* Polynomials. */
    uint16_t polys[CONVCODE_MAX_POLYNOMIALS];
    unsigned int num_polys;

    /* Current state. */
    uint16_t enc_state;
    uint16_t state_mask;

    /* For the given state, what is the encoded output? */
    uint16_t *convert;
    unsigned int convert_size;

    /*
     * Number of states in the state machine, 1 << (k - 1).
     */
    unsigned int num_states;

    /*
     * The bit trellis matrix.  The first array is an array of
     * pointers to arrays of uint16_t, one for each possible output
     * bit on decoding.  It is trellis_size elements.  Each array in
     * that is individually allocated and contains the state for a
     * specific input.  Each is num_states elements.
     */
    uint16_t *trellis;
    unsigned int trellis_size;
    unsigned int ctrellis; /* Current trellis value */

    /*
     * You don't need the whole path value matrix, you only need the
     * previous one and the next one (the one you are working on).
     * Each of these is num_states elements.
     */
    unsigned int *curr_path_values;
    unsigned int *next_path_values;

    /*
     * When reading bits for decoding, there may be some left over if
     * there weren't enough bits for the whole operation.  Store those
     * here for use in the next decode call.
     */
    unsigned int leftover_bits;
    unsigned char leftover_bits_data;
};

/*
 * If you want to manage all the memory yourself, then do the following:
 *  * Get your own copy of struct convcode.
 *  * Call setup_convcode1.  This will set up various data items you will
 *    need for allocation.
 *  * Set ce->output, ce->output_data
 *  * Allocate the following:
 *    ce->convert - sizeof(*ce->convert) * ce->convert_size
 *  * If you are doing decoding, allocate the following:
 *    ce->trellis - sizeof(*ce->trellis) * ce->trellis_size * ce->num_states
 *    ce->curr_paths_value - sizeof(*ce->curr_path_values) * ce->num_states
 *    ce->next_paths_value - sizeof(*ce->next_path_values) * ce->num_states
 *  * Call setup_convcode2(ce)
 *  * Call reinit_convcode(ce)
 *
 * You can look at the code for the various size calculations if you want
 * to statically allocate the various entries.
 *
 * Note that if you use this technique, you will not be binary
 * compatible with newer libraries of this code.  But that's probably
 * not an issue.
 */

/*
 * See the above discussion and alloc_convcode for the meaning of the values.
 */
int setup_convcode1(struct convcode *ce, unsigned int k, uint16_t *polynomials,
		    unsigned int num_polynomials,
		    unsigned int max_decode_len_bits);

/* See the above discussion for how to use this. */
void setup_convcode2(struct convcode *ce);

#endif /* CONVCODE_H */
