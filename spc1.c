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
 * Implement the SPC-1 workload.
 * Entry points are defined in spc1.h
 */

/*
 * Revision History
 *
 * Version 1.4.
 *    August 28, 2009.  Fixed an overflow bug for large ASUs
 * Version 1.3.
 *    July 12, 2005.  Better support for integer-only math.
 * Version 1.2
 *    June 24, 2005.  Better support for embedded systems.
 * Version 1.1
 *    June 5, 2005.  Added multiple state blocks.
 * Version 1.0
 *    May 1, 2005.  First public version.
 */

static char *Version = "V1.4: $Id$";

#define _XOPEN_SOURCE
#define _ISOC99_SOURCE
#define _XPG5

#ifdef _ONTAP_
#include "spc1_kernel.h"
#else
#define	VALIDATE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#endif

#include "spc1.h"

/*
 * Parameters.
 */
#ifdef VALIDATE
static char *myname;
#endif
static int asu1_size;
static int asu2_size;
static int asu3_size;
static int asu1_mult;
static int asu2_mult;
static int asu3_mult;

#define	IOPS_PER_BSU	50.
#ifndef TIME_UNITS_PER_SECOND
#define	TIME_UNITS_PER_SECOND	10000
#endif
#ifndef SPC1_USE_INTEGER_MATH
#define SPC1_USE_INTEGER_MATH 0
#endif

#if SPC1_USE_INTEGER_MATH == 1
#define PERCENT(x,p) ((int)(((unsigned long)(x)*(p))/100))
/*
 * These constants represent the stream intensities.
 * The are scaled by a factor of 5000.= 100 * IOPS per BSU
 */
#define IN018       90
#define IN035      175
#define IN070      350
#define IN210     1050
#define IN281     1405
#else
#define PERCENT(x,p) ((int)((double)(x)*((p)/100.0)))
#define IN018    0.018
#define IN035    0.035
#define IN070    0.070
#define IN210    0.210
#define IN281    0.281
#endif

/*
 * Some discussion of HRRW implemetations:
 * CLASSIC is what is implemented in the version 1 SPC workload generator.
 *
 * V2 enlarges the tree and loops for out-of-bounds requests.
 * The version two generator will do this, but with a bound of 100
 * loops. 
 *
 * FIXED uses a smaller tree, like classic, but fixes the data
 * sharing model to do the right thing.
 */


#define	HRRW_CLASSIC	1	/* As implemented */
#define	HRRW_FIXED	2	/* Simple Fix */
#define	HRRW_V2		3	/* As Proposed */

#define	HRRW_BLOCKS_PER_LEAF	8
#define	HRRW_V2_RETRY	100

static int hrrw_style = HRRW_CLASSIC;

static int
rnd(int n);

/*
 * An I/O Stream
 */
struct io_state_s {
	unsigned char i_stream_id;
	unsigned int i_next_time;
	unsigned char i_op;
	unsigned char i_len;		/* units are 4K */
	unsigned int i_block_addr;	/* units are 4K */
	unsigned int i_end_addr;
		// for classic and fixed, this is the offset, in blocks,
		// between the start of teh hot spot and the start of
		// the hrrw tree.  For v2, this is zero.
	unsigned int i_hrrw_offset;
	char i_rewrite;			/* boolean */
	unsigned int i_rewrite_block;
};

/*
 * Stuff for the HRRW
 */
struct hrrw_s {
	unsigned int h_min_block;	// the start of the region

		// for classic and fixed, size of the tree in blocks.
		// for v2, size of the region in blocks.
	unsigned int h_tree_size;
	unsigned int h_n_levels;
	unsigned char *h_leaf_state;

		// for classic and fixed, delta is the difference,
		// in blocks, between the size of the hot spot
		// and the size of the hrrw tree..
		// For v2, 0.
	unsigned int h_delta;
};


/*
 * Context blocks.  (Eumlating multiple JVMs.
 */
static int n_state_blocks;

