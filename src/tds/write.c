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

#include <assert.h>

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if TIME_WITH_SYS_TIME
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "tds.h"
#include "tdsiconv.h"
#include <signal.h>		/* GW ADDED */
#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char software_version[] = "$Id: write.c,v 1.36 2003/04/08 07:14:11 jklowden Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static int tds_write_packet(TDSSOCKET * tds, unsigned char final);

/** \addtogroup network
 *  \@{ 
 */

/*
 * CRE 01262002 making buf a void * means we can put any type without casting
 *		much like read() and memcpy()
 */
int
tds_put_n(TDSSOCKET * tds, const void *buf, int n)
{
	int i;
	const unsigned char *bufp = (const unsigned char *) buf;

	if (bufp) {
		for (i = 0; i < n; i++)
			tds_put_byte(tds, bufp[i]);
	} else {
		for (i = 0; i < n; i++)
			tds_put_byte(tds, '\0');
	}
	return 0;
}

/**
 * Output a string to wire
 * automatic translate string to unicode if needed
 * @param s   string to write
 * @param len length of string in characters, or -1 for null terminated
 */
int
tds_put_string(TDSSOCKET * tds, const char *s, int len)
{
	TDS_ENCODING *client;
	char buffer[256];
	const char * eob; 
	unsigned int output_size, bytes_out = 0;
	unsigned int bpc = tds->iconv_info.server_charset.max_bytes_per_char; /* bytes per char */ ;

	client = &tds->iconv_info.client_charset;

	if (len < 0) {
		if (client->min_bytes_per_char == 1) {	/* ascii or UTF-8 */
			len = strlen(s);
		} else {
			if (client->min_bytes_per_char == 2 && client->max_bytes_per_char == 2) {	/* UCS-2 or variant */

				TDS_SMALLINT *p = (TDS_SMALLINT *) s;

				for (len = 0; p && p[len]; len++);
				len *= sizeof(TDS_SMALLINT);

			} else {
				assert(client->min_bytes_per_char < 3);	/* FIXME */
			}
		}
	}
	assert(bpc);
	assert(len >= 0);

	if (IS_TDS7_PLUS(tds)) {
		eob = s + len;	/* 1 past the end of the input buffer */
		while (len > 0) {
			tdsdump_log(TDS_DBG_NETWORK, "%L tds_put_string converting %d bytes of \"%s\"\n", len, s);
			output_size = len * bpc;
			if (output_size > sizeof(buffer))
				output_size = sizeof(buffer);
			bytes_out = tds_iconv(to_server, &tds->iconv_info, s, &len, buffer, output_size);
			s = eob - len;
			tds_put_n(tds, buffer, bytes_out);
		}
		tdsdump_log(TDS_DBG_NETWORK, "%L tds_put_string wrote %d bytes\n", bytes_out);
		return bytes_out;
	}
	return tds_put_n(tds, s, len);
}

int
tds_put_buf(TDSSOCKET * tds, const unsigned char *buf, int dsize, int ssize)
{
	int cpsize;

	cpsize = ssize > dsize ? dsize : ssize;
	tds_put_n(tds, buf, cpsize);
	dsize -= cpsize;
	tds_put_n(tds, NULL, dsize);
	return tds_put_byte(tds, cpsize);
}

int
tds_put_int8(TDSSOCKET * tds, TDS_INT8 i)
{
#if WORDS_BIGENDIAN
	TDS_UINT h;

	if (tds->emul_little_endian) {
		h = (TDS_UINT) i;
		tds_put_byte(tds, h & 0x000000FF);
		tds_put_byte(tds, (h & 0x0000FF00) >> 8);
		tds_put_byte(tds, (h & 0x00FF0000) >> 16);
		tds_put_byte(tds, (h & 0xFF000000) >> 24);
		h = (TDS_UINT) (i >> 32);
		tds_put_byte(tds, h & 0x000000FF);
		tds_put_byte(tds, (h & 0x0000FF00) >> 8);
		tds_put_byte(tds, (h & 0x00FF0000) >> 16);
		tds_put_byte(tds, (h & 0xFF000000) >> 24);
		return 0;
	}
#endif
	return tds_put_n(tds, (const unsigned char *) &i, sizeof(TDS_INT8));
}

int
tds_put_int(TDSSOCKET * tds, TDS_INT i)
{
#if WORDS_BIGENDIAN
	if (tds->emul_little_endian) {
		tds_put_byte(tds, i & 0x000000FF);
		tds_put_byte(tds, (i & 0x0000FF00) >> 8);
		tds_put_byte(tds, (i & 0x00FF0000) >> 16);
		tds_put_byte(tds, (i & 0xFF000000) >> 24);
		return 0;
	}
#endif
	return tds_put_n(tds, (const unsigned char *) &i, sizeof(TDS_INT));
}

int
tds_put_smallint(TDSSOCKET * tds, TDS_SMALLINT si)
{
#if WORDS_BIGENDIAN
	if (tds->emul_little_endian) {
		tds_put_byte(tds, si & 0x000000FF);
		tds_put_byte(tds, (si & 0x0000FF00) >> 8);
		return 0;
	}
#endif
	return tds_put_n(tds, (const unsigned char *) &si, sizeof(TDS_SMALLINT));
}

