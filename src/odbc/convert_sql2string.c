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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <assert.h>
#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include "tds.h"
#include "tdsconvert.h"
#include "convert_sql2string.h"
#include <sqlext.h>

static char software_version[] = "$Id: convert_sql2string.c,v 1.18 2002/11/29 22:11:04 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

/**
 * Pass this an SQL_* type and get a SYB* type which most closely corresponds
 * to the SQL_* type.
 */
int
_odbc_get_server_type(int c_type)
{
	switch (c_type) {
	case SQL_C_BINARY:
		return SYBBINARY;
	case SQL_C_CHAR:
		return SYBCHAR;
	case SQL_C_FLOAT:
		return SYBREAL;
	case SQL_C_DOUBLE:
		return SYBFLT8;
	case SQL_NUMERIC:
		return SYBNUMERIC;
	case SQL_C_BIT:
		return SYBBIT;
#if (ODBCVER >= 0x0300)
	case SQL_BIGINT:
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
		return SYBINT8;
	case SQL_C_GUID:
		return SYBUNIQUE;
#endif
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
		return SYBINT4;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
		return SYBINT2;
	case SQL_C_TINYINT:
	case SQL_C_STINYINT:
	case SQL_C_UTINYINT:
		return SYBINT1;
	/* ODBC date formats are completely differect from SQL one */
	case SQL_C_DATE:
	case SQL_C_TIME:
	case SQL_C_TIMESTAMP:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_INTERVAL_YEAR:
	case SQL_C_INTERVAL_MONTH:
	case SQL_C_INTERVAL_DAY:
	case SQL_C_INTERVAL_HOUR:
	case SQL_C_INTERVAL_MINUTE:
	case SQL_C_INTERVAL_SECOND:
	case SQL_C_INTERVAL_YEAR_TO_MONTH:
	case SQL_C_INTERVAL_DAY_TO_HOUR:
	case SQL_C_INTERVAL_DAY_TO_MINUTE:
	case SQL_C_INTERVAL_DAY_TO_SECOND:
	case SQL_C_INTERVAL_HOUR_TO_MINUTE:
	case SQL_C_INTERVAL_HOUR_TO_SECOND:
	case SQL_C_INTERVAL_MINUTE_TO_SECOND:
		break;
	}
	return TDS_FAIL;
}


static TDS_INT
convert_datetime2string(TDSCONTEXT * context, int srctype, const TDS_CHAR * src, TDS_CHAR * dest, TDS_INT destlen)
{
	struct tm src_tm;
	char dfmt[30];
	char tmpbuf[256];
	int ret;

	const DATE_STRUCT *src_date = (const DATE_STRUCT *) src;
	const TIME_STRUCT *src_time = (const TIME_STRUCT *) src;
	const TIMESTAMP_STRUCT *src_timestamp = (const TIMESTAMP_STRUCT *) src;

	memset(&src_tm, 0, sizeof(src_tm));

	/* FIX ME -- This fails for dates before 1902 or after 2038 */
	switch (srctype) {
	case SQL_C_DATE:
	case SQL_C_TYPE_DATE:
		src_tm.tm_year = src_date->year - 1900;
		src_tm.tm_mon = src_date->month - 1;
		src_tm.tm_mday = src_date->day;
		strcpy(&dfmt[0], "%Y-%m-%d");
		break;
	case SQL_C_TIME:
	case SQL_C_TYPE_TIME:
		src_tm.tm_hour = src_time->hour;
		src_tm.tm_min = src_time->minute;
		src_tm.tm_sec = src_time->second;
		strcpy(&dfmt[0], "%I:%M:%S");
		break;
	case SQL_C_TIMESTAMP:
	case SQL_C_TYPE_TIMESTAMP:
		src_tm.tm_year = src_timestamp->year - 1900;
		src_tm.tm_mon = src_timestamp->month - 1;
		src_tm.tm_mday = src_timestamp->day;
		src_tm.tm_hour = src_timestamp->hour;
		src_tm.tm_min = src_timestamp->minute;
		src_tm.tm_sec = src_timestamp->second;
		strcpy(&dfmt[0], "%Y-%m-%d %I:%M:%S");
		break;
	default:
		return TDS_FAIL;
	}

	ret = strftime(tmpbuf, sizeof(tmpbuf), dfmt, &src_tm);
	if (!ret) {
		dest[0] = '\0';	/* set empty string and return */
		return 0;
	}
	memcpy(dest, tmpbuf, ret);
	dest[ret] = '\0';
	return ret;
}


static TDS_INT
convert_text2string(const TDS_CHAR * src, TDS_INT srclen, TDS_CHAR * dest, TDS_INT destlen)
{
	if (srclen < 0 || !src[srclen])
		srclen = strlen(src);

	if (destlen >= 0 && destlen < srclen)
		return TDS_FAIL;

	memcpy(dest, src, srclen);

	return srclen;
}


TDS_INT
convert_sql2string(TDSCONTEXT * context, int srctype, const TDS_CHAR * src, TDS_INT srclen,
		   TDS_CHAR * dest, TDS_INT destlen, int param_lenbind)
{
int res;
CONV_RESULT ores;

static const char *str_null = "null";

	if (SQL_NULL_DATA == param_lenbind) {
		src = str_null;
		srclen = strlen(str_null);
		srctype = SQL_C_CHAR;
	}

	switch (srctype) {
	case SQL_C_DATE:
	case SQL_C_TIME:
	case SQL_C_TIMESTAMP:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
		return convert_datetime2string(context, srctype, src, dest, destlen);
		break;
	case SQL_C_CHAR:
		return convert_text2string(src, srclen, dest, destlen);
		break;
/*		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
		default:
*/
	}

	res = tds_convert(context, _odbc_get_server_type(srctype), src, srclen, SYBVARCHAR, &ores);

	if (res < 0) {
		fprintf(stderr, "convert_sql2string(): Attempting to convert unknown "
			"source type %d (size %d) into string\n", srctype, srclen);
	} else {
		memcpy(dest, ores.c, res);
		dest[res] = 0;
		res = res;
		free(ores.c);
	}

	return res;
}
