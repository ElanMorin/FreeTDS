/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-1999  Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * iconv.c, handle all the conversion stuff without spreading #if HAVE_ICONV 
 * all over the other code
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include "tds.h"
#include "tdsiconv.h"
#if HAVE_ICONV
#include <iconv.h>
#endif
#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char software_version[] = "$Id: iconv.c,v 1.40 2003/03/26 16:20:50 freddy77 Exp $";
static void *no_unused_var_warn[] = {
	software_version,
	no_unused_var_warn
};

/**
 * \ingroup libtds
 * \defgroup conv Charset conversion
 * Convert between different charsets
 */

/**
 * \addtogroup conv
 * \@{ 
 */

void
tds_iconv_open(TDSSOCKET * tds, char *charset)
{
	TDSICONVINFO *iconv_info;

	iconv_info = (TDSICONVINFO *) tds->iconv_info;

#if HAVE_ICONV
	tdsdump_log(TDS_DBG_FUNC, "iconv will convert client-side data to the \"%s\" character set\n", charset);
	iconv_info->cdto_ucs2 = iconv_open("UCS-2LE", charset);
	if (iconv_info->cdto_ucs2 == (iconv_t) - 1) {
		iconv_info->use_iconv = 0;
		tdsdump_log(TDS_DBG_FUNC, "%L iconv_open: cannot convert to \"%s\"\n", charset);
		return;
	}
	iconv_info->cdfrom_ucs2 = iconv_open(charset, "UCS-2LE");
	if (iconv_info->cdfrom_ucs2 == (iconv_t) - 1) {
		iconv_info->use_iconv = 0;
		tdsdump_log(TDS_DBG_FUNC, "%L iconv_open: cannot convert from \"%s\"\n", charset);
		return;
	}
	/* TODO init singlebyte server */
	iconv_info->use_iconv = 1;
	/* temporarily disable */
	/* iconv_info->use_iconv = 0; */
#else
	iconv_info->use_iconv = 0;
	tdsdump_log(TDS_DBG_FUNC, "%L iconv library not employed, relying on ISO-8859-1 compatibility\n");
#endif
}

void
tds_iconv_close(TDSSOCKET * tds)
{
TDSICONVINFO *iconv_info;

	iconv_info = (TDSICONVINFO *) tds->iconv_info;

#if HAVE_ICONV
	if (iconv_info->cdto_ucs2 != (iconv_t) - 1) {
		iconv_close(iconv_info->cdto_ucs2);
	}
	if (iconv_info->cdfrom_ucs2 != (iconv_t) - 1) {
		iconv_close(iconv_info->cdfrom_ucs2);
	}
	if (iconv_info->cdto_srv != (iconv_t) - 1) {
		iconv_close(iconv_info->cdto_srv);
	}
	if (iconv_info->cdfrom_srv != (iconv_t) - 1) {
		iconv_close(iconv_info->cdfrom_srv);
	}
#endif
}

/**
 * convert from ucs2 string to ascii.
 * @return saved bytes
 * @param in_string ucs2 string (not terminated) to convert to ascii
 * @param in_len length of input string in characters (2 byte)
 * @param out_string buffer to store translated string. It should be large enough 
 *        to handle out_len bytes. string won't be zero terminated.
 * @param out_len length of input string in characters
 */
int
tds7_unicode2ascii(TDSSOCKET * tds, const char *in_string, int in_len, char *out_string, int out_len)
{
	int i;

#if HAVE_ICONV
	TDSICONVINFO *iconv_info;
	ICONV_CONST char *in_ptr;
	char *out_ptr;
	size_t out_bytes, in_bytes;
	char quest_mark[] = "?\0";	/* best to live no-const */
	ICONV_CONST char *pquest_mark;
	size_t lquest_mark;
#endif

	if (!in_string)
		return 0;

#if HAVE_ICONV
	iconv_info = (TDSICONVINFO *) tds->iconv_info;
	if (iconv_info->use_iconv) {
		out_bytes = out_len;
		in_bytes = in_len * 2;
		in_ptr = (ICONV_CONST char *) in_string;
		out_ptr = out_string;
		while (iconv(iconv_info->cdfrom_ucs2, &in_ptr, &in_bytes, &out_ptr, &out_bytes) == (size_t) - 1) {
			/* iconv call can reset errno */
			i = errno;
			/* reset iconv state */
			iconv(iconv_info->cdfrom_ucs2, NULL, NULL, NULL, NULL);
			if (i != EILSEQ)
				break;

			/* skip one UCS-2 sequence */
			in_ptr += 2;
			in_bytes -= 2;

			/* replace invalid with '?' */
			pquest_mark = quest_mark;
			lquest_mark = 2;
			iconv(iconv_info->cdfrom_ucs2, &pquest_mark, &lquest_mark, &out_ptr, &out_bytes);
			if (out_bytes == 0)
				break;
		}
		return out_len - out_bytes;
	}
#endif

	/* no iconv, strip high order byte if zero or replace with '?' 
	 * this is the same of converting to ISO8859-1 charset using iconv */
	/* TODO update docs */
	if (out_len < in_len)
		in_len = out_len;
	for (i = 0; i < in_len; ++i) {
		out_string[i] = in_string[i * 2 + 1] ? '?' : in_string[i * 2];
	}
	return in_len;
}

/**
 * convert a ascii string to ucs2.
 * Note: output string is not terminated
 * @param in_string string to translate, null terminated
 * @param out_string buffer to store translated string
 * @param maxlen length of out_string buffer in bytes
 */
char *
tds7_ascii2unicode(TDSSOCKET * tds, const char *in_string, char *out_string, int maxlen)
{
	register int out_pos = 0;
	register int i;
	size_t string_length;

#if HAVE_ICONV
	TDSICONVINFO *iconv_info;
	ICONV_CONST char *in_ptr;
	char *out_ptr;
	size_t out_bytes, in_bytes;
#endif

	if (!in_string)
		return NULL;
	string_length = strlen(in_string);

#if HAVE_ICONV
	iconv_info = (TDSICONVINFO *) tds->iconv_info;
	if (iconv_info->use_iconv) {
		out_bytes = maxlen;
		in_bytes = string_length;
		in_ptr = (ICONV_CONST char *) in_string;
		out_ptr = out_string;
		iconv(iconv_info->cdto_ucs2, &in_ptr, &in_bytes, &out_ptr, &out_bytes);

		return out_string;
	}
#endif

	/* no iconv, add null high order byte to convert 7bit ascii to unicode */
	if (string_length * 2 > maxlen)
		string_length = maxlen >> 1;

	for (i = 0; i < string_length; i++) {
		out_string[out_pos++] = in_string[i];
		out_string[out_pos++] = '\0';
	}

	return out_string;
}

/** \@} */