int
tds_put_tinyint(TDSSOCKET * tds, TDS_TINYINT ti)
{
	return tds_put_byte(tds, (unsigned char) ti);
}

int
tds_put_byte(TDSSOCKET * tds, unsigned char c)
{
	if (tds->out_pos >= tds->env->block_size) {
		tds_write_packet(tds, 0x0);
		tds_init_write_buf(tds);
	}
	tds->out_buf[tds->out_pos++] = c;
	return 0;
}

int
tds_put_bulk_data(TDSSOCKET * tds, const unsigned char *buf, TDS_INT bufsize)
{

	tds->out_flag = 0x07;
	return tds_put_n(tds, buf, bufsize);
}

int
tds_init_write_buf(TDSSOCKET * tds)
{
	memset(tds->out_buf, '\0', tds->env->block_size);
	tds->out_pos = 8;
	return 0;
}

/* TODO this code should be similar to read one... */
static int
tds_check_socket_write(TDSSOCKET * tds)
{
	int retcode = 0;
	struct timeval selecttimeout;
	time_t start, now;
	fd_set fds;

	/* Jeffs hack *** START OF NEW CODE */
	FD_ZERO(&fds);

	if (!tds->timeout) {
		for (;;) {
			FD_SET(tds->s, &fds);
			retcode = select(tds->s + 1, NULL, &fds, NULL, NULL);
			/* write available */
			if (retcode >= 0)
				return 0;
			/* interrupted */
			if (sock_errno == EINTR)
				continue;
			/* error, leave caller handle problems */
			return -1;
		}
	}
	start = time(NULL);
	now = start;

	while ((retcode == 0) && ((now - start) < tds->timeout)) {
		FD_SET(tds->s, &fds);
		selecttimeout.tv_sec = tds->timeout - (now - start);
		selecttimeout.tv_usec = 0;
		retcode = select(tds->s + 1, NULL, &fds, NULL, &selecttimeout);
		if (retcode < 0 && sock_errno == EINTR) {
			retcode = 0;
		}

		now = time(NULL);
	}

	return retcode;
	/* Jeffs hack *** END OF NEW CODE */
}

/* goodwrite function adapted from patch by freddy77 */
static int
goodwrite(TDSSOCKET * tds)
{
	int left;
	unsigned char *p;
	int result = TDS_SUCCEED;
	int retval;

	left = tds->out_pos;
	p = tds->out_buf;

	while (left > 0) {
		/* If there's a timeout, we need to sit and wait for socket */
		/* writability */
		/* moved socket writability check to own function -- bsb */
		tds_check_socket_write(tds);

		retval = WRITESOCKET(tds->s, p, left);

		if (retval <= 0) {
			tdsdump_log(TDS_DBG_NETWORK, "TDS: Write failed in tds_write_packet\nError: %d (%s)\n", sock_errno,
				    strerror(sock_errno));
			tds_client_msg(tds->tds_ctx, tds, 20006, 9, 0, 0, "Write to SQL Server failed.");
			tds->in_pos = 0;
			tds->in_len = 0;
			tds_close_socket(tds);
			result = TDS_FAIL;
			break;
		}
		left -= retval;
		p += retval;
	}
	return result;
}

static int
tds_write_packet(TDSSOCKET * tds, unsigned char final)
{
	int retcode;

#ifndef WIN32
	void (*oldsig) (int);
#endif

	tds->out_buf[0] = tds->out_flag;
	tds->out_buf[1] = final;
	tds->out_buf[2] = (tds->out_pos) / 256u;
	tds->out_buf[3] = (tds->out_pos) % 256u;
	if (IS_TDS70(tds) || IS_TDS80(tds)) {
		tds->out_buf[6] = 0x01;
	}

	tdsdump_log(TDS_DBG_NETWORK, "Sending packet @ %L\n%D\n", tds->out_buf, tds->out_pos);

#ifndef WIN32
	oldsig = signal(SIGPIPE, SIG_IGN);
	if (oldsig == SIG_ERR) {
		tdsdump_log(TDS_DBG_WARN, "TDS: Warning: Couldn't set SIGPIPE signal to be ignored\n");
	}
#endif

	retcode = goodwrite(tds);

#ifndef WIN32
	if (signal(SIGPIPE, oldsig) == SIG_ERR) {
		tdsdump_log(TDS_DBG_WARN, "TDS: Warning: Couldn't reset SIGPIPE signal to previous value\n");
	}
#endif

	/* GW added in check for write() returning <0 and SIGPIPE checking */
	return retcode;
}

/**
 * Flush packet to server
 * @return TDS_FAIL or TDS_SUCCEED
 */
int
tds_flush_packet(TDSSOCKET * tds)
{
	int result = TDS_FAIL;

	/* GW added check for tds->s */
	if (!IS_TDSDEAD(tds)) {
		result = tds_write_packet(tds, 0x01);
		tds_init_write_buf(tds);
	}
	return result;
}

/** \@} */
