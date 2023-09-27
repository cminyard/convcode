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
#include <limits.h>

/*
 * Maximum number of polynomials. I've never seen one with more than
 * 8, but it doesn't take a lot of space to add a few more.
 */
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
 * high bit handles the first bit fed into the state machine.  This
 * seems to be the standard used, but may be backwards from what you
 * expect.  There may be up to CONVCODE_MAX_POLYNOMIALS polynomials.
 *
 * max_decode_len_bits is the maximum number of bits that can be
 * decoded.  You can get a pretty big matrix from this.  If you say 0
 * here, you can only use the coder for encoding.
 *
 * See the discussion below on tails for what do_tail does.
 *
 * The recursive setting enables a recursive decoder.  The first
 * polynomial is the recursive one, the rest are the output
 * polynomials.  The first bit output for each symbol will be the
 * input bit, per standard recursive convolutional encoding.
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
				bool do_tail, bool recursive,
				convcode_output enc_output,
				void *enc_out_user_data,
				convcode_output dec_output,
				void *dec_out_user_data);


/*
 * Free an allocated coder.
 */
void free_convcode(struct convcode *ce);

/*
 * Convolutional tail
 *
 * Normally you have a "tail" of the convolutional code, where you it
 * feeds k - 1 zeros to clear out the state and get the end state and
 * initial state the same.  That's normally what you want, so this
 * code does that for you if you set do_tail to true.  You can disable
 * the tail, but it reduces the performance of the code.
 *
 * However, there is something called "tail biting".  You initialize
 * the state to the last k - 1 bits of the data.  That way, when the
 * state machine finishes, it will be in the same state as the
 * beginning, and though it doesn't perform quite as well as a tail,
 * it's better than no tail.
 *
 * However, you have the problem on the decode side of knowing what
 * state to start at.  You solve that by running the state machine and
 * starting at zero.  The beginning bits will probably be wrong, but
 * by the end it will be aligned and you can get the final bits.  Then
 * you can re-run the algorithm with the start state set properly from
 * the final bits.
 *
 * If doing tail biting, set do_tail to false when you allocate the
 * coder.  You must then use the reinit_convencode() set the start
 * value for encoding.  Grab the last k - 1 bits of the data and put
 * them into the start state.
 *
 * On the decode side, first run with the start_state to 0 and
 * init_other_paths to a smaller number like 256.  Then determine the
 * last bits and use those for start_state and
 * CONVCODE_DEFAULT_INIT_VAL for init_other_states.
 */

/*
 * For calling the reinit functions, use these unless you are using tail
 * biting.
 */
#define CONVCODE_DEFAULT_START_STATE 0
#define CONVCODE_DEFAULT_INIT_VAL (UINT_MAX / 2)

/*
 * Reinit the encoder or decoder.  If you want to use the coder again
 * after and encode or decode operation, you must reinitialize the
 * part you are using.  Encoding and decoding may be done
 * simultaneously with the same structure.
 */
void reinit_convencode(struct convcode *ce, unsigned int start_state);

/*
 * The decoder takes a couple of parameters.
 *
 * For use of start_state and init_other_states, see the discussion of
 * tails above.  If you aren't doing tail biting, use the defaults
 * defined above.
 */
int reinit_convdecode(struct convcode *ce, unsigned int start_state,
		      unsigned int init_other_states);

/*
 * Call both of the the above functions with the defaults.
 */
void reinit_convcode(struct convcode *ce);

/*
 * If set to false (the default) the output for encoding comes out in
 * bytes except for possibly the last output.  If set to true, the
 * output will come out in a symbol, or num_polynomial, number of bits
 * each time, and there will not be a chunk at the end that is smaller.
 * This is useful if you want to split up the individual output streams
 * from each polynomial, like you would for a recursive decoder for
 * turbo coding.
 */
void set_encode_output_per_symbol(struct convcode *ce, bool val);

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

    /*
     * For encoding, this enables outputing the bytes in a per-symbol
     * basis instead of a per-byte basis.  So, for instance, if
     * num_polynomials is 3, you would get output in 3-bit chunks.
     * Not really useful for decoding.
     */
    bool output_symbol_size;

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

    bool do_tail;
    bool recursive;

    /* Current state. */
    uint16_t enc_state;

    /*
     * For the given state, what is the encoded output?  Indexed first
     * by the bit, then by the state.
     */
    unsigned int *convert[2];

    /*
     * 2D Array indexed first by bit then by current state.
     */
    uint16_t *next_state[2];

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
		    unsigned int max_decode_len_bits,
		    bool do_tail, bool recursive);

/* See the above discussion for how to use this. */
void setup_convcode2(struct convcode *ce);

#endif /* CONVCODE_H */