struct state_block_s {
	int stream_count;
	int bsu_count;
	struct io_state_s *io_heap;
	struct hrrw_s hrrw1, hrrw2, hrrw3;
};

static struct state_block_s *states;

/* legal stream ids: */
#define	ASU1_1		0
#define	ASU1_2		1
#define	ASU1_3		2
#define	ASU1_4		3
#define	ASU2_1		4
#define	ASU2_2		5
#define	ASU2_3		6
#define	ASU3_1		7
#define	BSU_STREAMS	 8

int stream_id_to_asu[] = {1, 1, 1, 1, 2, 2, 2, 3};

/* legal operations */
#define	OP_READ		0
#define	OP_WRITE	1

static int
hrrw_init(struct hrrw_s *hp, int pos, int size)
{
	int i;
	unsigned int n_leaves;
	unsigned int array_size;

	hp->h_min_block = pos;
	n_leaves = size / HRRW_BLOCKS_PER_LEAF;
	array_size = n_leaves * 2;
	hp->h_leaf_state = (unsigned char *)malloc(array_size);
	if (hp->h_leaf_state == (unsigned char *)0) {
		return SPC1_ENOMEM;
	}

	for (i = 0; i < array_size; i++)
		hp->h_leaf_state[i] = 0;


	hp->h_n_levels = 0;
	for (i = 1; i < n_leaves; i <<= 1)
		hp->h_n_levels++;

	/*
	 * At this point, i is the smallest power of 2
	 * equal to or larger than the HRRW region.
	 */

	switch (hrrw_style) {
	case HRRW_CLASSIC:
	case HRRW_FIXED:
		if (i > n_leaves) {
			hp->h_n_levels--;
			i >>= 1;
		}
		/* Now i is largest equal to or smaller than */
		hp->h_delta = HRRW_BLOCKS_PER_LEAF * (n_leaves - i);
		hp->h_tree_size = size - hp->h_delta;
		break;
	case HRRW_V2:
		hp->h_delta = 0;
		hp->h_tree_size = size;
		break;
	default:
		return SPC1_ESTYLE;
	}
	return SPC1_ENOERR;
}

static int
hrrw_per_stream(struct state_block_s *sp, struct hrrw_s *hp)
{
	int r;

	switch (hrrw_style) {
	case HRRW_CLASSIC:
		sp->io_heap->i_hrrw_offset =
			hp->h_min_block + rnd(hp->h_delta + 1);
		sp->io_heap->i_hrrw_offset &= ~0x07; /* 32KB (leaf) boundary */
		break;
	case HRRW_FIXED:
		r = rnd(hp->h_delta + 1);
		sp->io_heap->i_hrrw_offset = hp->h_min_block +
				r - (r % HRRW_BLOCKS_PER_LEAF);
		break;
	case HRRW_V2:
		sp->io_heap->i_hrrw_offset = hp->h_min_block;
		break;
	default:
		return SPC1_ESTYLE;
	}

	sp->io_heap->i_block_addr = sp->io_heap->i_hrrw_offset +
					rnd(hp->h_tree_size);
	sp->io_heap->i_rewrite = 0;
	return SPC1_ENOERR;
}

