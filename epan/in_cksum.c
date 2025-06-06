/* in_cksum.c
 * 4.4-Lite-2 Internet checksum routine, modified to take a vector of
 * pointers/lengths giving the pieces to be checksummed.  Also using
 * Tahoe/CGI version of ADDCARRY(x) macro instead of from portable version.
 *
 * Copyright (c) 1988, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *	@(#)in_cksum.c	8.1 (Berkeley) 6/10/93
 */

#include "config.h"

#include <glib.h>

#include <epan/tvbuff.h>
#include <epan/in_cksum.h>

/*
 * Checksum routine for Internet Protocol family headers (Portable Version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define ADDCARRY(x)  {if ((x) > 65535) (x) -= 65535;}
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);}

/*
 * Linux and Windows, at least, when performing Local Checksum Offload
 * store the one's complement sum (not inverted to its bitwise complement)
 * of the pseudo header in the checksum field (instead of intializing
 * to zero), allowing the device driver to calculate the real checksum
 * later without needing knowledge of the pseudoheader itself.
 * (This is presumably why GSO requires equal length buffers - so that the
 * pseudo header contribution to the checksum, which includes the payload
 * length, is the same.)
 *
 * We can output this partial checksum as an intermediate result,
 * assuming that the pseudo header is all but the last chunk in the vector.
 * Note that unlike the final output it is not inverted, and that it
 * (like the final computed checksum) is is network byte order.
 */
