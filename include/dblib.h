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

#ifndef _dblib_h_
#define _dblib_h_

#ifdef __cplusplus
extern "C"
{
#if 0
}
#endif
#endif

static char rcsid_dblib_h[] = "$Id: dblib.h,v 1.15 2004/01/28 20:37:42 freddy77 Exp $";
static void *no_unused_dblib_h_warn[] = { rcsid_dblib_h, no_unused_dblib_h_warn };

struct tds_dblib_loginrec
{
	TDSLOGIN *tds_login;
};

typedef struct tag_DBPROC_ROWBUF
{
	int buffering_on;	/* (boolean) is row buffering turned on?     */
	int first_in_buf;	/* result set row number of first row in buf */
	int next_row;		/* result set row number of next row         */
	int newest;		/* index of most recent item in queue        */
	int oldest;		/* index of least recent item in queue       */
	int elcount;		/* max element count that buffer can hold    */
	int element_size;	/* size in bytes of each element in queue    */
	int rows_in_buf;	/* # of rows currently in buffer             */
	void *rows;		/* pointer to the row storage                */
} DBPROC_ROWBUF;

typedef struct
{
	int host_column;
	void *host_var;
	int datatype;
	int prefix_len;
	DBINT column_len;
	BYTE *terminator;
	int term_len;
	int tab_colnum;
	int column_error;
} BCP_HOSTCOLINFO;

/* linked list of rpc parameters */

typedef struct _DBREMOTE_PROC_PARAM
{
	struct _DBREMOTE_PROC_PARAM *next;

	char *name;
	BYTE status;
	int type;
	DBINT maxlen;
	DBINT datalen;
	BYTE *value;
} DBREMOTE_PROC_PARAM;

typedef struct
{
	char *hint;
	TDS_CHAR *hostfile;
	TDS_CHAR *errorfile;
	TDS_CHAR *tablename;
	TDS_CHAR *insert_stmt;
	TDS_INT direction;
	TDS_INT db_colcount;
	TDS_INT host_colcount;
	BCP_COLINFO **db_columns;
	BCP_HOSTCOLINFO **host_columns;
	TDS_INT firstrow;
	TDS_INT lastrow;
	TDS_INT maxerrs;
	TDS_INT batch;
} DBBULKCOPY;

typedef struct _DBREMOTE_PROC
{
	struct _DBREMOTE_PROC *next;

	char *name;
	DBSMALLINT options;
	DBREMOTE_PROC_PARAM *param_list;
} DBREMOTE_PROC;

struct tds_dblib_dbprocess
{
	TDSSOCKET *tds_socket;

	DBPROC_ROWBUF row_buf;

	int noautofree;
	int more_results;	/* boolean.  Are we expecting results? */
	int dbresults_state;
	int dbresults_retcode;
	BYTE *user_data;	/* see dbsetuserdata() and dbgetuserdata() */
	unsigned char *dbbuf;	/* is dynamic!                   */
	int dbbufsz;
	int command_state;
	TDS_INT text_size;
	TDS_INT text_sent;
	FILE *bcp_errfileptr;
	TDS_INT sendrow_init;
	TDS_INT var_cols;
	DBTYPEINFO typeinfo;
	unsigned char avail_flag;
	DBOPTION *dbopts;
	DBSTRING *dboptcmd;
	DBBULKCOPY bcp;		/* see TODO, above */
	DBREMOTE_PROC *rpc;
	DBUSMALLINT envchange_rcv;
	char dbcurdb[DBMAXNAME + 1];
	char servcharset[DBMAXNAME + 1];
	FILE *ftos;
	DB_DBCHKINTR_FUNC dbchkintr;
	DB_DBHNDLINTR_FUNC dbhndlintr;
};

#define DBLIB_INFO_MSG_TYPE 0
#define DBLIB_ERROR_MSG_TYPE 1

/*
** internal prototypes
*/
int dblib_handle_info_message(TDSCONTEXT * ctxptr, TDSSOCKET * tdsptr, TDSMESSAGE* msgptr);
int dblib_handle_err_message(TDSCONTEXT * ctxptr, TDSSOCKET * tdsptr, TDSMESSAGE* msgptr);
int _dblib_client_msg(DBPROCESS * dbproc, int dberr, int severity, const char *dberrstr);
void dblib_setTDS_version(TDSLOGIN * tds_login, DBINT version);

DBINT _convert_char(int srctype, BYTE * src, int destype, BYTE * dest, DBINT destlen);
DBINT _convert_intn(int srctype, BYTE * src, int destype, BYTE * dest, DBINT destlen);

RETCODE _bcp_clear_storage(DBPROCESS * dbproc);
RETCODE _bcp_get_term_var(BYTE * dataptr, BYTE * term, int term_len);
RETCODE _bcp_get_prog_data(DBPROCESS * dbproc);
int _bcp_readfmt_colinfo(DBPROCESS * dbproc, char *buf, BCP_HOSTCOLINFO * ci);
RETCODE _bcp_read_hostfile(DBPROCESS * dbproc, FILE * hostfile, FILE * errfile, int *row_error);

extern MHANDLEFUNC _dblib_msg_handler;
extern EHANDLEFUNC _dblib_err_handler;

#ifdef __cplusplus
#if 0
{
#endif
}
#endif

#endif