static int
init(struct state_block_s *sp, int bsu_count)
{
	int i;
	struct io_state_s *ip;
	int retcode;

	sp->bsu_count = bsu_count;
	sp->stream_count = sp->bsu_count * BSU_STREAMS;
	sp->io_heap = (struct io_state_s *)malloc(sp->stream_count *
			(sizeof (struct io_state_s)));
	
	if (sp->io_heap == (struct io_state_s *)0) {
		return SPC1_ENOMEM;
	}

	ip = sp->io_heap;
	for (i = 0; i < sp->stream_count; i++) {
		ip->i_next_time = 0;	/* flag value, means not init */
		ip++;
	}
	ip = sp->io_heap;
	for (i = 0; i < sp->bsu_count; i++) {
		ip->i_stream_id = ASU1_1;
		ip++;
		ip->i_stream_id = ASU1_2;
		ip++;
		ip->i_stream_id = ASU1_3;
		ip++;
		ip->i_stream_id = ASU1_4;
		ip++;
		ip->i_stream_id = ASU2_1;
		ip++;
		ip->i_stream_id = ASU2_2;
		ip++;
		ip->i_stream_id = ASU2_3;
		ip++;
		ip->i_stream_id = ASU3_1;
		ip++;
	}

	retcode = hrrw_init(&(sp->hrrw1), PERCENT(asu1_size,15), asu1_size/20);
	if (retcode)
		return retcode;

	retcode = hrrw_init(&(sp->hrrw2), PERCENT(asu1_size,70), asu1_size/20);
	if (retcode)
		return retcode;

	retcode = hrrw_init(&(sp->hrrw3), PERCENT(asu2_size,47), asu2_size/20);
	if (retcode)
		return retcode;

	return SPC1_ENOERR;
}

/*
 * Call requeue after updating the time on the
 * stream pointed to by io_heap.
 *
 * This is the guts of the heap algorithm.
 * 
 * The array represents a binary tree.
 * The tree has the property that tree[i] has children at
 * tree[2i+1] and [2i+2].
 *
 */
static void
requeue_i(struct state_block_s *sp, int n)
{
	struct io_state_s it;
	struct io_state_s *root, *left, *right;
	
	root = sp->io_heap + n;
	left = sp->io_heap + (2 * n + 1);
	right = left + 1;
	if (2 * n + 1 >= sp->stream_count)
		left = (struct io_state_s *)0;
	if (2 * n + 2 >= sp->stream_count)
		right = (struct io_state_s *)0;

	/* If we are already a head, return */
	if ((!left || root->i_next_time <= left->i_next_time) &&
	    (!right || root->i_next_time <= right->i_next_time))
	    	return;

	it = *root;
	if (!right || left->i_next_time <= right->i_next_time) {
		/* use left tree */
		*root = *left;
		*left = it;
		requeue_i(sp, 2 * n + 1);
	} else {
		/* use right tree */
		*root = *right;
		*right = it;
		requeue_i(sp, 2 * n + 2);
	}
}

static void
requeue(struct state_block_s *sp)
{
	requeue_i(sp, 0);
}

/*
 * These routines generate the next I/O request.
 */
static int
rnd(int n)
{
#ifdef VALIDATE
	if (n <= 1) {
		fprintf(stderr, "%s: INTERNAL ERROR rnd(%d)\n",
			myname, n);
		exit(1);
	}
#endif
	return lrand48() % n;
}

static int smix_lengths[] =
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	  2, 2, 2, 2, 2, 2,
	  4, 4, 4, 4, 4,
	  8, 8,
	  16, 16,
	};

static int
smix(void)
{
	return smix_lengths[rnd(sizeof smix_lengths / sizeof (int))];
}
	

#if SPC1_USE_INTEGER_MATH == 1
#define	RAND	((unsigned)lrand48()*2)
#define	T24	  16777216
#define	T23	   8388608

#define	B      346574		// 50 * BF * -ln(.5)
#define BF      10000		// factor for time unit correction of B

#define	AN	7	// number of coefficients in our expansion

#if AN == 5
long long c[AN] = {	// chebyshev coeffcients scaled by 2^24
	-29137993,
	47093318,
	-24414117,
	7390409,
	-930452
};
#endif

#if AN == 7
long long c[AN] = {	// chebyshev coeffcients scaled by 2^24
	-35289639,
	70819884,
	-61653999,
	37788306,
	-14512655,
	3140287,
	-292159
};
#endif

/*
 * v is any non-zero unsigned integer.
 *
 * returns 50 * BF * log(v/(2**32)) as a signed 64 bit integer
 */