int
in_cksum_ret_partial(const vec_t *vec, int veclen, uint16_t *partial)
{
	register const uint16_t *w;
	register int sum = 0;
	register int mlen = 0;
	int byte_swapped = 0;

	union {
		uint8_t	c[2];
		uint16_t	s;
	} s_util;
	union {
		uint16_t s[2];
		uint32_t	l;
	} l_util;

	for (; veclen != 0; vec++, veclen--) {
		if (veclen == 1 && partial) {
			REDUCE;
			*partial = sum;
		}
		if (vec->len == 0)
			continue;
		w = (const uint16_t *)(const void *)vec->ptr;
		if (mlen == -1) {
			/*
			 * The first byte of this chunk is the continuation
			 * of a word spanning between this chunk and the
			 * last chunk.
			 *
			 * s_util.c[0] is already saved when scanning previous
			 * chunk.
			 */
			s_util.c[1] = *(const uint8_t *)w;
			sum += s_util.s;
			w = (const uint16_t *)(const void *)((const uint8_t *)w + 1);
			mlen = vec->len - 1;
		} else
			mlen = vec->len;
		/*
		 * Force to even boundary.
		 */
		if ((1 & (intptr_t)w) && (mlen > 0)) {
			REDUCE;
			sum <<= 8;
			s_util.c[0] = *(const uint8_t *)w;
			w = (const uint16_t *)(const void *)((const uint8_t *)w + 1);
			mlen--;
			byte_swapped = 1;
		}
		/*
		 * Unroll the loop to make overhead from
		 * branches &c small.
		 */
		while ((mlen -= 32) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
			sum += w[8]; sum += w[9]; sum += w[10]; sum += w[11];
			sum += w[12]; sum += w[13]; sum += w[14]; sum += w[15];
			w += 16;
		}
		mlen += 32;
		while ((mlen -= 8) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			w += 4;
		}
		mlen += 8;
		if (mlen == 0 && byte_swapped == 0)
			continue;
		REDUCE;
		while ((mlen -= 2) >= 0) {
			sum += *w++;
		}
		if (byte_swapped) {
			REDUCE;
			sum <<= 8;
			byte_swapped = 0;
			if (mlen == -1) {
				s_util.c[1] = *(const uint8_t *)w;
				sum += s_util.s;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1)
			s_util.c[0] = *(const uint8_t *)w;
	}
	if (mlen == -1) {
		/* The last mbuf has odd # of bytes. Follow the
		   standard (the odd byte may be shifted left by 8 bits
		   or not as determined by endian-ness of the machine) */
		s_util.c[1] = 0;
		sum += s_util.s;
	}
	REDUCE;
	return (~sum & 0xffff);
}

int
in_cksum(const vec_t *vec, int veclen)
{
	return in_cksum_ret_partial(vec, veclen, NULL);
}

uint16_t
ip_checksum(const uint8_t *ptr, int len)
{
	vec_t cksum_vec[1];

	SET_CKSUM_VEC_PTR(cksum_vec[0], ptr, len);
	return in_cksum_ret_partial(&cksum_vec[0], 1, NULL);
}

uint16_t
ip_checksum_tvb(tvbuff_t *tvb, int offset, int len)
{
	vec_t cksum_vec[1];

	SET_CKSUM_VEC_TVB(cksum_vec[0], tvb, offset, len);
	return in_cksum_ret_partial(&cksum_vec[0], 1, NULL);
}

/*
 * Given the host-byte-order value of the checksum field in a packet
 * header, and the network-byte-order computed checksum of the data
 * that the checksum covers (including the checksum itself), compute
 * what the checksum field *should* have been.
 *
 * This always returns +0 (0x0000) not -0 (0xffff). The few protocols,
 * like ICMP, that can have an all zero packet (aside from the checksum
 * field) such that 0xffff is the correct result should test for that
 * case before, after, or instead of calling this method.
 */
uint16_t
in_cksum_shouldbe(uint16_t sum, uint16_t computed_sum)
{
	uint32_t shouldbe;

	/*
	 * The value that should have gone into the checksum field
	 * is the negative of the value gotten by summing up everything
	 * *but* the checksum field.
	 *
	 * We can compute that by subtracting the value of the checksum
	 * field from the sum of all the data in the packet, and then
	 * computing the negative of that value.
	 *
	 * "sum" is the value of the checksum field, and "computed_sum"
	 * is the negative of the sum of all the data in the packets,
	 * so that's -(-computed_sum - sum), or (sum + computed_sum).
	 *
	 * All the arithmetic in question is one's complement, so the
	 * addition must include an end-around carry; we do this by
	 * doing the arithmetic in 32 bits (with no sign-extension),
	 * and then adding the upper 16 bits of the sum, which contain
	 * the carry, to the lower 16 bits of the sum, and then do it
	 * again in case *that* sum produced a carry. (XXX - It won't.
	 * It can't be any larger than 0xFFFF + 0xFFFF = 0x1FFFE,
	 * which only carries once back to 0xFFFF.)
	 *
	 * Also, since all the arithmetic is one's complement, +0 (0x0000)
	 * and -0 (0xFFFF) are indistinguishable. (Different ways of
	 * performing the calculation can yield one zero or the other for
	 * different inputs.) Between the two, +0 is the representation we
	 * want unless all the bits of the packet except for the checksum
	 * field are zero, but we can't know that from our inputs here.
	 * Most protocols which use this checksum require at least some
	 * nonzero bits (a version, a length field, something either directly
	 * or in a pseudoheader), and so +0 is correct. Despite this, some
	 * networking stacks put 0xFFFF when 0x0000 is appropriate, see RFC
	 * 1624.
	 *
	 * As RFC 1071 notes, the checksum can be computed without
	 * byte-swapping the 16-bit words; summing 16-bit words
	 * on a big-endian machine gives a big-endian checksum, which
	 * can be directly stuffed into the big-endian checksum fields
	 * in protocol headers, and summing words on a little-endian
	 * machine gives a little-endian checksum, which must be
	 * byte-swapped before being stuffed into a big-endian checksum
	 * field.
	 *
	 * "computed_sum" is a network-byte-order value, so we must put
	 * it in host byte order before subtracting it from the
	 * host-byte-order value from the header; the adjusted checksum
	 * will be in host byte order, which is what we'll return.
	 */
	shouldbe = sum;
	shouldbe += g_ntohs(computed_sum);
	shouldbe = (shouldbe & 0xFFFF) + (shouldbe >> 16);
	shouldbe = (shouldbe & 0xFFFF) + (shouldbe >> 16); // XXX - Unneeded.
	/* Always return +0, not -0.
	 * There are some other ways to always return +0, such as
	 * calculating -(-computed_sum + -sum) instead; the fastest
	 * approach is likely CPU and compiler dependent.
	 */
	return shouldbe == 0xFFFF ? 0 : shouldbe;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
