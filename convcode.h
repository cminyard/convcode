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
 * This is the size of the polynomials and thus the maximum state
 * machine size, and the value to hold the state.  Keep it as small as
 * possible to reduce the trellis size.  Size of K is limited by this
 * value.
 */
typedef uint16_t convcode_state;
#define CONVCODE_MAX_K 16

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
struct convcode *alloc_convcode(unsigned int k, convcode_state *polynomials,
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
 * the final bits.  I'm not 100% sure how reliable that is, but it
 * seems to work pretty well.  The other option is to run it multiple
 * time with starting state from 0 to k-1 and choose the minimum
 * value.
 *
 * If doing tail biting, set do_tail to false when you allocate the
 * coder.  You must then use the reinit_convencode() set the start
 * value for encoding.  Grab the last k - 1 bits of the data and put
 * them into the start state parameter.
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
 * This is for handling soft decoding.  Soft decoding takes into
 * account how certain (or, in this case, uncertain) a particular bit
 * is to be correct.  For instance, when doing phase decoding, if you
 * are right on phase, then you would be 0% uncertain that the value
 * was incorrect.  If it was half-way beteen two expected phase
 * values, you would be 50% uncertain the value was correct.
 *
 * By default the uncertainty ranges from 0 to 100, where 0 is 100%
 * uncertain and 100 is 0% certain.  This function allows you to set a
 * different max value to range from.  For instance, if you set it to
 * 10 then the values would range from 0 to 10.
 *
 * These uncertainty values are given in a range from 0 to 50; if you
 * were more than 50% uncertain, you would have chosen the other
 * value, of course.
 *
 * When using soft decoding, them meaning of num_errs from
 * convdecode_finish() changes.  It is no longer a count of errors, it
 * is instead a rating of uncertainty.
 *
 * See convdecode_block() for a way to get the full set of
 * uncertainties for each output bit, for a BCJR type algorithm.
 */
void set_decode_max_uncertainty(struct convcode *ce, uint8_t max_uncertainty);

/*
 * Feed some data into encoder.  The size is given in bits, the data
 * goes in low bit first.  The last byte does not have to be completely
 * full, and that's fine, it will only use the low nbits % 8.
 *
 * You can feed data in with multiple calls.  Returns an error
 */
int convencode_data(struct convcode *ce,
		    const unsigned char *bytes, unsigned int nbits);

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
 * Encode a block of data bits.  The output bits are stored in
 * outbytes, which must be large enough to hold the full encoded
 * output.  If tail is set, then this will be ((nbits + k - 1) *
 * num_polynomials).  If tail is not set, this will be (nbits *
 * num_polynomials).  The output function is not used in this case.
 * If doing partial blocks, outbitpos is the current output bit position.
 * Pass in 0 if not using partial blocks, pass in the output of outbitpos
 * if you are.
 */
void convencode_block(struct convcode *ce,
		      const unsigned char *bytes, unsigned int nbits,
		      unsigned char *outbytes);

/*
 * For multi-part block operations, you can call convencode_block
 * partial() for all but the last block, and call
 * convencode_block_final() for the last one.
 */
void convencode_block_partial(struct convcode *ce,
			      const unsigned char *bytes, unsigned int nbits,
			      unsigned char **outbytes,
			      unsigned int *outbitpos);
void convencode_block_final(struct convcode *ce,
			    const unsigned char *bytes, unsigned int nbits,
			    unsigned char *outbytes, unsigned int outbitpos);

/*
 * Feed some data into decoder.  The size is given in bits, the data
 * goes in low bit first.  The last byte may not be completely full,
 * and that's fine, it will only use the low nbits % 8.
 *
 * If uncertainty is not NULL, this will do soft decoding.  Each array
 * entry will correspond to the uncertainty of the given bit number.  See
 * the discussion on soft decoding above the set_decode_max_uncertainty
 * function.  If it is NULL, it will do normal hard decoding.
 *
 * You can feed data in with multiple calls.
 */
int convdecode_data(struct convcode *ce,
		    const unsigned char *bytes, unsigned int nbits,
		    const uint8_t *uncertainty);

/*
 * Once all the data has been fed for decoding, you must call this to
 * finish the operation.  Output will be done from here.  The total
 * number of bits generated is returned in total_out_bits;  The total
 * number of errors (or total uncertainty when doing soft decoding)
 * encountered is returned in num_errs.
 *
 * If the output function (see above) returns an error, that error
 * will be returned here.  This will also return 1 if the data exceeds
 * the available size given in max_decode_len_bits above.
 */
int convdecode_finish(struct convcode *ce, unsigned int *total_out_bits,
		      unsigned int *num_errs);

/*
 * Much like convdecode_data() and convdecode_finish(), but does a
 * full block all at once and does not use the output function.  See
 * convdecode_data for an explaination of the first four parameters.
 *
 * The output data is stored in outbits, in the normal bit format
 * everything else uses.  With a tail, the output array must be at
 * least (nbits / num_polynomials - k - 1) *bits* long.  If tail is
 * off, it must be (nbits / num_polynomials) long.
 *
 * If output_uncertainty is not NULL, the uncertainty of each output
 * bit is stored in this array.  It must be the same length as the
 * number of bits in outbytes.  This is basically a full BCJR
 * algorithm; the output uncertainty can be used to compute the
 * probabilities of each output bit.  (Output uncertainties are not
 * provided in the standard output routine because that would require
 * keeping a lot of extra data in the convcode structure.  You would
 * only really use this if you were using blocks, anyway, so there's
 * no value in having it in the output routine.)
 *
 * The output uncertainty for each bit is the total uncertainty value
 * for all bits up to that point.  To convert that to an uncertainty
 * value for just that bit, you would use:
 *
 *   bit_uncertainty = ((uncertainty * num_polynomials) / bit_position)
 *
 * which should give you a value from 0 - 100.  You can, of course,
 * take that anddo (100 - bit_uncertainty) to get the certainty, or
 * probability.  This is assuming the max_uncertainty is 100, of
 * course, you would need to adjust if you changed that.
 */
int convdecode_block(struct convcode *ce, const unsigned char *bytes,
		     unsigned int nbits, const uint8_t *uncertainty,
		     unsigned char *outbytes, unsigned int *output_uncertainty,
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
     * Used to report output bytes as they are collected after encoding
     * or decoding.  The last time this is called from convencode_finish()
     * or convdecode_finish() nbits may be < 8.
     */
    convcode_output enc_output;
    void *enc_user_data;

    /* The constraint, or polynomial size in bits.  Max is 16. */
    unsigned int k;

    /* Polynomials. */
    convcode_state polys[CONVCODE_MAX_POLYNOMIALS];
    unsigned int num_polys;

    bool do_tail;
    bool recursive;

    /* Current state. */
    convcode_state enc_state;

    /*
     * For the given state, what is the encoded output?  Indexed first
     * by the bit, then by the state.
     */
    unsigned int *convert[2];

    /*
     * 2D Array indexed first by bit then by current state.
     */
    convcode_state *next_state[2];

    /*
     * Number of states in the state machine, 1 << (k - 1).
     */
    unsigned int num_states;

    /*
     * The bit trellis matrix.  The first array is an array of
     * pointers to arrays of convcode_state, one for each possible output
     * bit on decoding.  It is trellis_size elements.  Each array in
     * that is individually allocated and contains the state for a
     * specific input.  Each is num_states elements.
     */
    convcode_state *trellis;
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
     * The uncertainty that maps to 100% uncertain for soft decoding.
     * See the discussion on soft decoding above the
     * set_decode_max_uncertainty function.
     */
    uint8_t uncertainty_100;

    /*
     * When reading bits for decoding, there may be some left over if
     * there weren't enough bits for the whole operation.  Store those
     * here for use in the next decode call.
     */
    unsigned int leftover_bits;
    convcode_state leftover_bits_data;
    uint8_t leftover_uncertainty[CONVCODE_MAX_POLYNOMIALS];
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
int setup_convcode1(struct convcode *ce, unsigned int k,
		    convcode_state *polynomials, unsigned int num_polynomials,
		    unsigned int max_decode_len_bits,
		    bool do_tail, bool recursive);

/* See the above discussion for how to use this. */
void setup_convcode2(struct convcode *ce);

#endif /* CONVCODE_H */