static long long
ilog(unsigned int v)
{
	int i;
	int k;
	long long acc;
	long long vv;

	if (v == 0)
		return -100;	/* punt */

	k = 0;
	while (v > T24) {
		v >>= 1;
		k--;
	}

	while (v <= T23) {
		v <<= 1;
		k++;
	}
	vv = (long long)v;

	acc = c[0] * T23;

	for (i = 1; i < AN; i++) {
		acc += c[i] * vv;
		vv = ((vv * (long long)v) >> 23);
	}

        acc >>= 8;              /* get rid of some fractional part
                                 * before the multiply by BF */
	acc = (acc * (long long)(50 * BF)) >> (23+24-8);
	acc -= (k+9) * B;
	return acc;
}

/*
 * Call tnext with one of the intensities and get
 * back the number of milliseconds to the next I/O request
 *
 * Assumes there exists a RAND function that returns
 * an unsigned 32-bit random integer.
 */

static int
tnext(int intensity)
{
	unsigned int v;
	int factor = BF / TIME_UNITS_PER_SECOND;
	int result;

	do {
		v = RAND;
	} while (v == 0);

        /* When compared with the floating point version at 10000 and
         * 1000 time units per second, result will be identical about
         * 50% of the time, off by negative one about 25% of the time,
         * and off by positive one about 25% of the time.  (Tested with
         * 5m random numbers and an observed variation of about 1%.) */
        if (factor == 1) {
		result = (-ilog(v) + intensity/2) / intensity;
		result *= 2;
        } else {
		result = -ilog(v) / intensity;
		result = (result + factor/2) / factor;
		result *= 2;
        }

#ifdef VALIDATE
        if (result < 0)
	    fprintf(stderr, "%s: INTERNAL ERROR tnext(%d) returning %d\n",
		myname, intensity, result);
#endif

	return result;
}

#else /* SPC1_USE_INTEGER_MATH (floating point version follows) */

static double
exponential(double average)
{
	double d;

	d = drand48();
	if (d < 1.0E-8)
		d = 1.0E-8;
	return -log(d) * average;
}

/* how long till the next op at the stated intensity? */
static int
tnext(double intensity)
{
	double d;

	d = exponential(1. / (IOPS_PER_BSU * intensity));
	return (int) (d * (double)TIME_UNITS_PER_SECOND + .5);
}
#endif /* SPC1_USE_INTEGER_MATH */

static int
asu1_1(struct state_block_s *sp)
{
	sp->io_heap->i_next_time += tnext(IN035);
	sp->io_heap->i_op = (rnd(10) < 5)? OP_READ: OP_WRITE;
	sp->io_heap->i_len = 1;
	sp->io_heap->i_block_addr = rnd(asu1_size);
	return SPC1_ENOERR;
}

static int
asu1_2(struct state_block_s *sp)
{
	if (sp->io_heap->i_next_time == 0) {
		int retcode = hrrw_per_stream(sp, &(sp->hrrw1));
		if (retcode)
			return retcode;
	}
	sp->io_heap->i_next_time += tnext(IN281);
	sp->io_heap->i_op = (rnd(10) < 5)? OP_READ: OP_WRITE;
	sp->io_heap->i_len = 1;
	/* the hrrw stuff is computed at launch time, not now */
	return SPC1_ENOERR;
}

static int
asu1_3(struct state_block_s *sp)
{
	sp->io_heap->i_op = OP_READ;
	sp->io_heap->i_block_addr += sp->io_heap->i_len;
	sp->io_heap->i_len = smix();
	if (sp->io_heap->i_next_time == 0 ||
	    sp->io_heap->i_block_addr + sp->io_heap->i_len >= sp->io_heap->i_end_addr) {
	    	/* must initialize the seq read stream */
		sp->io_heap->i_block_addr =
			PERCENT(asu1_size, 20) + rnd(PERCENT(asu1_size, 40));
		sp->io_heap->i_end_addr =
			sp->io_heap->i_block_addr + PERCENT(asu1_size, 10);
		if (hrrw_style == HRRW_CLASSIC) { /* 64KB boundary */
			sp->io_heap->i_block_addr &= ~0x0f;
			sp->io_heap->i_end_addr &= ~0x0f;
		}
	}
	sp->io_heap->i_next_time += tnext(IN070);
	return SPC1_ENOERR;
}

