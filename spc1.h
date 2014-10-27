/*

Copyright 2005-2009 NetApp, Incorporated.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

     THIS SOFTWARE IS PROVIDED BY NETAPP, INCORPORATED
     ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
     BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
     AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
     NO EVENT SHALL NETAPP, INC. BE LIABLE FOR
     ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
     OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
     PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
     DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
     AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
     IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/*
 * Revision History
 *
 * Version 1.2
 *    August 28, 2009.  Fixed an overflow bug for large ASUs
 * Version 1.1
 *    June 5, 2005.  Added multiple state blocks.
 * Version 1.0
 *    May 1, 2005.  First public version.
 *
 * $Id$
 */


/*
 * This file defines an SPC-1 I/O operation.
 */

struct spc1_io_s {
	unsigned int	asu:2;		/* which ASU? */
	unsigned int	dir:1;		/* read=0, write=1 */
	unsigned int	len:7;		/* length of transfer in units of 4KB */
	unsigned int	stream:3;	/* which stream in the bsu? */
	unsigned int	bsu:16;		/* which bsu? */
	unsigned int	pos;		/* in units of 4KB */
	unsigned int	when;		/* when to do this I/O in units of 0.1 milliseconds */
};

/*
 * Error codes
 */
enum {
    SPC1_ENOERR = 0,            /* Success */
    SPC1_ENOMEM = -1,           /* Memory allocation failed */
    SPC1_ESTYLE = -2,           /* Illegal HRRW style */
    SPC1_EHRRW  = -3,           /* Internal HRRW error */
    SPC1_EASU   = -4,           /* Internal ASU error */
};

/*
 * Generates the next operation.
 *
 * Parameters:
 *	context Which context block to use.  Must
 *		be in the range [0, n_contexts-1]
 *
 *
 * Returns:
 *	no error or one of the errors above.
 *	These errors "should never happen".
 *
 * Each I/O request generated will be requested either
 * at the same time as the previous request in this context
 * or at a later time.  Within a context time never runs
 * backwards.
 *
 * Running time is O(log(b))
 *
 * This routine is thread safe iff concurrent calls
 * use different values of context.
 *
 */
int spc1_next_op(struct spc1_io_s *, int context);

/*
 * Generate the next operation in any context.
 *
 * Finds the next spc1 operation in any context.
 * Linear in the number of contexts.
 * Most assuredly *not* thread safe.
 */
int spc1_next_op_any(struct spc1_io_s *);

/*
 * Initialize the SPC-1 I/O geneator.
 *
 * Parameters:
 *	m	Name of the program.  Used for error messages.
 *	b	Number of BSUs.
 *	a1	Size of ASU 1 in 4K blocks.
 *	a2	Size of ASU 2 in 4K blocks.
 *	a3	Size of ASU 3 in 4K blocks.
 *	n_contexts
 *		The number of context blocks to allocate
 *	version	An output buffer where a version string may
 *		be written.  If NULL, no version is written.
 *	len	The length of the output buffer.
 *
 * The only possible errors are internal programming errors or
 * a failure to allocate enough memory.  Memory requirement is
 * O(b) and quite modest.
 *
 * No checks are made to ensure the ASUs are the right size
 * relative to each other.
 *
 * No check is made if the ASUs are too small.  The minimum
 * size of ASU1 or ASU2 is 20 * 2**6 * 8 (= 10240) 4KB blocks
 * or about 40 MB.  Running with ASUs smaller than this
 * will produce non-conforming output.
 *
 * Version 1 of the SPC-1 benchmark uses multiple java virtual
 * machines (JVMs) to improve the benchmark's ability to scale to large
 * workloads.  Because the JVMs do not share state with each other
 * each JVM is a distinct context.  This workload generator
 * has the ability to use multiple contexts as well.
 * An implementation wishing close conformance with the specification
 * should set n_contexts=1.  An implementation wishing close
 * conformance with version on of the SPC workload generator
 * should set n_contexts = (b+99)/100.
 * 
 *
 * This routine is not thread safe.
 */

int spc1_init(char *m,
	int b,
	unsigned int a1,
	unsigned int a2,
	unsigned int a3,
	int n_contexts,
        char *version, int len);