static int
asu1_4(struct state_block_s *sp)
{
	if (sp->io_heap->i_next_time == 0) {
		int retcode = hrrw_per_stream(sp, &(sp->hrrw2));
		if (retcode)
			return retcode;
	}
	sp->io_heap->i_next_time += tnext(IN210);
	sp->io_heap->i_op = (rnd(10) < 5)? OP_READ: OP_WRITE;
	sp->io_heap->i_len = 1;
	/* the hrrw stuff is computed at launch time, not now */
	return SPC1_ENOERR;
}

static int
asu2_1(struct state_block_s *sp)
{
	sp->io_heap->i_next_time += tnext(IN018);
	sp->io_heap->i_op = (rnd(10) < 3)? OP_READ: OP_WRITE;
	sp->io_heap->i_len = 1;

	if (hrrw_style == HRRW_CLASSIC) /* XXX FIXME do this always? */
		sp->io_heap->i_block_addr = rnd(asu2_size);
	else
		sp->io_heap->i_block_addr = (rnd(asu2_size) / 2) * 2;
	return SPC1_ENOERR;
}

static int
asu2_2(struct state_block_s *sp)
{
	if (sp->io_heap->i_next_time == 0) {
		int retcode = hrrw_per_stream(sp, &(sp->hrrw3));
		if (retcode)
			return retcode;
	}
	sp->io_heap->i_next_time += tnext(IN070);
	sp->io_heap->i_op = (rnd(10) < 3)? OP_READ: OP_WRITE;
	sp->io_heap->i_len = 1;
	/* the hrrw stuff is computed at launch time, not now */
	return SPC1_ENOERR;
}

static int
asu2_3(struct state_block_s *sp)
{
	sp->io_heap->i_op = OP_READ;
	sp->io_heap->i_block_addr += sp->io_heap->i_len;
	sp->io_heap->i_len = smix();
	if (sp->io_heap->i_next_time == 0 ||
	    sp->io_heap->i_block_addr + sp->io_heap->i_len >= sp->io_heap->i_end_addr) {
	    	/* must initialize the seq read stream */
		sp->io_heap->i_block_addr =
			PERCENT(asu2_size, 20) + rnd(PERCENT(asu2_size, 40));
		sp->io_heap->i_end_addr =
			sp->io_heap->i_block_addr + PERCENT(asu2_size, 10);
		if (hrrw_style == HRRW_CLASSIC) { /* 64KB boundary */
			sp->io_heap->i_block_addr &= ~0x0f;
			sp->io_heap->i_end_addr &= ~0x0f;
		}
	}
	sp->io_heap->i_next_time += tnext(IN035);
	return SPC1_ENOERR;
}

static int
asu3_1(struct state_block_s *sp)
{
	sp->io_heap->i_op = OP_WRITE;
	sp->io_heap->i_block_addr += sp->io_heap->i_len;
	sp->io_heap->i_len = smix();
	if (sp->io_heap->i_next_time == 0 ||
	    sp->io_heap->i_block_addr + sp->io_heap->i_len >= sp->io_heap->i_end_addr) {
	    	/* must initialize the seq write stream */
		sp->io_heap->i_block_addr = rnd(PERCENT(asu3_size, 70));
		sp->io_heap->i_end_addr =
			sp->io_heap->i_block_addr + PERCENT(asu3_size, 30);
		if (hrrw_style == HRRW_CLASSIC) { /* 64KB boundary */
			sp->io_heap->i_block_addr &= ~0x0f;
			sp->io_heap->i_end_addr &= ~0x0f;
		}
	}
	sp->io_heap->i_next_time += tnext(IN281);
	return SPC1_ENOERR;
}

/*
 * Implement the actual hierarchical resuse random walk!
 */
static int
hrrw(struct hrrw_s *hp, struct io_state_s *ip)
{
	unsigned int h, th;
	unsigned int old_leaf;
	unsigned int new_leaf;
	unsigned int block;
	int retry_count;

	/* if mode is write and we need to repeat, do so */
	if (ip->i_op == OP_WRITE && ip->i_rewrite) {
		ip->i_rewrite = 0;
		ip->i_block_addr = ip->i_rewrite_block;
		return SPC1_ENOERR;
	}

	if (ip->i_block_addr < ip->i_hrrw_offset) {
		return SPC1_EHRRW;
	}
	old_leaf = (ip->i_block_addr - ip->i_hrrw_offset) /
			HRRW_BLOCKS_PER_LEAF;

	retry_count = HRRW_V2_RETRY;

    again:
	h = 6;
	th = 64; // 2 ** h
        if (hrrw_style == HRRW_CLASSIC) { /* k=7 */
            ++h;
            th *= 2;
        }
	while (h < hp->h_n_levels && rnd(100) < 44) {
		h++;
		th *= 2;
	}

	new_leaf = th * (old_leaf / th) + rnd(th);
	if (ip->i_op == OP_WRITE)
		new_leaf -= new_leaf % 8;

	/*
	 * At this point, new_leaf is a position in the binary
	 * tree.  Now we need to convert it into an index
	 * into the leaf state array.
	 */
	switch (hrrw_style) {
	case HRRW_CLASSIC:
		// no conversion necessary
		break;
	case HRRW_FIXED:
		new_leaf += (ip->i_hrrw_offset - hp->h_min_block) /
			HRRW_BLOCKS_PER_LEAF;
		break;
	case HRRW_V2:
		// truncate the distribution
		if (new_leaf > hp->h_tree_size / HRRW_BLOCKS_PER_LEAF) {
			old_leaf = new_leaf;
			if (retry_count -- > 0)
				goto again;
			new_leaf = rnd(hp->h_tree_size / HRRW_BLOCKS_PER_LEAF);
		}
		break;
	}

	if (ip->i_op == OP_READ) {
		// on read, cycle through the 8 blocks
		block = hp->h_leaf_state[new_leaf];
		hp->h_leaf_state[new_leaf] = ((block + 1) %
				HRRW_BLOCKS_PER_LEAF);
	} else {
		// op is write
		if (rnd(100) < 50)
			// 50% of the time pick a random block
			block = rnd(HRRW_BLOCKS_PER_LEAF);
		else {
			// 50% of the time use the last read block
			// (*not* the next read block)
			block = hp->h_leaf_state[new_leaf];
			if (hrrw_style != HRRW_CLASSIC) {
				if (block == 0)
					block = HRRW_BLOCKS_PER_LEAF;
				block--;
			}

		}
		if (rnd(100) < 15) 
			// 15% of the time do two writes
			ip->i_rewrite = 1;
	}

	if (hrrw_style == HRRW_FIXED)
		block += new_leaf * HRRW_BLOCKS_PER_LEAF + hp->h_min_block;
	else
		block += new_leaf * HRRW_BLOCKS_PER_LEAF + ip->i_hrrw_offset;

	if (ip->i_op == OP_WRITE)
            ip->i_rewrite_block = block;
	ip->i_block_addr = block;

	return SPC1_ENOERR;
}

/*
 * Generate one I/O
 */
static int
gen_io_i(struct spc1_io_s *spc1_io, struct state_block_s *sp)
{
#ifdef VALIDATE
	int ignore;
	int s;
#endif
	int retcode = SPC1_ENOERR;

        memset(spc1_io, 0, sizeof(*spc1_io));

	/*
	 * Skip the I/O if we are not initialized.
	 */
#ifdef VALIDATE
    again:
	ignore = 0;
#endif
	if (sp->io_heap->i_next_time) {
		/*
		 * For HRRW, take the HRRW step now
		 */
		switch(sp->io_heap->i_stream_id) {
		case ASU1_2:
			retcode = hrrw(&(sp->hrrw1), sp->io_heap);
			break;
		case ASU1_4:
			retcode =hrrw(&(sp->hrrw2), sp->io_heap);
			break;
		case ASU2_2:
			retcode = hrrw(&(sp->hrrw3), sp->io_heap);
			break;
		default:
			break;
		}
		if (retcode)
			return retcode;

#ifdef VALIDATE
		switch(stream_id_to_asu[sp->io_heap->i_stream_id]) {
		case 3:
			s = asu3_size;
			break;
		case 2:
			s = asu2_size;
			break;
		case 1:
			s = asu1_size;
			break;
		default:
			fprintf(stderr, "%s: VALIDATE ERROR asu=%d\n",
				myname,
			    stream_id_to_asu[sp->io_heap->i_stream_id]);
			exit(1);
		}
		ignore = 0;
		if (s < sp->io_heap->i_len + sp->io_heap->i_block_addr) {
		    fprintf(stderr, "%s: VALIDATE ERROR asu=%d\n",
			myname,
		    stream_id_to_asu[sp->io_heap->i_stream_id]);
		    fprintf(stderr, "\tstream = %d\n", sp->io_heap->i_stream_id);
		    fprintf(stderr, "\tsize = %d\n", s);
		    fprintf(stderr, "\tlen = %d\n", sp->io_heap->i_len);
		    fprintf(stderr, "\tpos = %d\n", sp->io_heap->i_block_addr);
		    ignore = 1;
		}
#endif

		spc1_io->asu = stream_id_to_asu[sp->io_heap->i_stream_id];
		spc1_io->dir = sp->io_heap->i_op;
		spc1_io->len = sp->io_heap->i_len;
		spc1_io->stream = sp->io_heap->i_stream_id;
		spc1_io->bsu = 0; /* we don't actually know. */
		spc1_io->pos = sp->io_heap->i_block_addr;
		spc1_io->when = sp->io_heap->i_next_time;
	}

	switch(sp->io_heap->i_stream_id) {
	case ASU1_1:
		retcode = asu1_1(sp);
		break;
	case ASU1_2:
		retcode = asu1_2(sp);
		break;
	case ASU1_3:
		retcode = asu1_3(sp);
		break;
	case ASU1_4:
		retcode = asu1_4(sp);
		break;
	case ASU2_1:
		retcode = asu2_1(sp);
		break;
	case ASU2_2:
		retcode = asu2_2(sp);
		break;
	case ASU2_3:
		retcode = asu2_3(sp);
		break;
	case ASU3_1:
		retcode = asu3_1(sp);
		break;
	default:
		return SPC1_EASU;
	}
#ifdef VALIDATE
	if (ignore)
		goto again;
#endif

	/*
         * Do a fixup for small ASU sizes.  This allows the code to run,
         * even though the results may not be representative of SPC-1
         * results.
	 */
        switch(stream_id_to_asu[sp->io_heap->i_stream_id]) {
        case 1:  spc1_io->pos /= asu1_mult; break;
        case 2:  spc1_io->pos /= asu2_mult; break;
        case 3:  spc1_io->pos /= asu3_mult; break;
        default: return SPC1_EASU;
        }
            
	return retcode;
}

int
spc1_next_op(struct spc1_io_s *s, int context)
{
	struct state_block_s *sp;
	int retcode;

	/*
	 * context is supposed to be in the range [0, n_state_blocks-1].
	 * However, it is simpler to fix it than to throw an error.
	 */
	if (context < 0)
		context = 0;
	sp = states + (context % n_state_blocks);

	retcode = gen_io_i(s, sp);
	requeue(sp);
	return retcode;
}

int
spc1_next_op_any(struct spc1_io_s *s)
{
	struct state_block_s *sp;
	static int last_context = 0;
	int i, best_context;
	int f, best_time;
	int retcode;

	/*
	 * Loop over all contexts finding the next op
	 * The complexity is added to break ties in a round-robin
	 * fashion.
	 */
	f = 0;
	best_time = 0;		/* suppress warnings */
	best_context = 0;	/* suppress warnings */
	for (i = 0; i < n_state_blocks; i++) {
		sp = states + ((i + last_context + 1) % n_state_blocks);
		if (!f || best_time > sp->io_heap->i_next_time) {
			f = 1;
			best_time = sp->io_heap->i_next_time;
			best_context = sp - states;
		}
	}

	last_context = best_context;
	sp = states + best_context;

	retcode = gen_io_i(s, sp);
	requeue(sp);
	return retcode;
}

/*
 * ASU1 and ASU2 have a minimum size or the hot spots do not work.
 * If the requested ASU is smaller than the legal minimum, we make
 * the ASU bigger by some mulitplier and then divide the answers by
 * the same multiplier.
 * This algorithm does not comply with the letter of the specification,
 * but this is better than throwing an error and more practical than
 * fixing all the HRRW walk code to deal with very small regions.
 */

static int
spc1_compute_multiplier(int size, int min)
{
    int mult  = 1;
    int total = size;

    if (total >= min)
	return 1;

    while (total * 10 < min) {
        total += 10 * size;
        mult += 10;
    }
    
    while (total < min) {
        total += size;
        ++mult;
    }
    return mult;
}

int
spc1_init(char *m,
	int b,
	unsigned int a1,
	unsigned int a2,
	unsigned int a3,
	int n_contexts,
	char *version,
	int len)
{
	int i, j;
	int mbc, rbc;
	unsigned u;
	struct state_block_s *sp;
	int retcode;

#ifdef VALIDATE
	myname = m;
#endif

	if (n_contexts < 1)
		n_contexts = 1;
        n_state_blocks = n_contexts;

        /* Provide a version string even if we fail early */
	if (version)
            snprintf(version, len,
                     "%s [bsu=%d asu1=%d asu2=%d asu3=%d ctx=%d mth=%s t=%d]",
                     Version, b, a1, a2, a3, n_contexts,
                     SPC1_USE_INTEGER_MATH ? "i" : "f",
                     TIME_UNITS_PER_SECOND);

	u = (unsigned)(n_state_blocks * sizeof (struct state_block_s));
	states = (struct state_block_s *)malloc(u);
	if (states == (struct state_block_s *)0) {
		return SPC1_ENOMEM;
	}

        asu1_mult = spc1_compute_multiplier(a1, 128 * 8 * 20); /* 2^7 * 32k / 5% */
        asu2_mult = spc1_compute_multiplier(a2, 128 * 8 * 20); /* 2^7 * 32k / 5% */
        asu3_mult = 1;

	asu1_size = a1 * asu1_mult;
	asu2_size = a2 * asu2_mult;
	asu3_size = a3 * asu3_mult;

	mbc = b / n_state_blocks;
	rbc = b % n_state_blocks;

        /* Provide a full version string after multipliers are calculated */
	if (version)
            snprintf(version, len,
                     "%s [bsu=%d asu1=%d*%d asu2=%d*%d asu3=%d*%d"
                     " ctx=%d mth=%s t=%d]",
                     Version, b, a1, asu1_mult, a2, asu2_mult, a3,
                     asu3_mult, n_contexts,
                     SPC1_USE_INTEGER_MATH ? "i" : "f",
                     TIME_UNITS_PER_SECOND);

	for (i = 0; i < n_state_blocks; i++) {
		sp = states + i;

		/*
		 * The mbc/rbc stuff distributes the streams evenly
		 * over the state blocks.
		 */
		retcode = init(sp, mbc + (rbc-- > 0? 1: 0));
		if (retcode)
			return retcode;

		/*
		 * Initialize each of the streams
		 */
		for (j = 0; j < sp->stream_count; j++) {
			struct spc1_io_s spc1_io;
			retcode = gen_io_i(&spc1_io, sp);
			if (retcode)
				return retcode;
			requeue(sp);
		}
	}
	return SPC1_ENOERR;
}
