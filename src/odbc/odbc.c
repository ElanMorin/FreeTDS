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

/***************************************************************
 * PROGRAMMER   NAME            CONTACT
 *==============================================================
 * BSB          Brian Bruns     camber@ais.org
 * PAH          Peter Harvey    pharvey@codebydesign.com
 *
 ***************************************************************
 * DATE         PROGRAMMER  CHANGE
 *==============================================================
 *                          Original.
 * 03.FEB.02    PAH         Started adding use of SQLGetPrivateProfileString().
 * 04.FEB.02	PAH         Fixed small error preventing SQLBindParameter from being called
 *
 ***************************************************************/

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include "tds.h"
#include "tdsodbc.h"
#include "tdsstring.h"
#include "tdsconvert.h"

#include "connectparams.h"
#include "odbc_util.h"
#include "convert_tds2sql.h"
#include "convert_sql2string.h"
#include "sql2tds.h"
#include "prepare_query.h"
#include "replacements.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char software_version[] = "$Id: odbc.c,v 1.200 2003/07/30 05:57:10 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static SQLRETURN SQL_API _SQLAllocConnect(SQLHENV henv, SQLHDBC FAR * phdbc);
static SQLRETURN SQL_API _SQLAllocEnv(SQLHENV FAR * phenv);
static SQLRETURN SQL_API _SQLAllocStmt(SQLHDBC hdbc, SQLHSTMT FAR * phstmt);
static SQLRETURN SQL_API _SQLFreeConnect(SQLHDBC hdbc);
static SQLRETURN SQL_API _SQLFreeEnv(SQLHENV henv);
static SQLRETURN SQL_API _SQLFreeStmt(SQLHSTMT hstmt, SQLUSMALLINT fOption);
static int mymessagehandler(TDSCONTEXT * ctx, TDSSOCKET * tds, TDSMSGINFO * msg);
static int myerrorhandler(TDSCONTEXT * ctx, TDSSOCKET * tds, TDSMSGINFO * msg);
static void log_unimplemented_type(const char function_name[], int fType);
static SQLRETURN SQL_API _SQLExecute(TDS_STMT * stmt);
static SQLRETURN SQL_API _SQLGetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER BufferLength,
					    SQLINTEGER * StringLength);
static SQLRETURN SQL_API _SQLSetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER ValuePtr, SQLINTEGER StringLength);
SQLRETURN _SQLRowCount(SQLHSTMT hstmt, SQLINTEGER FAR * pcrow);
static void odbc_upper_column_names(TDS_STMT * stmt);
static int odbc_col_setname(TDS_STMT * stmt, int colpos, char *name);
static SQLRETURN odbc_stat_execute(TDS_STMT * stmt, const char *begin, int nparams, ...);
static SQLRETURN odbc_free_dynamic(TDS_STMT * stmt);


/**
 * \defgroup odbc_api ODBC API
 * Functions callable by \c ODBC client programs
 */


/* utils to check handles */
#define CHECK_HDBC  if ( SQL_NULL_HDBC  == hdbc || !IS_HDBC(hdbc) ) return SQL_INVALID_HANDLE;
#define CHECK_HSTMT if ( SQL_NULL_HSTMT == hstmt || !IS_HSTMT(hstmt) ) return SQL_INVALID_HANDLE;
#define CHECK_HENV  if ( SQL_NULL_HENV  == henv || !IS_HENV(henv) ) return SQL_INVALID_HANDLE;
#define CHECK_HDESC if ( SQL_NULL_HDESC == hdesc || !IS_HDESC(hdesc) ) return SQL_INVALID_HANDLE;

#define INIT_HSTMT \
	TDS_STMT *stmt = (TDS_STMT*)hstmt; \
	CHECK_HSTMT; \
	odbc_errs_reset(&stmt->errs); \

#define INIT_HDBC \
	TDS_DBC *dbc = (TDS_DBC*)hdbc; \
	CHECK_HDBC; \
	odbc_errs_reset(&dbc->errs); \

#define INIT_HENV \
	TDS_ENV *env = (TDS_ENV*)henv; \
	CHECK_HENV; \
	odbc_errs_reset(&env->errs); \

#define INIT_HDESC \
	TDS_DESC *desc = (TDS_DESC*)hdesc; \
	CHECK_HDESC; \
	odbc_errs_reset(&desc->errs); \

#define IS_VALID_LEN(len) ((len) >= 0 || (len) == SQL_NTS || (len) == SQL_NULL_DATA)

/*
**
** Note: I *HATE* hungarian notation, it has to be the most idiotic thing
** I've ever seen. So, you will note it is avoided other than in the function
** declarations. "Gee, let's make our code totally hard to read and they'll
** beg for GUI tools"
** Bah!
*/

static int
odbc_col_setname(TDS_STMT * stmt, int colpos, char *name)
{
	TDSRESULTINFO *resinfo;
	int retcode = -1;

	if (colpos > 0 && stmt->hdbc->tds_socket != NULL && (resinfo = stmt->hdbc->tds_socket->res_info) != NULL) {
		if (colpos <= resinfo->num_cols) {
			/* TODO set column_namelen, see overflow */
			strcpy(resinfo->columns[colpos - 1]->column_name, name);
			retcode = 0;
		}
	}
	return retcode;
}

/* spinellia@acm.org : copied shamelessly from change_database */
static SQLRETURN
change_autocommit(TDS_DBC * dbc, int state)
{
	TDSSOCKET *tds = dbc->tds_socket;
	char query[80];

	/*
	 * We may not be connected yet and dbc->tds_socket
	 * may not initialized.
	 */
	if (tds) {
		/* mssql: SET IMPLICIT_TRANSACTION ON
		 * sybase: SET CHAINED ON */

		/* implicit transactions are on if autocommit is off :-| */
		if (TDS_IS_MSSQL(tds))
			sprintf(query, "set implicit_transactions %s", (state == SQL_AUTOCOMMIT_ON) ? "off" : "on");
		else
			sprintf(query, "set chained %s", (state == SQL_AUTOCOMMIT_ON) ? "off" : "on");

		tdsdump_log(TDS_DBG_INFO1, "change_autocommit: executing %s\n", query);

		if (tds_submit_query(tds, query, NULL) != TDS_SUCCEED) {
			odbc_errs_add(&dbc->errs, 0, "HY000", "Could not change transaction status", NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		if (tds_process_simple_query(tds) != TDS_SUCCEED) {
			odbc_errs_add(&dbc->errs, 0, "HY000", "Could not change transaction status", NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		dbc->attr.attr_autocommit = state;
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}

static SQLRETURN
change_database(TDS_DBC * dbc, char *database, int database_len)
{
	TDSSOCKET *tds = dbc->tds_socket;

	/* 
	 * We may not be connected yet and dbc->tds_socket
	 * may not initialized.
	 */
	if (tds) {
		/* build query */
		/* FIXME quote id, not quote string */
		char *query = (char *) malloc(6 + tds_quote_string(tds, NULL, database, database_len));

		if (!query) {
			odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		strcpy(query, "USE ");
		tds_quote_string(tds, query + 4, database, database_len);

		tdsdump_log(TDS_DBG_INFO1, "change_database: executing %s\n", query);

		if (tds_submit_query(tds, query, NULL) != TDS_SUCCEED) {
			free(query);
			odbc_errs_add(&dbc->errs, 0, "HY000", "Could not change database", NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		free(query);
		if (tds_process_simple_query(tds) != TDS_SUCCEED) {
			odbc_errs_add(&dbc->errs, 0, "HY000", "Could not change database", NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}

static void
odbc_env_change(TDSSOCKET * tds, int type, char *oldval, char *newval)
{
	TDS_DBC *dbc;

	if (tds == NULL) {
		return;
	}
	dbc = (TDS_DBC *) tds->parent;
	if (!dbc)
		return;

	switch (type) {
	case TDS_ENV_DATABASE:
		tds_dstr_copy(&dbc->attr.attr_current_catalog, newval);
		break;
	case TDS_ENV_PACKSIZE:
		dbc->attr.attr_packet_size = atoi(newval);
		break;
	}
}

static SQLRETURN
do_connect(TDS_DBC * dbc, TDSCONNECTINFO * connect_info)
{
	TDS_ENV *env = dbc->henv;

	dbc->tds_socket = tds_alloc_socket(env->tds_ctx, 512);
	if (!dbc->tds_socket) {
		odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
	tds_set_parent(dbc->tds_socket, (void *) dbc);

	/* Set up our environment change hook */
	dbc->tds_socket->env_chg_func = odbc_env_change;

	tds_fix_connect(connect_info);

	/* fix login type */
	if (!connect_info->try_domain_login) {
		if (strchr(tds_dstr_cstr(&connect_info->user_name), '\\')) {
			connect_info->try_domain_login = 1;
			connect_info->try_server_login = 0;
		}
	}
	if (!connect_info->try_domain_login && !connect_info->try_server_login)
		connect_info->try_server_login = 1;

	if (tds_connect(dbc->tds_socket, connect_info) == TDS_FAIL) {
		tds_free_socket(dbc->tds_socket);
		dbc->tds_socket = NULL;
		odbc_errs_add(&dbc->errs, 0, "08001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLDriverConnect(SQLHDBC hdbc, SQLHWND hwnd, SQLCHAR FAR * szConnStrIn, SQLSMALLINT cbConnStrIn, SQLCHAR FAR * szConnStrOut,
		 SQLSMALLINT cbConnStrOutMax, SQLSMALLINT FAR * pcbConnStrOut, SQLUSMALLINT fDriverCompletion)
{
	SQLRETURN ret;
	TDSCONNECTINFO *connect_info;

	INIT_HDBC;

	connect_info = tds_alloc_connect(dbc->henv->tds_ctx->locale);
	if (!connect_info) {
		odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	/* FIXME szConnStrIn can be no-null terminated */
	tdoParseConnectString((char *) szConnStrIn, connect_info);

	if (tds_dstr_isempty(&connect_info->server_name)) {
		tds_free_connect(connect_info);
		odbc_errs_add(&dbc->errs, 0, "IM007", "Could not find Servername or server parameter", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	if (tds_dstr_isempty(&connect_info->user_name)) {
		tds_free_connect(connect_info);
		odbc_errs_add(&dbc->errs, 0, "IM007", "Could not find UID parameter", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	if ((ret = do_connect(dbc, connect_info)) != SQL_SUCCESS) {
		tds_free_connect(connect_info);
		ODBC_RETURN(dbc, ret);
	}

	/* use the default database */
	tds_free_connect(connect_info);
	if (dbc->errs.num_errors != 0)
		ODBC_RETURN(dbc, SQL_SUCCESS_WITH_INFO);
	ODBC_RETURN(dbc, SQL_SUCCESS);
}

#if 0
SQLRETURN SQL_API
SQLBrowseConnect(SQLHDBC hdbc, SQLCHAR FAR * szConnStrIn, SQLSMALLINT cbConnStrIn, SQLCHAR FAR * szConnStrOut,
		 SQLSMALLINT cbConnStrOutMax, SQLSMALLINT FAR * pcbConnStrOut)
{
	INIT_HDBC;
	odbc_errs_add(&dbc->errs, 0, "HYC00", "SQLBrowseConnect: function not implemented", NULL);
	ODBC_RETURN(dbc, SQL_ERROR);
}
#endif

SQLRETURN SQL_API
SQLColumnPrivileges(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
		    SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName, SQLCHAR FAR * szColumnName,
		    SQLSMALLINT cbColumnName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_column_privileges ", 4,
				  "@table_qualifier", szCatalogName, cbCatalogName,
				  "@table_owner", szSchemaName, cbSchemaName,
				  "@table_name", szTableName, cbTableName, "@column_name", szColumnName, cbColumnName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

#if 0
SQLRETURN SQL_API
SQLDescribeParam(SQLHSTMT hstmt, SQLUSMALLINT ipar, SQLSMALLINT FAR * pfSqlType, SQLUINTEGER FAR * pcbParamDef,
		 SQLSMALLINT FAR * pibScale, SQLSMALLINT FAR * pfNullable)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLDescribeParam: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}

SQLRETURN SQL_API
SQLExtendedFetch(SQLHSTMT hstmt, SQLUSMALLINT fFetchType, SQLINTEGER irow, SQLUINTEGER FAR * pcrow, SQLUSMALLINT FAR * rgfRowStatus)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLExtendedFetch: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}
#endif

SQLRETURN SQL_API
SQLForeignKeys(SQLHSTMT hstmt, SQLCHAR FAR * szPkCatalogName, SQLSMALLINT cbPkCatalogName, SQLCHAR FAR * szPkSchemaName,
	       SQLSMALLINT cbPkSchemaName, SQLCHAR FAR * szPkTableName, SQLSMALLINT cbPkTableName, SQLCHAR FAR * szFkCatalogName,
	       SQLSMALLINT cbFkCatalogName, SQLCHAR FAR * szFkSchemaName, SQLSMALLINT cbFkSchemaName, SQLCHAR FAR * szFkTableName,
	       SQLSMALLINT cbFkTableName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_fkeys ", 6,
				  "@pktable_qualifier", szPkCatalogName, cbPkCatalogName,
				  "@pktable_owner", szPkSchemaName, cbPkSchemaName,
				  "@pktable_name", szPkTableName, cbPkTableName,
				  "@fktable_qualifier", szFkCatalogName, cbFkCatalogName,
				  "@fktable_owner", szFkSchemaName, cbFkSchemaName, "@fktable_name", szFkTableName, cbFkTableName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "PKTABLE_CAT");
		odbc_col_setname(stmt, 2, "PKTABLE_SCHEM");
		odbc_col_setname(stmt, 5, "FKTABLE_CAT");
		odbc_col_setname(stmt, 6, "FKTABLE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

SQLRETURN SQL_API
SQLMoreResults(SQLHSTMT hstmt)
{
	TDSSOCKET *tds;
	TDS_INT result_type;
	int tdsret;
	TDS_INT rowtype;
	int in_row = 0;
	int done_flags;

	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;

	/* try to go to the next recordset */
	for (;;) {
		switch (tds_process_result_tokens(tds, &result_type, &done_flags)) {
		case TDS_NO_MORE_RESULTS:
			if (stmt->hdbc->current_statement == stmt)
				stmt->hdbc->current_statement = NULL;
			ODBC_RETURN(stmt, SQL_NO_DATA_FOUND);
		case TDS_SUCCEED:
			switch (result_type) {
			case TDS_COMPUTE_RESULT:
			case TDS_ROW_RESULT:
				if (in_row)
					ODBC_RETURN(stmt, SQL_SUCCESS);
				/* Skipping current result set's rows to access next resultset or proc's retval */
				while ((tdsret = tds_process_row_tokens(tds, &rowtype, NULL)) == TDS_SUCCEED);
				if (tdsret == TDS_FAIL)
					ODBC_RETURN(stmt, SQL_ERROR);
				break;
			case TDS_STATUS_RESULT:
				odbc_set_return_status(stmt);
				break;
			case TDS_PARAM_RESULT:
				odbc_set_return_params(stmt);
				break;

			case TDS_DONE_RESULT:
			case TDS_DONEPROC_RESULT:
				if (!(done_flags & TDS_DONE_COUNT) && !(done_flags & TDS_DONE_ERROR))
					break;
				/* FIXME this row is used only as a flag for update binding, should be cleared if binding/result changed */
				stmt->row = 0;
				/* FIXME here ??? */
				if (!in_row)
					tds_free_all_results(tds);
				ODBC_RETURN(stmt, SQL_SUCCESS);
				break;

				/* TODO test flags ? check error and change result ? */
			case TDS_DONEINPROC_RESULT:
				if (in_row)
					ODBC_RETURN(stmt, SQL_SUCCESS);
				break;

				/* do not stop at metadata, an error can follow... */
			case TDS_COMPUTEFMT_RESULT:
			case TDS_ROWFMT_RESULT:
				if (in_row)
					ODBC_RETURN(stmt, SQL_SUCCESS);
				tds->rows_affected = TDS_NO_COUNT;
				stmt->row = 0;
				in_row = 1;
				break;
			case TDS_MSG_RESULT:
			case TDS_DESCRIBE_RESULT:
				break;
			}
		}
	}
	ODBC_RETURN(stmt, SQL_ERROR);
}

SQLRETURN SQL_API
SQLNativeSql(SQLHDBC hdbc, SQLCHAR FAR * szSqlStrIn, SQLINTEGER cbSqlStrIn, SQLCHAR FAR * szSqlStr, SQLINTEGER cbSqlStrMax,
	     SQLINTEGER FAR * pcbSqlStr)
{
	SQLRETURN ret = SQL_SUCCESS;
	DSTR query;

	INIT_HDBC;

	tds_dstr_init(&query);

#ifdef TDS_NO_DM
	if (!szSqlStrIn || !IS_VALID_LEN(cbSqlStrIn)) {
		odbc_errs_add(&dbc->errs, 0, "HY009", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
#endif

	if (!tds_dstr_copyn(&query, szSqlStrIn, odbc_get_string_size(cbSqlStrIn, szSqlStrIn))) {
		odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	/* TODO support not null terminated */
	native_sql(tds_dstr_cstr(&query));

	ret = odbc_set_string_i(szSqlStr, cbSqlStrMax, pcbSqlStr, tds_dstr_cstr(&query), -1);

	tds_dstr_free(&query);

	ODBC_RETURN(dbc, ret);
}

SQLRETURN SQL_API
SQLNumParams(SQLHSTMT hstmt, SQLSMALLINT FAR * pcpar)
{
	INIT_HSTMT;
	*pcpar = stmt->param_count;
	ODBC_RETURN(stmt, SQL_SUCCESS);
}

#if 0
SQLRETURN SQL_API
SQLParamOptions(SQLHSTMT hstmt, SQLUINTEGER crow, SQLUINTEGER FAR * pirow)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLParamOptions: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}
#endif

SQLRETURN SQL_API
SQLPrimaryKeys(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
	       SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_pkeys ", 3,
				  "@table_qualifier", szCatalogName, cbCatalogName,
				  "@table_owner", szSchemaName, cbSchemaName, "@table_name", szTableName, cbTableName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

SQLRETURN SQL_API
SQLProcedureColumns(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
		    SQLSMALLINT cbSchemaName, SQLCHAR FAR * szProcName, SQLSMALLINT cbProcName, SQLCHAR FAR * szColumnName,
		    SQLSMALLINT cbColumnName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_sproc_columns ", 4,
				  "@procedure_qualifier", szCatalogName, cbCatalogName,
				  "@procedure_owner", szSchemaName, cbSchemaName,
				  "@procedure_name", szProcName, cbProcName, "@column_name", szColumnName, cbColumnName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "PROCEDURE_CAT");
		odbc_col_setname(stmt, 2, "PROCEDURE_SCHEM");
		odbc_col_setname(stmt, 8, "COLUMN_SIZE");
		odbc_col_setname(stmt, 9, "BUFFER_LENGTH");
		odbc_col_setname(stmt, 10, "DECIMAL_DIGITS");
		odbc_col_setname(stmt, 11, "NUM_PREC_RADIX");
	}
	ODBC_RETURN(stmt, retcode);
}

SQLRETURN SQL_API
SQLProcedures(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
	      SQLSMALLINT cbSchemaName, SQLCHAR FAR * szProcName, SQLSMALLINT cbProcName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_stored_procedures ", 3,
				  "@sp_name", szProcName, cbProcName,
				  "@sp_owner", szSchemaName, cbSchemaName, "@sp_qualifier", szCatalogName, cbCatalogName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "PROCEDURE_CAT");
		odbc_col_setname(stmt, 2, "PROCEDURE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

#if 0
SQLRETURN SQL_API
SQLSetPos(SQLHSTMT hstmt, SQLUSMALLINT irow, SQLUSMALLINT fOption, SQLUSMALLINT fLock)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLSetPos: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}
#endif

SQLRETURN SQL_API
SQLTablePrivileges(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
		   SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_table_privileges ", 3,
				  "@table_qualifier", szCatalogName, cbCatalogName,
				  "@table_owner", szSchemaName, cbSchemaName, "@table_name", szTableName, cbTableName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

#if (ODBCVER >= 0x0300)
#ifndef SQLULEN
/* unixodbc began defining SQLULEN in recent versions; this lets us complile if you're using an older version. */
# define SQLULEN SQLUINTEGER
#endif
SQLRETURN SQL_API
SQLSetEnvAttr(SQLHENV henv, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER StringLength)
{
	INIT_HENV;

	switch (Attribute) {
	case SQL_ATTR_CONNECTION_POOLING:
	case SQL_ATTR_CP_MATCH:
		odbc_errs_add(&env->errs, 0, "HYC00", NULL, NULL);
		ODBC_RETURN(env, SQL_ERROR);
		break;
	case SQL_ATTR_ODBC_VERSION:
		switch ((SQLULEN) Value) {
		case SQL_OV_ODBC3:
		case SQL_OV_ODBC2:
			break;
		default:
			odbc_errs_add(&env->errs, 0, "HY024", NULL, NULL);
			ODBC_RETURN(env, SQL_ERROR);
		}
		env->attr.attr_odbc_version = (SQLINTEGER) Value;
		ODBC_RETURN(env, SQL_SUCCESS);
		break;
	case SQL_ATTR_OUTPUT_NTS:
		env->attr.attr_output_nts = (SQLINTEGER) Value;
		/* TODO - Make this really work */
		env->attr.attr_output_nts = SQL_TRUE;
		ODBC_RETURN(env, SQL_SUCCESS);
		break;
	}
	odbc_errs_add(&env->errs, 0, "HY092", NULL, NULL);
	ODBC_RETURN(env, SQL_ERROR);
}

SQLRETURN SQL_API
SQLGetEnvAttr(SQLHENV henv, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER BufferLength, SQLINTEGER * StringLength)
{
	size_t size;
	void *src;

	INIT_HENV;

	switch (Attribute) {
	case SQL_ATTR_CONNECTION_POOLING:
		src = &env->attr.attr_connection_pooling;
		size = sizeof(env->attr.attr_connection_pooling);
		break;
	case SQL_ATTR_CP_MATCH:
		src = &env->attr.attr_cp_match;
		size = sizeof(env->attr.attr_cp_match);
		break;
	case SQL_ATTR_ODBC_VERSION:
		src = &env->attr.attr_odbc_version;
		size = sizeof(env->attr.attr_odbc_version);
		break;
	case SQL_ATTR_OUTPUT_NTS:
		/* TODO handle output_nts flags */
		env->attr.attr_output_nts = SQL_TRUE;
		src = &env->attr.attr_output_nts;
		size = sizeof(env->attr.attr_output_nts);
		break;
	default:
		odbc_errs_add(&env->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(env, SQL_ERROR);
		break;
	}

	if (StringLength) {
		*StringLength = size;
	}
	memcpy(Value, src, size);

	ODBC_RETURN(env, SQL_SUCCESS);
}

#endif

SQLRETURN SQL_API
SQLBindParameter(SQLHSTMT hstmt, SQLUSMALLINT ipar, SQLSMALLINT fParamType, SQLSMALLINT fCType, SQLSMALLINT fSqlType,
		 SQLUINTEGER cbColDef, SQLSMALLINT ibScale, SQLPOINTER rgbValue, SQLINTEGER cbValueMax, SQLINTEGER FAR * pcbValue)
{
	struct _sql_param_info *cur, *newitem;

	INIT_HSTMT;

	if (ipar == 0) {
		odbc_errs_add(&stmt->errs, 0, "07009", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* find available item in list */
	cur = odbc_find_param(stmt, ipar);

	if (!cur) {
		/* didn't find it create a new one */
		newitem = (struct _sql_param_info *)
			malloc(sizeof(struct _sql_param_info));
		if (!newitem) {
			odbc_errs_add(&stmt->errs, 0, "HY001", NULL, NULL);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
		memset(newitem, 0, sizeof(struct _sql_param_info));
		newitem->param_number = ipar;
		cur = newitem;
		cur->next = stmt->param_head;
		stmt->param_head = cur;
	}

	cur->param_type = fParamType;
	cur->param_bindtype = fCType;
	if (fCType == SQL_C_DEFAULT) {
		cur->param_bindtype = odbc_sql_to_c_type_default(fSqlType);
		if (cur->param_bindtype == 0) {
			odbc_errs_add(&stmt->errs, 0, "HY004", NULL, NULL);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
	} else {
		cur->param_bindtype = fCType;
	}
	cur->param_sqltype = fSqlType;
	if (cur->param_bindtype == SQL_C_CHAR)
		cur->param_bindlen = cbValueMax;
	if (!pcbValue) {
		cur->param_inlen = 0;
		cur->param_lenbind = &cur->param_inlen;
		/* TODO add XML if defined */
		if (cur->param_bindtype == SQL_C_CHAR || cur->param_bindtype == SQL_C_BINARY) {
			cur->param_inlen = SQL_NTS;
		} else {
			int size = tds_get_size_by_type(odbc_get_server_type(cur->param_bindtype));

			if (size > 0)
				cur->param_inlen = size;
		}
	} else {
		cur->param_lenbind = pcbValue;
	}
	cur->varaddr = (char *) rgbValue;

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

/* compatibility with X/Open */
SQLRETURN SQL_API
SQLBindParam(SQLHSTMT hstmt, SQLUSMALLINT ipar, SQLSMALLINT fCType, SQLSMALLINT fSqlType, SQLUINTEGER cbColDef, SQLSMALLINT ibScale,
	     SQLPOINTER rgbValue, SQLINTEGER FAR * pcbValue)
{
	return SQLBindParameter(hstmt, ipar, SQL_PARAM_INPUT, fCType, fSqlType, cbColDef, ibScale, rgbValue, 0, pcbValue);
}

#if (ODBCVER >= 0x0300)
SQLRETURN SQL_API
SQLAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle, SQLHANDLE * OutputHandle)
{
	switch (HandleType) {
	case SQL_HANDLE_STMT:
		return _SQLAllocStmt(InputHandle, OutputHandle);
		break;
	case SQL_HANDLE_DBC:
		return _SQLAllocConnect(InputHandle, OutputHandle);
		break;
	case SQL_HANDLE_ENV:
		return _SQLAllocEnv(OutputHandle);
		break;
	}
	return SQL_ERROR;
}
#endif

static SQLRETURN SQL_API
_SQLAllocConnect(SQLHENV henv, SQLHDBC FAR * phdbc)
{
	TDS_DBC *dbc;

	INIT_HENV;

	dbc = (TDS_DBC *) malloc(sizeof(TDS_DBC));
	if (!dbc) {
		odbc_errs_add(&env->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(env, SQL_ERROR);
	}

	memset(dbc, '\0', sizeof(TDS_DBC));

	dbc->htype = SQL_HANDLE_DBC;
	dbc->henv = env;
	tds_dstr_init(&dbc->server);
	tds_dstr_init(&dbc->dsn);

	dbc->attr.attr_access_mode = SQL_MODE_READ_WRITE;
	dbc->attr.attr_async_enable = SQL_ASYNC_ENABLE_OFF;
	dbc->attr.attr_auto_ipd = SQL_FALSE;
	/* spinellia@acm.org
	 * after login is enabled autocommit */
	dbc->attr.attr_autocommit = SQL_AUTOCOMMIT_ON;
	dbc->attr.attr_connection_dead = SQL_CD_TRUE;	/* No connection yet */
	dbc->attr.attr_connection_timeout = 0;	/* TODO */
	/* This is set in the environment change function */
	tds_dstr_init(&dbc->attr.attr_current_catalog);
	dbc->attr.attr_login_timeout = 0;	/* TODO */
	dbc->attr.attr_metadata_id = SQL_FALSE;
	dbc->attr.attr_odbc_cursors = SQL_CUR_USE_IF_NEEDED;
	dbc->attr.attr_packet_size = 0;
	dbc->attr.attr_quite_mode = NULL;	/* We don't support GUI dialogs yet */
#ifdef TDS_NO_DM
	dbc->attr.attr_trace = SQL_OPT_TRACE_OFF;
	tds_dstr_init(&dbc->attr.attr_tracefile);
#endif
	tds_dstr_init(&dbc->attr.attr_translate_lib);
	dbc->attr.attr_translate_option = 0;
	dbc->attr.attr_txn_isolation = SQL_TXN_READ_COMMITTED;

	*phdbc = (SQLHDBC) dbc;

	ODBC_RETURN(env, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLAllocConnect(SQLHENV henv, SQLHDBC FAR * phdbc)
{
	odbc_errs_reset(&((TDS_ENV *) henv)->errs);
	return _SQLAllocConnect(henv, phdbc);
}

static SQLRETURN SQL_API
_SQLAllocEnv(SQLHENV FAR * phenv)
{
	TDS_ENV *env;
	TDSCONTEXT *ctx;

	env = (TDS_ENV *) malloc(sizeof(TDS_ENV));
	if (!env)
		return SQL_ERROR;

	memset(env, '\0', sizeof(TDS_ENV));

	env->htype = SQL_HANDLE_ENV;
	env->attr.attr_odbc_version = SQL_OV_ODBC2;
	env->attr.attr_output_nts = SQL_TRUE;

	ctx = tds_alloc_context();
	if (!ctx) {
		free(env);
		return SQL_ERROR;
	}
	env->tds_ctx = ctx;
	tds_ctx_set_parent(ctx, env);
	ctx->msg_handler = mymessagehandler;
	ctx->err_handler = myerrorhandler;

	/* ODBC has its own format */
	if (ctx->locale->date_fmt)
		free(ctx->locale->date_fmt);
	ctx->locale->date_fmt = strdup("%Y-%m-%d %H:%M:%S");

	*phenv = (SQLHENV) env;

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLAllocEnv(SQLHENV FAR * phenv)
{
	return _SQLAllocEnv(phenv);
}

static SQLRETURN SQL_API
_SQLAllocStmt(SQLHDBC hdbc, SQLHSTMT FAR * phstmt)
{
	TDS_STMT *stmt;

	INIT_HDBC;

	stmt = (TDS_STMT *) malloc(sizeof(TDS_STMT));
	if (!stmt) {
		odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
	memset(stmt, '\0', sizeof(TDS_STMT));

	stmt->htype = SQL_HANDLE_STMT;
	stmt->hdbc = dbc;
	*phstmt = (SQLHSTMT) stmt;

	ODBC_RETURN(dbc, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLAllocStmt(SQLHDBC hdbc, SQLHSTMT FAR * phstmt)
{
	INIT_HDBC;
	return _SQLAllocStmt(hdbc, phstmt);
}

SQLRETURN SQL_API
SQLBindCol(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLSMALLINT fCType, SQLPOINTER rgbValue, SQLINTEGER cbValueMax,
	   SQLINTEGER FAR * pcbValue)
{
	struct _sql_bind_info *cur, *prev = NULL, *newitem;

	INIT_HSTMT;
	if (icol <= 0) {
		odbc_errs_add(&stmt->errs, 0, "07009", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* find available item in list */
	cur = stmt->bind_head;
	while (cur) {
		if (cur->column_number == icol)
			break;
		prev = cur;
		cur = cur->next;
	}

	if (!cur) {
		/* didn't find it create a new one */
		newitem = (struct _sql_bind_info *) malloc(sizeof(struct _sql_bind_info));
		if (!newitem) {
			odbc_errs_add(&stmt->errs, 0, "HY001", NULL, NULL);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
		memset(newitem, 0, sizeof(struct _sql_bind_info));
		newitem->column_number = icol;
		/* if there's no head yet */
		if (!stmt->bind_head) {
			stmt->bind_head = newitem;
		} else {
			prev->next = newitem;
		}
		cur = newitem;
	}

	cur->column_bindtype = fCType;
	cur->column_bindlen = cbValueMax;
	cur->column_lenbind = (char *) pcbValue;
	cur->varaddr = (char *) rgbValue;

	/* force rebind */
	stmt->row = 0;

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLCancel(SQLHSTMT hstmt)
{
	TDSSOCKET *tds;

	INIT_HSTMT;
	tds = stmt->hdbc->tds_socket;

	/* TODO this can fail... */
	tds_send_cancel(tds);
	tds_process_cancel(tds);

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLConnect(SQLHDBC hdbc, SQLCHAR FAR * szDSN, SQLSMALLINT cbDSN, SQLCHAR FAR * szUID, SQLSMALLINT cbUID, SQLCHAR FAR * szAuthStr,
	   SQLSMALLINT cbAuthStr)
{
	SQLRETURN result;
	TDSCONNECTINFO *connect_info;

	INIT_HDBC;

#ifdef TDS_NO_DM
	if (szDSN && !IS_VALID_LEN(cbDSN)) {
		odbc_errs_add(&dbc->errs, 0, "HY090", "Invalid DSN buffer length", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	if (szUID && !IS_VALID_LEN(cbUID)) {
		odbc_errs_add(&dbc->errs, 0, "HY090", "Invalid UID buffer length", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	if (szAuthStr && !IS_VALID_LEN(cbAuthStr)) {
		odbc_errs_add(&dbc->errs, 0, "HY090", "Invalid PWD buffer length", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
#endif

	connect_info = tds_alloc_connect(dbc->henv->tds_ctx->locale);
	if (!connect_info) {
		odbc_errs_add(&dbc->errs, 0, "HY001", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	/* data source name */
	if (szDSN || (*szDSN))
		tds_dstr_copyn(&dbc->dsn, szDSN, odbc_get_string_size(cbDSN, szDSN));
	else
		tds_dstr_copy(&dbc->dsn, "DEFAULT");


	if (!odbc_get_dsn_info(tds_dstr_cstr(&dbc->dsn), connect_info)) {
		tds_free_connect(connect_info);
		odbc_errs_add(&dbc->errs, 0, "IM007", "Error getting DSN information", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	/* username/password are never saved to ini file, 
	 * so you do not check in ini file */
	/* user id */
	if (szUID && (*szUID))
		tds_dstr_copyn(&connect_info->user_name, (char *) szUID, odbc_get_string_size(cbUID, szUID));

	/* password */
	if (szAuthStr)
		tds_dstr_copyn(&connect_info->password, (char *) szAuthStr, odbc_get_string_size(cbAuthStr, szAuthStr));

	/* DO IT */
	if ((result = do_connect(dbc, connect_info)) != SQL_SUCCESS) {
		tds_free_connect(connect_info);
		ODBC_RETURN(dbc, result);
	}

	tds_free_connect(connect_info);
	if (dbc->errs.num_errors != 0)
		ODBC_RETURN(dbc, SQL_SUCCESS_WITH_INFO);
	ODBC_RETURN(dbc, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLDescribeCol(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLCHAR FAR * szColName, SQLSMALLINT cbColNameMax, SQLSMALLINT FAR * pcbColName,
	       SQLSMALLINT FAR * pfSqlType, SQLUINTEGER FAR * pcbColDef, SQLSMALLINT FAR * pibScale, SQLSMALLINT FAR * pfNullable)
{
	TDSSOCKET *tds;
	TDSCOLINFO *colinfo;
	int cplen;
	SQLRETURN result = SQL_SUCCESS;

	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;
	if (icol <= 0 || tds->res_info == NULL || icol > tds->res_info->num_cols) {
		odbc_errs_add(&stmt->errs, 0, "07009", "Column out of range", NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
	/* check name length */
	if (cbColNameMax < 0) {
		odbc_errs_add(&stmt->errs, 0, "HY090", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
	colinfo = tds->res_info->columns[icol - 1];

	/* cbColNameMax can be 0 (to retrieve name length) */
	if (szColName && cbColNameMax) {
		/* straight copy column name up to cbColNameMax */
		cplen = strlen(colinfo->column_name);
		if (cplen >= cbColNameMax) {
			cplen = cbColNameMax - 1;
			odbc_errs_add(&stmt->errs, 0, "01004", NULL, NULL);
			result = SQL_SUCCESS_WITH_INFO;
		}
		strncpy((char *) szColName, colinfo->column_name, cplen);
		szColName[cplen] = '\0';
	}
	if (pcbColName) {
		/* return column name length (without terminator) 
		 * as specification return full length, not copied length */
		*pcbColName = strlen(colinfo->column_name);
	}
	if (pfSqlType) {
		*pfSqlType =
			odbc_tds_to_sql_type(colinfo->column_type, colinfo->column_size, stmt->hdbc->henv->attr.attr_odbc_version);
	}

	if (pcbColDef) {
		if (is_numeric_type(colinfo->column_type)) {
			*pcbColDef = colinfo->column_prec;
		} else {
			*pcbColDef = colinfo->column_size;
		}
	}
	if (pibScale) {
		if (is_numeric_type(colinfo->column_type)) {
			*pibScale = colinfo->column_scale;
		} else {
			*pibScale = 0;
		}
	}
	if (pfNullable) {
		*pfNullable = is_nullable_type(colinfo->column_type) ? 1 : 0;
	}
	ODBC_RETURN(stmt, result);
}

SQLRETURN SQL_API
SQLColAttributes(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLUSMALLINT fDescType, SQLPOINTER rgbDesc, SQLSMALLINT cbDescMax,
		 SQLSMALLINT FAR * pcbDesc, SQLINTEGER FAR * pfDesc)
{
	TDSSOCKET *tds;
	TDSCOLINFO *colinfo;
	int cplen, len = 0;
	TDS_DBC *dbc;
	SQLRETURN result = SQL_SUCCESS;

	INIT_HSTMT;

	dbc = stmt->hdbc;
	tds = dbc->tds_socket;

	/* dont check column index for these */
	switch (fDescType) {
	case SQL_COLUMN_COUNT:
		if (!tds->res_info) {
			*pfDesc = 0;
		} else {
			*pfDesc = tds->res_info->num_cols;
		}
		ODBC_RETURN(stmt, SQL_SUCCESS);
		break;
	}

	if (!tds->res_info) {
		odbc_errs_add(&stmt->errs, 0, "07005", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	if (icol == 0 || icol > tds->res_info->num_cols) {
		odbc_errs_add(&stmt->errs, 0, "07009", "Column out of range", NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
	colinfo = tds->res_info->columns[icol - 1];

	tdsdump_log(TDS_DBG_INFO1, "odbc:SQLColAttributes: fDescType is %d\n", fDescType);
	switch (fDescType) {
	case SQL_COLUMN_NAME:
	case SQL_COLUMN_LABEL:
		len = strlen(colinfo->column_name);
		cplen = len;
		if (len >= cbDescMax) {
			cplen = cbDescMax - 1;
			odbc_errs_add(&stmt->errs, 0, "01004", NULL, NULL);
			result = SQL_SUCCESS_WITH_INFO;
		}
		tdsdump_log(TDS_DBG_INFO2, "SQLColAttributes: copying %d bytes, len = %d, cbDescMax = %d\n", cplen, len, cbDescMax);
		strncpy((char *) rgbDesc, colinfo->column_name, cplen);
		((char *) rgbDesc)[cplen] = '\0';
		/* return length of full string, not only copied part */
		if (pcbDesc) {
			*pcbDesc = len;
		}
		break;
	case SQL_COLUMN_TYPE:
	case SQL_DESC_TYPE:
		*pfDesc =
			odbc_tds_to_sql_type(colinfo->column_type, colinfo->column_size, stmt->hdbc->henv->attr.attr_odbc_version);
		tdsdump_log(TDS_DBG_INFO2,
			    "odbc:SQLColAttributes: colinfo->column_type = %d," " colinfo->column_size = %d," " *pfDesc = %d\n",
			    colinfo->column_type, colinfo->column_size, *pfDesc);
		break;
	case SQL_COLUMN_PRECISION:	/* this section may be wrong */
		switch (colinfo->column_type) {
		case SYBNUMERIC:
		case SYBDECIMAL:
			*pfDesc = colinfo->column_prec;
			break;
		case SYBCHAR:
		case SYBVARCHAR:
			*pfDesc = colinfo->column_size;
			break;
		case SYBDATETIME:
		case SYBDATETIME4:
		case SYBDATETIMN:
			*pfDesc = 30;
			break;
			/* FIXME what to do with other types ?? */
		default:
			*pfDesc = 0;
			break;
		}
		break;
	case SQL_COLUMN_LENGTH:
		*pfDesc = colinfo->column_size;
		break;
	case SQL_COLUMN_DISPLAY_SIZE:
		switch (odbc_tds_to_sql_type(colinfo->column_type, colinfo->column_size, stmt->hdbc->henv->attr.attr_odbc_version)) {
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
			*pfDesc = colinfo->column_size;
			break;
		case SQL_BIGINT:
			*pfDesc = 20;
			break;
		case SQL_INTEGER:
			*pfDesc = 11;	/* -1000000000 */
			break;
		case SQL_SMALLINT:
			*pfDesc = 6;	/* -10000 */
			break;
		case SQL_TINYINT:
			*pfDesc = 3;	/* 255 */
			break;
		case SQL_DECIMAL:
		case SQL_NUMERIC:
			*pfDesc = colinfo->column_prec + 2;
			break;
		case SQL_DATE:
			/* FIXME check always yyyy-mm-dd ?? */
			*pfDesc = 19;
			break;
		case SQL_TIME:
			/* FIXME check always hh:mm:ss[.fff] */
			*pfDesc = 19;
			break;
		case SQL_TYPE_TIMESTAMP:
		case SQL_TIMESTAMP:
			*pfDesc = 24;	/* FIXME check, always format 
					 * yyyy-mm-dd hh:mm:ss[.fff] ?? */
			/* spinellia@acm.org: int token.c it is 30 should we comply? */
			break;
		case SQL_FLOAT:
		case SQL_REAL:
		case SQL_DOUBLE:
			*pfDesc = 24;	/* FIXME -- what should the correct size be? */
			break;
		case SQL_GUID:
			*pfDesc = 36;
			break;
		default:
			/* FIXME TODO finish, should support ALL types (interval) */
			*pfDesc = 40;
			tdsdump_log(TDS_DBG_INFO1,
				    "SQLColAttributes(%d,SQL_COLUMN_DISPLAY_SIZE): unknown client type %d\n",
				    icol, odbc_tds_to_sql_type(colinfo->column_type, colinfo->column_size,
							       stmt->hdbc->henv->attr.attr_odbc_version)
				);
			break;
		}
		break;
		/* FIXME other types ... */
	default:
		tdsdump_log(TDS_DBG_INFO2, "odbc:SQLColAttributes: fDescType %d not catered for...\n");
		break;
	}
	ODBC_RETURN(stmt, result);
}


SQLRETURN SQL_API
SQLDisconnect(SQLHDBC hdbc)
{
	INIT_HDBC;

	tds_free_socket(dbc->tds_socket);
	dbc->tds_socket = NULL;

	/* TODO free all associated statements (done by DM??) f77 */

	ODBC_RETURN(dbc, SQL_SUCCESS);
}

static int
mymessagehandler(TDSCONTEXT * ctx, TDSSOCKET * tds, TDSMSGINFO * msg)
{
	struct _sql_errors *errs = NULL;
	TDS_DBC *dbc;

	/*
	 * if (asprintf(&p,
	 * " Msg %d, Level %d, State %d, Server %s, Line %d\n%s\n",
	 * msg->msg_number, msg->msg_level, msg->msg_state, msg->server, msg->line_number, msg->message) < 0)
	 * return 0;
	 */
	if (tds && tds->parent) {
		dbc = (TDS_DBC *) tds->parent;
		errs = &dbc->errs;
		if (dbc->current_statement)
			errs = &dbc->current_statement->errs;
		/* set server info if not setted in dbc */
		if (msg->server && tds_dstr_isempty(&dbc->server))
			tds_dstr_copy(&dbc->server, msg->server);
	} else if (ctx->parent) {
		errs = &((TDS_ENV *) ctx->parent)->errs;
	}
	if (errs)
		odbc_errs_add_rdbms(errs, msg->msg_number, msg->sql_state, msg->message, msg->line_number, msg->msg_level,
				    msg->server);
	return 1;
}

static int
myerrorhandler(TDSCONTEXT * ctx, TDSSOCKET * tds, TDSMSGINFO * msg)
{
	struct _sql_errors *errs = NULL;
	TDS_DBC *dbc;

	/*
	 * if (asprintf(&p,
	 * " Err %d, Level %d, State %d, Server %s, Line %d\n%s\n",
	 * msg->msg_number, msg->msg_level, msg->msg_state, msg->server, msg->line_number, msg->message) < 0)
	 * return 0;
	 */
	if (tds && tds->parent) {
		dbc = (TDS_DBC *) tds->parent;
		errs = &dbc->errs;
		if (dbc->current_statement)
			errs = &dbc->current_statement->errs;
		/* set server info if not setted in dbc */
		if (msg->server && tds_dstr_isempty(&dbc->server))
			tds_dstr_copy(&dbc->server, msg->server);
	} else if (ctx->parent) {
		errs = &((TDS_ENV *) ctx->parent)->errs;
	}
	if (errs)
		odbc_errs_add_rdbms(errs, msg->msg_number, msg->sql_state, msg->message, msg->line_number, msg->msg_level,
				    msg->server);
	return 1;
}

static SQLRETURN SQL_API
_SQLExecute(TDS_STMT * stmt)
{
	int ret;
	TDSSOCKET *tds = stmt->hdbc->tds_socket;
	TDS_INT result_type;
	TDS_INT done = 0;
	SQLRETURN result = SQL_SUCCESS;
	int in_row = 0;
	int done_flags;

	stmt->row = 0;

	/* TODO submit rpc with more parameters */
	if (stmt->param_count == 0 && stmt->prepared_query_is_rpc) {
		/* get rpc name */
		/* TODO change method */
		char *end = stmt->query, tmp;

		if (*end == '[')
			end = (char *) tds_skip_quoted(end);
		else
			while (!isspace(*++end) && *end);
		tmp = *end;
		*end = 0;
		ret = tds_submit_rpc(tds, stmt->query, NULL);
		*end = tmp;
		if (ret != TDS_SUCCEED)
			ODBC_RETURN(stmt, SQL_ERROR);
	} else if (!(tds_submit_query(tds, stmt->query, NULL) == TDS_SUCCEED))
		ODBC_RETURN(stmt, SQL_ERROR);
	stmt->hdbc->current_statement = stmt;

	/* TODO review this, ODBC return parameter in other way, for compute I don't know */
	while ((ret = tds_process_result_tokens(tds, &result_type, &done_flags)) == TDS_SUCCEED) {
		switch (result_type) {
		case TDS_COMPUTE_RESULT:
		case TDS_ROW_RESULT:
			done = 1;
			break;

		case TDS_STATUS_RESULT:
			odbc_set_return_status(stmt);
			break;
		case TDS_PARAM_RESULT:
			odbc_set_return_params(stmt);
			break;

		case TDS_DONE_RESULT:
		case TDS_DONEPROC_RESULT:
			if (!(done_flags & TDS_DONE_COUNT) && !(done_flags & TDS_DONE_ERROR))
				break;
			if (done_flags & TDS_DONE_ERROR)
				result = SQL_ERROR;
			done = 1;
			break;

			/* TODO test flags ? check error and change result ? */
		case TDS_DONEINPROC_RESULT:
			if (in_row)
				done = 1;
			break;

			/* ignore metadata, stop at done or row */
		case TDS_COMPUTEFMT_RESULT:
		case TDS_ROWFMT_RESULT:
			if (in_row) {
				done = 1;
				break;
			}
			tds->rows_affected = TDS_NO_COUNT;
			stmt->row = 0;
			result = SQL_SUCCESS;
			in_row = 1;
			break;

		case TDS_MSG_RESULT:
		case TDS_DESCRIBE_RESULT:
			break;
		}
		if (done)
			break;
	}
	switch (ret) {
	case TDS_NO_MORE_RESULTS:
		if (result == SQL_SUCCESS && stmt->errs.num_errors != 0)
			ODBC_RETURN(stmt, SQL_SUCCESS_WITH_INFO);
		ODBC_RETURN(stmt, result);
	case TDS_SUCCEED:
		if (result == SQL_SUCCESS && stmt->errs.num_errors != 0)
			ODBC_RETURN(stmt, SQL_SUCCESS_WITH_INFO);
		ODBC_RETURN(stmt, result);
	default:
		/* TODO test what happened, report correct error to client */
		tdsdump_log(TDS_DBG_INFO1, "SQLExecute: bad results\n");
		ODBC_RETURN(stmt, SQL_ERROR);
	}
}

SQLRETURN SQL_API
SQLExecDirect(SQLHSTMT hstmt, SQLCHAR FAR * szSqlStr, SQLINTEGER cbSqlStr)
{
	INIT_HSTMT;

	if (SQL_SUCCESS != odbc_set_stmt_query(stmt, (char *) szSqlStr, cbSqlStr))
		ODBC_RETURN(stmt, SQL_ERROR);

	/* count placeholders */
	/* note: szSqlStr can be no-null terminated, so first we set query and then count placeholders */
	stmt->param_count = tds_count_placeholders(stmt->query);

	if (SQL_SUCCESS != prepare_call(stmt))
		ODBC_RETURN(stmt, SQL_ERROR);

	if (stmt->param_count) {
		SQLRETURN res;

		assert(stmt->prepared_query == NULL);
		stmt->prepared_query = stmt->query;
		stmt->query = NULL;

		res = start_parse_prepared_query(stmt);

		if (SQL_SUCCESS != res)
			ODBC_RETURN(stmt, res);
	}

	return _SQLExecute(stmt);
}

SQLRETURN SQL_API
SQLExecute(SQLHSTMT hstmt)
{
#ifdef ENABLE_DEVELOPING
	TDSSOCKET *tds;
	TDSDYNAMIC *dyn;
	struct _sql_param_info *param;
	TDS_INT result_type;
	int ret, done, done_flags;
	SQLRETURN result = SQL_NO_DATA;
	int i, nparam;
	TDSPARAMINFO *params = NULL, *temp_params;
	int in_row = 0;
#endif

	INIT_HSTMT;

#ifdef ENABLE_DEVELOPING
	tds = stmt->hdbc->tds_socket;

	/* FIXME SQLExecute return SQL_ERROR if binding is dynamic (data incomplete) */

	/* TODO rebuild should be done for every bingings change, not every time */

	/* build parameters list */
	tdsdump_log(TDS_DBG_INFO1, "Setting input parameters\n");
	for (i = (stmt->prepared_query_is_func ? 1 : 0), nparam = 0; ++i <= (int) stmt->param_count; ++nparam) {
		/* find binded parameter */
		param = odbc_find_param(stmt, i);
		if (!param) {
			tds_free_param_results(params);
			ODBC_RETURN(stmt, SQL_ERROR);
		}

		/* add a columns to parameters */
		if (!(temp_params = tds_alloc_param_result(params))) {
			tds_free_param_results(params);
			odbc_errs_add(&stmt->errs, 0, "HY001", NULL, NULL);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
		params = temp_params;

		/* add another type and copy data */
		if (sql2tds(stmt->hdbc, param, params, nparam) < 0) {
			tds_free_param_results(params);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
	}

	/* TODO test if two SQLPrepare on a statement */
	/* TODO unprepare on statement free or connection close */
	/* prepare dynamic query (only for first SQLExecute call) */
	if (!stmt->dyn && !stmt->prepared_query_is_rpc) {
		tdsdump_log(TDS_DBG_INFO1, "Creating prepared statement\n");
		/* TODO use tds_submit_prepexec */
		if (tds_submit_prepare(tds, stmt->prepared_query, NULL, &stmt->dyn, params) == TDS_FAIL) {
			tds_free_param_results(params);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
		if (tds_process_simple_query(tds) != TDS_SUCCEED) {
			tds_free_param_results(params);
			ODBC_RETURN(stmt, SQL_ERROR);
		}
	}
	if (!stmt->prepared_query_is_rpc) {
		dyn = stmt->dyn;
		tds_free_input_params(dyn);
		dyn->params = params;
		tdsdump_log(TDS_DBG_INFO1, "End prepare, execute\n");
		/* TODO return error to client */
		if (tds_submit_execute(tds, dyn) == TDS_FAIL)
			ODBC_RETURN(stmt, SQL_ERROR);
	} else {
		/* get rpc name */
		/* TODO change method */
		char *end = stmt->prepared_query, tmp;

		if (*end == '[')
			end = (char *) tds_skip_quoted(end);
		else
			while (!isspace(*++end) && *end);
		tmp = *end;
		*end = 0;
		ret = tds_submit_rpc(tds, stmt->prepared_query, params);
		*end = tmp;
		tds_free_param_results(params);
		if (ret != TDS_SUCCEED)
			ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* TODO copied from _SQLExecute, use a function... */
	stmt->hdbc->current_statement = stmt;

	done = 0;
	while ((ret = tds_process_result_tokens(tds, &result_type, &done_flags)) == TDS_SUCCEED) {
		switch (result_type) {
		case TDS_COMPUTE_RESULT:
		case TDS_ROW_RESULT:
			result = SQL_SUCCESS;
			done = 1;
			break;

		case TDS_STATUS_RESULT:
			result = SQL_SUCCESS;
			odbc_set_return_status(stmt);
			break;
		case TDS_PARAM_RESULT:
			odbc_set_return_params(stmt);
			break;

		case TDS_DONE_RESULT:
		case TDS_DONEPROC_RESULT:
			if (!(done_flags & TDS_DONE_COUNT) && !(done_flags & TDS_DONE_ERROR))
				break;
			if (done_flags & TDS_DONE_ERROR)
				result = SQL_ERROR;
			else
				result = SQL_SUCCESS;
			done = 1;
			break;

			/* TODO test flags ? check error and change result ? */
		case TDS_DONEINPROC_RESULT:
			if (in_row)
				done = 1;
			break;

			/* ignore metadata, stop at done or row */
		case TDS_COMPUTEFMT_RESULT:
		case TDS_ROWFMT_RESULT:
			if (in_row) {
				done = 1;
				break;
			}
			tds->rows_affected = TDS_NO_COUNT;
			stmt->row = 0;
			result = SQL_SUCCESS;
			in_row = 1;
			break;

		case TDS_MSG_RESULT:
		case TDS_DESCRIBE_RESULT:
			result = SQL_SUCCESS;
			break;
		}
		if (done)
			break;
	}
	ODBC_RETURN(stmt, result);
#else /* ENABLE_DEVELOPING */

	if (stmt->prepared_query) {
		SQLRETURN res = start_parse_prepared_query(stmt);

		if (SQL_SUCCESS != res)
			ODBC_RETURN(stmt, res);
	}

	return _SQLExecute(stmt);
#endif
}

SQLRETURN SQL_API
SQLFetch(SQLHSTMT hstmt)
{
	int ret;
	TDSSOCKET *tds;
	TDSRESULTINFO *resinfo;
	TDSCOLINFO *colinfo;
	int i;
	SQLINTEGER len = 0;
	TDS_CHAR *src;
	int srclen;
	struct _sql_bind_info *cur;
	TDSLOCALE *locale;
	TDSCONTEXT *context;
	TDS_INT rowtype;
	TDS_INT computeid;


	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;

	context = stmt->hdbc->henv->tds_ctx;
	locale = context->locale;

	/* if we bound columns, transfer them to res_info now that we have one */
	if (stmt->row == 0) {
		cur = stmt->bind_head;
		while (cur) {
			if (cur->column_number > 0 && cur->column_number <= tds->res_info->num_cols) {
				colinfo = tds->res_info->columns[cur->column_number - 1];
				colinfo->column_varaddr = cur->varaddr;
				colinfo->column_bindtype = cur->column_bindtype;
				colinfo->column_bindlen = cur->column_bindlen;
				colinfo->column_lenbind = cur->column_lenbind;
			} else {
				/* log error ? */
			}
			cur = cur->next;
		}
	}
	stmt->row++;

	ret = tds_process_row_tokens(stmt->hdbc->tds_socket, &rowtype, &computeid);
	if (ret == TDS_NO_MORE_ROWS) {
		tdsdump_log(TDS_DBG_INFO1, "SQLFetch: NO_DATA_FOUND\n");
		ODBC_RETURN(stmt, SQL_NO_DATA_FOUND);
	}
	resinfo = tds->res_info;
	if (!resinfo) {
		tdsdump_log(TDS_DBG_INFO1, "SQLFetch: !resinfo\n");
		ODBC_RETURN(stmt, SQL_NO_DATA_FOUND);
	}
	for (i = 0; i < resinfo->num_cols; i++) {
		colinfo = resinfo->columns[i];
		colinfo->column_text_sqlgetdatapos = 0;
		if (colinfo->column_varaddr && !tds_get_null(resinfo->current_row, i)) {
			src = (TDS_CHAR *) & resinfo->current_row[colinfo->column_offset];
			if (is_blob_type(colinfo->column_type))
				src = ((TDSBLOBINFO *) src)->textvalue;
			srclen = colinfo->column_cur_size;
			len = convert_tds2sql(context,
					      tds_get_conversion_type(colinfo->column_type, colinfo->column_size),
					      src,
					      srclen, colinfo->column_bindtype, colinfo->column_varaddr, colinfo->column_bindlen);
			if (len < 0)
				ODBC_RETURN(stmt, SQL_ERROR);
		}
		if (colinfo->column_lenbind) {
			if (tds_get_null(resinfo->current_row, i))
				*((SQLINTEGER *) colinfo->column_lenbind) = SQL_NULL_DATA;
			else
				*((SQLINTEGER *) colinfo->column_lenbind) = len;
		}
	}
	if (ret == TDS_SUCCEED) {
		ODBC_RETURN(stmt, SQL_SUCCESS);
	} else {
		tdsdump_log(TDS_DBG_INFO1, "SQLFetch: !TDS_SUCCEED (%d)\n", ret);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
}


#if (ODBCVER >= 0x0300)
SQLRETURN SQL_API
SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle)
{
	tdsdump_log(TDS_DBG_INFO1, "SQLFreeHandle(%d, 0x%x)\n", HandleType, Handle);

	switch (HandleType) {
	case SQL_HANDLE_STMT:
		return _SQLFreeStmt(Handle, SQL_DROP);
		break;
	case SQL_HANDLE_DBC:
		return _SQLFreeConnect(Handle);
		break;
	case SQL_HANDLE_ENV:
		return _SQLFreeEnv(Handle);
		break;
	}
	return SQL_ERROR;
}

static SQLRETURN SQL_API
_SQLFreeConnect(SQLHDBC hdbc)
{
	INIT_HDBC;

	tds_free_socket(dbc->tds_socket);

	/* free attributes */
	tds_dstr_free(&dbc->attr.attr_current_catalog);
	tds_dstr_free(&dbc->attr.attr_translate_lib);

	tds_dstr_free(&dbc->server);
	tds_dstr_free(&dbc->dsn);

	odbc_errs_reset(&dbc->errs);

	free(dbc);

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLFreeConnect(SQLHDBC hdbc)
{
	return _SQLFreeConnect(hdbc);
}
#endif

static SQLRETURN SQL_API
_SQLFreeEnv(SQLHENV henv)
{
	INIT_HENV;

	tds_free_context(env->tds_ctx);
	odbc_errs_reset(&env->errs);
	free(env);

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLFreeEnv(SQLHENV henv)
{
	return _SQLFreeEnv(henv);
}

static SQLRETURN SQL_API
_SQLFreeStmt(SQLHSTMT hstmt, SQLUSMALLINT fOption)
{
	TDSSOCKET *tds;

	INIT_HSTMT;

	/* check if option correct */
	if (fOption != SQL_DROP && fOption != SQL_CLOSE && fOption != SQL_UNBIND && fOption != SQL_RESET_PARAMS) {
		tdsdump_log(TDS_DBG_ERROR, "odbc:SQLFreeStmt: Unknown option %d\n", fOption);
		odbc_errs_add(&stmt->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* if we have bound columns, free the temporary list */
	if (fOption == SQL_DROP || fOption == SQL_UNBIND) {
		struct _sql_bind_info *cur, *tmp;

		if (stmt->bind_head) {
			cur = stmt->bind_head;
			while (cur) {
				tmp = cur->next;
				free(cur);
				cur = tmp;
			}
			stmt->bind_head = NULL;
		}
	}

	/* do the same for bound parameters */
	if (fOption == SQL_DROP || fOption == SQL_RESET_PARAMS) {
		struct _sql_param_info *cur, *tmp;

		if (stmt->param_head) {
			cur = stmt->param_head;
			while (cur) {
				tmp = cur->next;
				free(cur);
				cur = tmp;
			}
			stmt->param_head = NULL;
		}
	}

	/* close statement */
	if (fOption == SQL_DROP || fOption == SQL_CLOSE) {
		SQLRETURN retcode;

		tds = stmt->hdbc->tds_socket;
		/* 
		 * FIX ME -- otherwise make sure the current statement is complete
		 */
		/* do not close other running query ! */
		if (tds->state != TDS_IDLE && stmt->hdbc->current_statement == stmt) {
			tds_send_cancel(tds);
			tds_process_cancel(tds);
		}

		/* close prepared statement or add to connection */
		retcode = odbc_free_dynamic(stmt);
		if (retcode != SQL_SUCCESS)
			return retcode;
	}

	/* free it */
	if (fOption == SQL_DROP) {
		if (stmt->query)
			free(stmt->query);
		if (stmt->prepared_query)
			free(stmt->prepared_query);
		odbc_errs_reset(&stmt->errs);
		if (stmt->hdbc->current_statement == stmt)
			stmt->hdbc->current_statement = NULL;
		free(stmt);

		/* NOTE we freed stmt, do not use ODBC_RETURN */
		return SQL_SUCCESS;
	}
	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLFreeStmt(SQLHSTMT hstmt, SQLUSMALLINT fOption)
{
	return _SQLFreeStmt(hstmt, fOption);
}

#if (ODBCVER >= 0x0300)
SQLRETURN SQL_API
SQLGetStmtAttr(SQLHSTMT hstmt, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER BufferLength, SQLINTEGER * StringLength)
{
	SQLINTEGER tmp_len;
	SQLUINTEGER *ui_val = (SQLUINTEGER *) Value;

	INIT_HSTMT;

	if (!StringLength)
		StringLength = &tmp_len;

	switch (Attribute) {
	case SQL_ATTR_ASYNC_ENABLE:
		*ui_val = SQL_ASYNC_ENABLE_OFF;
		break;

	case SQL_ATTR_CONCURRENCY:
		*ui_val = SQL_CONCUR_READ_ONLY;
		break;

	case SQL_ATTR_CURSOR_SCROLLABLE:
		*ui_val = SQL_NONSCROLLABLE;
		break;

	case SQL_ATTR_CURSOR_SENSITIVITY:
		*ui_val = SQL_UNSPECIFIED;
		break;

	case SQL_ATTR_CURSOR_TYPE:
		*ui_val = SQL_CURSOR_FORWARD_ONLY;
		break;

	case SQL_ATTR_ENABLE_AUTO_IPD:
		*ui_val = SQL_FALSE;
		break;

	case SQL_ATTR_KEYSET_SIZE:
	case SQL_ATTR_MAX_LENGTH:
	case SQL_ATTR_MAX_ROWS:
		*ui_val = 0;
		break;

	case SQL_ATTR_NOSCAN:
		*ui_val = SQL_NOSCAN_OFF;
		break;

	case SQL_ATTR_PARAM_BIND_TYPE:
		*ui_val = SQL_PARAM_BIND_BY_COLUMN;
		break;

	case SQL_ATTR_QUERY_TIMEOUT:
		*ui_val = 0;	/* TODO return timeout in seconds */
		break;

	case SQL_ATTR_RETRIEVE_DATA:
		*ui_val = SQL_RD_ON;
		break;

	case SQL_ATTR_ROW_ARRAY_SIZE:
		*ui_val = 1;
		break;

	case SQL_ATTR_ROW_NUMBER:
		*ui_val = 0;	/* TODO */
		break;

	case SQL_ATTR_USE_BOOKMARKS:
		*ui_val = SQL_UB_OFF;
		break;

		/* This make MS ODBC not crash */
	case SQL_ATTR_APP_ROW_DESC:
		*(SQLPOINTER *) Value = &stmt->ard;
		*StringLength = sizeof(SQL_IS_POINTER);
		break;

	case SQL_ATTR_IMP_ROW_DESC:
		*(SQLPOINTER *) Value = &stmt->ird;
		*StringLength = sizeof(SQL_IS_POINTER);
		break;

	case SQL_ATTR_APP_PARAM_DESC:
		*(SQLPOINTER *) Value = &stmt->apd;
		*StringLength = sizeof(SQL_IS_POINTER);
		break;

	case SQL_ATTR_IMP_PARAM_DESC:
		*(SQLPOINTER *) Value = &stmt->ipd;
		*StringLength = sizeof(SQL_IS_POINTER);
		break;

		/* TODO ?? what to do */
	case SQL_ATTR_FETCH_BOOKMARK_PTR:
	case SQL_ATTR_METADATA_ID:
	case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
	case SQL_ATTR_PARAM_OPERATION_PTR:
	case SQL_ATTR_PARAM_STATUS_PTR:
	case SQL_ATTR_PARAMS_PROCESSED_PTR:
	case SQL_ATTR_PARAMSET_SIZE:
	case SQL_ATTR_ROW_BIND_OFFSET_PTR:
	case SQL_ATTR_ROW_BIND_TYPE:
	case SQL_ATTR_ROW_OPERATION_PTR:
	case SQL_ATTR_ROW_STATUS_PTR:
	case SQL_ATTR_ROWS_FETCHED_PTR:
	case SQL_ATTR_SIMULATE_CURSOR:
	default:
		odbc_errs_add(&stmt->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
	ODBC_RETURN(stmt, SQL_SUCCESS);
}
#endif

#if 0
SQLRETURN SQL_API
SQLGetCursorName(SQLHSTMT hstmt, SQLCHAR FAR * szCursor, SQLSMALLINT cbCursorMax, SQLSMALLINT FAR * pcbCursor)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLGetCursorName: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}
#endif

SQLRETURN SQL_API
SQLNumResultCols(SQLHSTMT hstmt, SQLSMALLINT FAR * pccol)
{
	TDSRESULTINFO *resinfo;
	TDSSOCKET *tds;

	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;
	resinfo = tds->res_info;
	if (resinfo == NULL) {
		/* 3/15/2001 bsb - DBD::ODBC calls SQLNumResultCols on non-result
		 * ** generating queries such as 'drop table' */
		*pccol = 0;
		ODBC_RETURN(stmt, SQL_SUCCESS);
	}

	*pccol = resinfo->num_cols;
	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLPrepare(SQLHSTMT hstmt, SQLCHAR FAR * szSqlStr, SQLINTEGER cbSqlStr)
{
	SQLRETURN retcode;

	INIT_HSTMT;

	/* try to free dynamic associated */
	retcode = odbc_free_dynamic(stmt);
	if (retcode != SQL_SUCCESS)
		return retcode;

	if (SQL_SUCCESS != odbc_set_stmt_prepared_query(stmt, (char *) szSqlStr, cbSqlStr))
		ODBC_RETURN(stmt, SQL_ERROR);

	/* count parameters */
	stmt->param_count = tds_count_placeholders(stmt->prepared_query);

	/* trasform to native (one time, not for every SQLExecute) */
	if (SQL_SUCCESS != prepare_call(stmt))
		ODBC_RETURN(stmt, SQL_ERROR);

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN
_SQLRowCount(SQLHSTMT hstmt, SQLINTEGER FAR * pcrow)
{
	TDSSOCKET *tds;

	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;

	/* test is this is current statement */
	if (stmt->hdbc->current_statement != stmt)
		ODBC_RETURN(stmt, SQL_ERROR);
	*pcrow = -1;
	if (tds->rows_affected != TDS_NO_COUNT)
		*pcrow = tds->rows_affected;
	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLRowCount(SQLHSTMT hstmt, SQLINTEGER FAR * pcrow)
{
	return _SQLRowCount(hstmt, pcrow);
}

#if 0
SQLRETURN SQL_API
SQLSetCursorName(SQLHSTMT hstmt, SQLCHAR FAR * szCursor, SQLSMALLINT cbCursor)
{
	INIT_HSTMT;
	odbc_errs_add(&stmt->errs, 0, "HYC00", "SQLSetCursorName: function not implemented", NULL);
	ODBC_RETURN(stmt, SQL_ERROR);
}
#endif

/* TODO join all this similar function... */
/* spinellia@acm.org : copied shamelessly from change_database */
/* transaction support */
/* 1 = commit, 0 = rollback */
static SQLRETURN
change_transaction(TDS_DBC * dbc, int state)
{
	TDSSOCKET *tds = dbc->tds_socket;

	tdsdump_log(TDS_DBG_INFO1, "change_transaction(0x%x,%d)\n", dbc, state);

	if (tds_submit_query(tds, state ? "commit" : "rollback", NULL) != TDS_SUCCEED) {
		odbc_errs_add(&dbc->errs, 0, "HY000", "Could not perform COMMIT or ROLLBACK", NULL);
		return SQL_ERROR;
	}

	if (tds_process_simple_query(tds) != TDS_SUCCEED)
		return SQL_ERROR;

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLTransact(SQLHENV henv, SQLHDBC hdbc, SQLUSMALLINT fType)
{
	int op = (fType == SQL_COMMIT ? 1 : 0);

	/* I may live without a HENV */
	/*     CHECK_HENV; */
	/* ..but not without a HDBC! */
	INIT_HDBC;

	tdsdump_log(TDS_DBG_INFO1, "SQLTransact(0x%x,0x%x,%d)\n", henv, hdbc, fType);
	ODBC_RETURN(dbc, change_transaction(dbc, op));
}

#if ODBCVER >= 0x300
SQLRETURN SQL_API
SQLEndTran(SQLSMALLINT handleType, SQLHANDLE handle, SQLSMALLINT completionType)
{
	switch (handleType) {
	case SQL_HANDLE_ENV:
		return SQLTransact(handle, NULL, completionType);
	case SQL_HANDLE_DBC:
		return SQLTransact(NULL, handle, completionType);
	}
	return SQL_ERROR;
}
#endif

/* end of transaction support */

SQLRETURN SQL_API
SQLSetParam(SQLHSTMT hstmt, SQLUSMALLINT ipar, SQLSMALLINT fCType, SQLSMALLINT fSqlType, SQLUINTEGER cbParamDef,
	    SQLSMALLINT ibScale, SQLPOINTER rgbValue, SQLINTEGER FAR * pcbValue)
{
	return SQLBindParameter(hstmt, ipar, SQL_PARAM_INPUT_OUTPUT, fCType, fSqlType, cbParamDef, ibScale, rgbValue,
				SQL_SETPARAM_VALUE_MAX, pcbValue);
}

/************************
 * SQLColumns
 ************************
 *
 * Return column information for a table or view. This is
 * mapped to a call to sp_columns which - lucky for us - returns
 * the exact result set we need.
 *
 * exec sp_columns [ @table_name = ] object 
 *                 [ , [ @table_owner = ] owner ] 
 *                 [ , [ @table_qualifier = ] qualifier ] 
 *                 [ , [ @column_name = ] column ] 
 *                 [ , [ @ODBCVer = ] ODBCVer ] 
 *
 ************************/
SQLRETURN SQL_API
SQLColumns(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName,	/* object_qualifier */
	   SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,	/* object_owner */
	   SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName,	/* object_name */
	   SQLSMALLINT cbTableName, SQLCHAR FAR * szColumnName,	/* column_name */
	   SQLSMALLINT cbColumnName)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_columns ", 4,
				  "@table_name", szTableName, cbTableName,
				  "@table_owner", szSchemaName, cbSchemaName,
				  "@table_qualifier", szCatalogName, cbCatalogName, "@column_name", szColumnName, cbColumnName);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
		odbc_col_setname(stmt, 7, "COLUMN_SIZE");
		odbc_col_setname(stmt, 8, "BUFFER_LENGTH");
		odbc_col_setname(stmt, 9, "DECIMAL_DIGITS");
		odbc_col_setname(stmt, 10, "NUM_PREC_RADIX");
	}
	ODBC_RETURN(stmt, retcode);
}

static SQLRETURN SQL_API
_SQLGetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER BufferLength, SQLINTEGER * StringLength)
{
	char *p = NULL;
	SQLRETURN rc;

	INIT_HDBC;

	switch (Attribute) {
	case SQL_ATTR_AUTOCOMMIT:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_autocommit;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_CONNECTION_TIMEOUT:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_connection_timeout;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_ACCESS_MODE:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_access_mode;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_CURRENT_CATALOG:
		p = tds_dstr_cstr(&dbc->attr.attr_current_catalog);
		break;
	case SQL_ATTR_LOGIN_TIMEOUT:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_login_timeout;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_ODBC_CURSORS:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_odbc_cursors;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_PACKET_SIZE:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_packet_size;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_QUIET_MODE:
		*((SQLHWND *) Value) = dbc->attr.attr_quite_mode;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
#ifdef TDS_NO_DM
	case SQL_ATTR_TRACE:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_trace;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_TRACEFILE:
		p = tds_dstr_cstr(&dbc->attr.attr_tracefile);
		break;
#endif
	case SQL_ATTR_TXN_ISOLATION:
		*((SQLUINTEGER *) Value) = dbc->attr.attr_txn_isolation;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_TRANSLATE_LIB:
	case SQL_ATTR_TRANSLATE_OPTION:
		odbc_errs_add(&dbc->errs, 0, "HYC00", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
		break;
	default:
		odbc_errs_add(&dbc->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
		break;
	}

	assert(p);

	rc = odbc_set_string_i(Value, BufferLength, StringLength, p, -1);
	ODBC_RETURN(dbc, rc);
}

#if ODBCVER >= 0x300
SQLRETURN SQL_API
SQLGetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER BufferLength, SQLINTEGER * StringLength)
{
	return (_SQLGetConnectAttr(hdbc, Attribute, Value, BufferLength, StringLength));
}
#endif

/* TODO is this OK ??? see function below */
SQLRETURN SQL_API
SQLGetConnectOption(SQLHDBC hdbc, SQLUSMALLINT fOption, SQLPOINTER pvParam)
{
	return (_SQLGetConnectAttr(hdbc, (SQLINTEGER) fOption, pvParam, SQL_IS_POINTER, NULL));
}

#if 0
SQLRETURN SQL_API
SQLGetConnectOption(SQLHDBC hdbc, SQLUSMALLINT fOption, SQLPOINTER pvParam)
{
	/* TODO implement more options
	 * AUTOCOMMIT required by DBD::ODBC
	 */
	INIT_HDBC;

	switch (fOption) {
	case SQL_AUTOCOMMIT:
		*((SQLUINTEGER *) pvParam) = dbc->autocommit_state;
		ODBC_RETURN(dbc, SQL_SUCCESS);
	case SQL_TXN_ISOLATION:
		*((SQLUINTEGER *) pvParam) = SQL_TXN_READ_COMMITTED;
		ODBC_RETURN(dbc, SQL_SUCCESS);
	default:
		tdsdump_log(TDS_DBG_INFO1, "odbc:SQLGetConnectOption: Statement option %d not implemented\n", fOption);
		odbc_errs_add(&dbc->errs, 0, "HY000", "Statement option not implemented", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}
#endif

SQLRETURN SQL_API
SQLGetData(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLSMALLINT fCType, SQLPOINTER rgbValue, SQLINTEGER cbValueMax,
	   SQLINTEGER FAR * pcbValue)
{
	TDSCOLINFO *colinfo;
	TDSRESULTINFO *resinfo;
	TDSSOCKET *tds;
	TDS_CHAR *src;
	int srclen;
	TDSLOCALE *locale;
	TDSCONTEXT *context;
	SQLINTEGER dummy_cb;
	int nSybType;

	INIT_HSTMT;

	if (!pcbValue)
		pcbValue = &dummy_cb;

	tds = stmt->hdbc->tds_socket;
	context = stmt->hdbc->henv->tds_ctx;
	locale = context->locale;
	resinfo = tds->res_info;
	if (icol == 0 || icol > tds->res_info->num_cols) {
		odbc_errs_add(&stmt->errs, 0, "07009", "Column out of range", NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
	colinfo = resinfo->columns[icol - 1];

	if (tds_get_null(resinfo->current_row, icol - 1)) {
		*pcbValue = SQL_NULL_DATA;
	} else {
		src = (TDS_CHAR *) & resinfo->current_row[colinfo->column_offset];
		if (is_blob_type(colinfo->column_type)) {
			if (colinfo->column_text_sqlgetdatapos >= colinfo->column_cur_size)
				ODBC_RETURN(stmt, SQL_NO_DATA_FOUND);

			/* FIXME why this became < 0 ??? */
			if (colinfo->column_text_sqlgetdatapos > 0)
				src = ((TDSBLOBINFO *) src)->textvalue + colinfo->column_text_sqlgetdatapos;
			else
				src = ((TDSBLOBINFO *) src)->textvalue;

			srclen = colinfo->column_cur_size - colinfo->column_text_sqlgetdatapos;
		} else {
			srclen = colinfo->column_cur_size;
		}
		nSybType = tds_get_conversion_type(colinfo->column_type, colinfo->column_size);
		/* TODO add support for SQL_C_DEFAULT */
		*pcbValue = convert_tds2sql(context, nSybType, src, srclen, fCType, (TDS_CHAR *) rgbValue, cbValueMax);
		if (*pcbValue < 0)
			ODBC_RETURN(stmt, SQL_ERROR);

		if (is_blob_type(colinfo->column_type)) {
			/* calc how many bytes was readed */
			int readed = cbValueMax;

			/* char is always terminated... */
			/* FIXME test on destination char ??? */
			if (nSybType == SYBTEXT)
				--readed;
			if (readed > *pcbValue)
				readed = *pcbValue;
			colinfo->column_text_sqlgetdatapos += readed;
			/* not all readed ?? */
			if (colinfo->column_text_sqlgetdatapos < colinfo->column_cur_size)
				ODBC_RETURN(stmt, SQL_SUCCESS_WITH_INFO);
		}
	}
	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLGetFunctions(SQLHDBC hdbc, SQLUSMALLINT fFunction, SQLUSMALLINT FAR * pfExists)
{
	int i;

	INIT_HDBC;

	tdsdump_log(TDS_DBG_FUNC, "SQLGetFunctions: fFunction is %d\n", fFunction);
	switch (fFunction) {
#if (ODBCVER >= 0x0300)
	case SQL_API_ODBC3_ALL_FUNCTIONS:
		for (i = 0; i < SQL_API_ODBC3_ALL_FUNCTIONS_SIZE; ++i) {
			pfExists[i] = 0;
		}

		/* every api available are contained in a macro 
		 * all these macro begin with API followed by 2 letter
		 * first letter mean pre ODBC 3 (_) or ODBC 3 (3)
		 * second letter mean implemented (X) or unimplemented (_)
		 * You should copy these macro 3 times... not very good
		 * but work. Perhaps best method is build the bit array statically
		 * and then use it but I don't know how to build it...
		 */
#define API_X(n) if (n >= 0 && n < (16*SQL_API_ODBC3_ALL_FUNCTIONS_SIZE)) pfExists[n/16] |= (1 << n%16);
#define API__(n)
#define API3X(n) if (n >= 0 && n < (16*SQL_API_ODBC3_ALL_FUNCTIONS_SIZE)) pfExists[n/16] |= (1 << n%16);
#define API3_(n)
		API_X(SQL_API_SQLALLOCCONNECT);
		API_X(SQL_API_SQLALLOCENV);
		API3X(SQL_API_SQLALLOCHANDLE);
		API_X(SQL_API_SQLALLOCSTMT);
		API_X(SQL_API_SQLBINDCOL);
		API_X(SQL_API_SQLBINDPARAM);
		API_X(SQL_API_SQLBINDPARAMETER);
		API__(SQL_API_SQLBROWSECONNECT);
		API3_(SQL_API_SQLBULKOPERATIONS);
		API_X(SQL_API_SQLCANCEL);
		API3_(SQL_API_SQLCLOSECURSOR);
		API3_(SQL_API_SQLCOLATTRIBUTE);
		API_X(SQL_API_SQLCOLATTRIBUTES);
		API_X(SQL_API_SQLCOLUMNPRIVILEGES);
		API_X(SQL_API_SQLCOLUMNS);
		API_X(SQL_API_SQLCONNECT);
		API3_(SQL_API_SQLCOPYDESC);
		API_X(SQL_API_SQLDESCRIBECOL);
		API__(SQL_API_SQLDESCRIBEPARAM);
		API_X(SQL_API_SQLDISCONNECT);
		API_X(SQL_API_SQLDRIVERCONNECT);
		API3X(SQL_API_SQLENDTRAN);
		API_X(SQL_API_SQLERROR);
		API_X(SQL_API_SQLEXECDIRECT);
		API_X(SQL_API_SQLEXECUTE);
		API__(SQL_API_SQLEXTENDEDFETCH);
		API_X(SQL_API_SQLFETCH);
		API3_(SQL_API_SQLFETCHSCROLL);
		API_X(SQL_API_SQLFOREIGNKEYS);
		API_X(SQL_API_SQLFREECONNECT);
		API_X(SQL_API_SQLFREEENV);
		API3X(SQL_API_SQLFREEHANDLE);
		API_X(SQL_API_SQLFREESTMT);
		API3X(SQL_API_SQLGETCONNECTATTR);
		API_X(SQL_API_SQLGETCONNECTOPTION);
		API__(SQL_API_SQLGETCURSORNAME);
		API_X(SQL_API_SQLGETDATA);
		API3_(SQL_API_SQLGETDESCFIELD);
		API3_(SQL_API_SQLGETDESCREC);
		API3X(SQL_API_SQLGETDIAGFIELD);
		API3X(SQL_API_SQLGETDIAGREC);
		API3X(SQL_API_SQLGETENVATTR);
		API_X(SQL_API_SQLGETFUNCTIONS);
		API_X(SQL_API_SQLGETINFO);
		API3X(SQL_API_SQLGETSTMTATTR);
		API_X(SQL_API_SQLGETSTMTOPTION);
		API_X(SQL_API_SQLGETTYPEINFO);
		API_X(SQL_API_SQLMORERESULTS);
		API_X(SQL_API_SQLNATIVESQL);
		API_X(SQL_API_SQLNUMPARAMS);
		API_X(SQL_API_SQLNUMRESULTCOLS);
		API_X(SQL_API_SQLPARAMDATA);
		API__(SQL_API_SQLPARAMOPTIONS);
		API_X(SQL_API_SQLPREPARE);
		API_X(SQL_API_SQLPRIMARYKEYS);
		API_X(SQL_API_SQLPROCEDURECOLUMNS);
		API_X(SQL_API_SQLPROCEDURES);
		API_X(SQL_API_SQLPUTDATA);
		API_X(SQL_API_SQLROWCOUNT);
		API3X(SQL_API_SQLSETCONNECTATTR);
		API_X(SQL_API_SQLSETCONNECTOPTION);
		API__(SQL_API_SQLSETCURSORNAME);
		API3_(SQL_API_SQLSETDESCFIELD);
		API3_(SQL_API_SQLSETDESCREC);
		API3X(SQL_API_SQLSETENVATTR);
		API_X(SQL_API_SQLSETPARAM);
		API__(SQL_API_SQLSETPOS);
		API__(SQL_API_SQLSETSCROLLOPTIONS);
		API3_(SQL_API_SQLSETSTMTATTR);
		API_X(SQL_API_SQLSETSTMTOPTION);
		API_X(SQL_API_SQLSPECIALCOLUMNS);
		API_X(SQL_API_SQLSTATISTICS);
		API_X(SQL_API_SQLTABLEPRIVILEGES);
		API_X(SQL_API_SQLTABLES);
		API_X(SQL_API_SQLTRANSACT);
#undef API_X
#undef API__
#undef API3X
#undef API3_
		ODBC_RETURN(dbc, SQL_SUCCESS);
#endif

	case SQL_API_ALL_FUNCTIONS:
		tdsdump_log(TDS_DBG_FUNC, "odbc:SQLGetFunctions: " "fFunction is SQL_API_ALL_FUNCTIONS\n");
		for (i = 0; i < 100; ++i) {
			pfExists[i] = SQL_FALSE;
		}

#define API_X(n) if (n >= 0 && n < 100) pfExists[n] = SQL_TRUE;
#define API__(n)
#define API3X(n)
#define API3_(n)
		API_X(SQL_API_SQLALLOCCONNECT);
		API_X(SQL_API_SQLALLOCENV);
		API3X(SQL_API_SQLALLOCHANDLE);
		API_X(SQL_API_SQLALLOCSTMT);
		API_X(SQL_API_SQLBINDCOL);
		API_X(SQL_API_SQLBINDPARAM);
		API_X(SQL_API_SQLBINDPARAMETER);
		API__(SQL_API_SQLBROWSECONNECT);
		API3_(SQL_API_SQLBULKOPERATIONS);
		API_X(SQL_API_SQLCANCEL);
		API3_(SQL_API_SQLCLOSECURSOR);
		API3_(SQL_API_SQLCOLATTRIBUTE);
		API_X(SQL_API_SQLCOLATTRIBUTES);
		API_X(SQL_API_SQLCOLUMNPRIVILEGES);
		API_X(SQL_API_SQLCOLUMNS);
		API_X(SQL_API_SQLCONNECT);
		API3_(SQL_API_SQLCOPYDESC);
		API_X(SQL_API_SQLDESCRIBECOL);
		API__(SQL_API_SQLDESCRIBEPARAM);
		API_X(SQL_API_SQLDISCONNECT);
		API_X(SQL_API_SQLDRIVERCONNECT);
		API3X(SQL_API_SQLENDTRAN);
		API_X(SQL_API_SQLERROR);
		API_X(SQL_API_SQLEXECDIRECT);
		API_X(SQL_API_SQLEXECUTE);
		API__(SQL_API_SQLEXTENDEDFETCH);
		API_X(SQL_API_SQLFETCH);
		API3_(SQL_API_SQLFETCHSCROLL);
		API_X(SQL_API_SQLFOREIGNKEYS);
		API_X(SQL_API_SQLFREECONNECT);
		API_X(SQL_API_SQLFREEENV);
		API3X(SQL_API_SQLFREEHANDLE);
		API_X(SQL_API_SQLFREESTMT);
		API3X(SQL_API_SQLGETCONNECTATTR);
		API_X(SQL_API_SQLGETCONNECTOPTION);
		API__(SQL_API_SQLGETCURSORNAME);
		API_X(SQL_API_SQLGETDATA);
		API3_(SQL_API_SQLGETDESCFIELD);
		API3_(SQL_API_SQLGETDESCREC);
		API3X(SQL_API_SQLGETDIAGFIELD);
		API3X(SQL_API_SQLGETDIAGREC);
		API3X(SQL_API_SQLGETENVATTR);
		API_X(SQL_API_SQLGETFUNCTIONS);
		API_X(SQL_API_SQLGETINFO);
		API3X(SQL_API_SQLGETSTMTATTR);
		API_X(SQL_API_SQLGETSTMTOPTION);
		API_X(SQL_API_SQLGETTYPEINFO);
		API_X(SQL_API_SQLMORERESULTS);
		API_X(SQL_API_SQLNATIVESQL);
		API_X(SQL_API_SQLNUMPARAMS);
		API_X(SQL_API_SQLNUMRESULTCOLS);
		API_X(SQL_API_SQLPARAMDATA);
		API__(SQL_API_SQLPARAMOPTIONS);
		API_X(SQL_API_SQLPREPARE);
		API_X(SQL_API_SQLPRIMARYKEYS);
		API_X(SQL_API_SQLPROCEDURECOLUMNS);
		API_X(SQL_API_SQLPROCEDURES);
		API_X(SQL_API_SQLPUTDATA);
		API_X(SQL_API_SQLROWCOUNT);
		API3X(SQL_API_SQLSETCONNECTATTR);
		API_X(SQL_API_SQLSETCONNECTOPTION);
		API__(SQL_API_SQLSETCURSORNAME);
		API3_(SQL_API_SQLSETDESCFIELD);
		API3_(SQL_API_SQLSETDESCREC);
		API3X(SQL_API_SQLSETENVATTR);
		API_X(SQL_API_SQLSETPARAM);
		API__(SQL_API_SQLSETPOS);
		API__(SQL_API_SQLSETSCROLLOPTIONS);
		API3_(SQL_API_SQLSETSTMTATTR);
		API_X(SQL_API_SQLSETSTMTOPTION);
		API_X(SQL_API_SQLSPECIALCOLUMNS);
		API_X(SQL_API_SQLSTATISTICS);
		API_X(SQL_API_SQLTABLEPRIVILEGES);
		API_X(SQL_API_SQLTABLES);
		API_X(SQL_API_SQLTRANSACT);
#undef API_X
#undef API__
#undef API3X
#undef API3_
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
#define API_X(n) case n:
#define API__(n)
#if (ODBCVER >= 0x300)
#define API3X(n) case n:
#else
#define API3X(n)
#endif
#define API3_(n)
		API_X(SQL_API_SQLALLOCCONNECT);
		API_X(SQL_API_SQLALLOCENV);
		API3X(SQL_API_SQLALLOCHANDLE);
		API_X(SQL_API_SQLALLOCSTMT);
		API_X(SQL_API_SQLBINDCOL);
		API_X(SQL_API_SQLBINDPARAM);
		API_X(SQL_API_SQLBINDPARAMETER);
		API__(SQL_API_SQLBROWSECONNECT);
		API3_(SQL_API_SQLBULKOPERATIONS);
		API_X(SQL_API_SQLCANCEL);
		API3_(SQL_API_SQLCLOSECURSOR);
		API3_(SQL_API_SQLCOLATTRIBUTE);
		API_X(SQL_API_SQLCOLATTRIBUTES);
		API_X(SQL_API_SQLCOLUMNPRIVILEGES);
		API_X(SQL_API_SQLCOLUMNS);
		API_X(SQL_API_SQLCONNECT);
		API3_(SQL_API_SQLCOPYDESC);
		API_X(SQL_API_SQLDESCRIBECOL);
		API__(SQL_API_SQLDESCRIBEPARAM);
		API_X(SQL_API_SQLDISCONNECT);
		API_X(SQL_API_SQLDRIVERCONNECT);
		API3X(SQL_API_SQLENDTRAN);
		API_X(SQL_API_SQLERROR);
		API_X(SQL_API_SQLEXECDIRECT);
		API_X(SQL_API_SQLEXECUTE);
		API__(SQL_API_SQLEXTENDEDFETCH);
		API_X(SQL_API_SQLFETCH);
		API3_(SQL_API_SQLFETCHSCROLL);
		API_X(SQL_API_SQLFOREIGNKEYS);
		API_X(SQL_API_SQLFREECONNECT);
		API_X(SQL_API_SQLFREEENV);
		API3X(SQL_API_SQLFREEHANDLE);
		API_X(SQL_API_SQLFREESTMT);
		API3X(SQL_API_SQLGETCONNECTATTR);
		API_X(SQL_API_SQLGETCONNECTOPTION);
		API__(SQL_API_SQLGETCURSORNAME);
		API_X(SQL_API_SQLGETDATA);
		API3_(SQL_API_SQLGETDESCFIELD);
		API3_(SQL_API_SQLGETDESCREC);
		API3X(SQL_API_SQLGETDIAGFIELD);
		API3X(SQL_API_SQLGETDIAGREC);
		API3X(SQL_API_SQLGETENVATTR);
		API_X(SQL_API_SQLGETFUNCTIONS);
		API_X(SQL_API_SQLGETINFO);
		API3X(SQL_API_SQLGETSTMTATTR);
		API_X(SQL_API_SQLGETSTMTOPTION);
		API_X(SQL_API_SQLGETTYPEINFO);
		API_X(SQL_API_SQLMORERESULTS);
		API_X(SQL_API_SQLNATIVESQL);
		API_X(SQL_API_SQLNUMPARAMS);
		API_X(SQL_API_SQLNUMRESULTCOLS);
		API_X(SQL_API_SQLPARAMDATA);
		API__(SQL_API_SQLPARAMOPTIONS);
		API_X(SQL_API_SQLPREPARE);
		API_X(SQL_API_SQLPRIMARYKEYS);
		API_X(SQL_API_SQLPROCEDURECOLUMNS);
		API_X(SQL_API_SQLPROCEDURES);
		API_X(SQL_API_SQLPUTDATA);
		API_X(SQL_API_SQLROWCOUNT);
		API3X(SQL_API_SQLSETCONNECTATTR);
		API_X(SQL_API_SQLSETCONNECTOPTION);
		API__(SQL_API_SQLSETCURSORNAME);
		API3_(SQL_API_SQLSETDESCFIELD);
		API3_(SQL_API_SQLSETDESCREC);
		API3X(SQL_API_SQLSETENVATTR);
		API_X(SQL_API_SQLSETPARAM);
		API__(SQL_API_SQLSETPOS);
		API__(SQL_API_SQLSETSCROLLOPTIONS);
		API3_(SQL_API_SQLSETSTMTATTR);
		API_X(SQL_API_SQLSETSTMTOPTION);
		API_X(SQL_API_SQLSPECIALCOLUMNS);
		API_X(SQL_API_SQLSTATISTICS);
		API_X(SQL_API_SQLTABLEPRIVILEGES);
		API_X(SQL_API_SQLTABLES);
		API_X(SQL_API_SQLTRANSACT);
#undef API_X
#undef API__
#undef API3X
#undef API3_
		*pfExists = SQL_TRUE;
		ODBC_RETURN(dbc, SQL_SUCCESS);
	default:
		*pfExists = SQL_FALSE;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}


SQLRETURN SQL_API
SQLGetInfo(SQLHDBC hdbc, SQLUSMALLINT fInfoType, SQLPOINTER rgbInfoValue, SQLSMALLINT cbInfoValueMax,
	   SQLSMALLINT FAR * pcbInfoValue)
{
	const char *p = NULL;
	TDSSOCKET *tds;
	int is_ms;
	unsigned int smajor;

	SQLSMALLINT *siInfoValue = (SQLSMALLINT *) rgbInfoValue;
	SQLUSMALLINT *usiInfoValue = (SQLUSMALLINT *) rgbInfoValue;
	SQLUINTEGER *uiInfoValue = (SQLUINTEGER *) rgbInfoValue;

	INIT_HDBC;

	tds = dbc->tds_socket;

	is_ms = TDS_IS_MSSQL(tds);
	smajor = (tds->product_version >> 24) & 0x7F;

	switch (fInfoType) {
	case SQL_ACCESSIBLE_PROCEDURES:
	case SQL_ACCESSIBLE_TABLES:
		p = "Y";
		break;
	case SQL_ACTIVE_CONNECTIONS:
		*uiInfoValue = 0;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_ACTIVE_ENVIRONMENTS:
		*uiInfoValue = 0;
		break;
#endif /* ODBCVER >= 0x0300 */
	case SQL_ACTIVE_STATEMENTS:
		*siInfoValue = 1;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_AGGREGATE_FUNCTIONS:
		*uiInfoValue = SQL_AF_ALL;
		break;
	case SQL_ALTER_DOMAIN:
		*uiInfoValue = 0;
		break;
#endif /* ODBCVER >= 0x0300 */
	case SQL_ALTER_TABLE:
		*uiInfoValue =
			SQL_AT_ADD_COLUMN | SQL_AT_ADD_COLUMN_DEFAULT | SQL_AT_ADD_COLUMN_SINGLE | SQL_AT_ADD_CONSTRAINT |
			SQL_AT_ADD_TABLE_CONSTRAINT | SQL_AT_CONSTRAINT_NAME_DEFINITION | SQL_AT_DROP_COLUMN_RESTRICT;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_ASYNC_MODE:
		/* TODO we hope so in a not-too-far future */
		/* *uiInfoValue = SQL_AM_STATEMENT; */
		*uiInfoValue = SQL_AM_NONE;
		break;
	case SQL_BATCH_ROW_COUNT:
		*uiInfoValue = SQL_BRC_EXPLICIT;
		break;
	case SQL_BATCH_SUPPORT:
		*uiInfoValue = SQL_BS_ROW_COUNT_EXPLICIT | SQL_BS_ROW_COUNT_PROC | SQL_BS_SELECT_EXPLICIT | SQL_BS_SELECT_PROC;
		break;
#endif /* ODBCVER >= 0x0300 */
	case SQL_BOOKMARK_PERSISTENCE:
		/* TODO ??? */
		*uiInfoValue = SQL_BP_DELETE | SQL_BP_SCROLL | SQL_BP_UPDATE;
		break;
	case SQL_CATALOG_LOCATION:
		*siInfoValue = SQL_CL_START;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_CATALOG_NAME:
		p = "Y";
		break;
#endif /* ODBCVER >= 0x0300 */
	case SQL_CATALOG_NAME_SEPARATOR:
		p = ".";
		break;
	case SQL_CATALOG_TERM:
		p = "database";
		break;
	case SQL_CATALOG_USAGE:
		*uiInfoValue = SQL_CU_DML_STATEMENTS | SQL_CU_PROCEDURE_INVOCATION | SQL_CU_TABLE_DEFINITION;
		break;
		/* TODO 
		 * case SQL_COLLATION_SEQ:
		 *      break; */
	case SQL_COLUMN_ALIAS:
		p = "Y";
		break;
	case SQL_CONCAT_NULL_BEHAVIOR:
		/* TODO a bit more complicate for mssql2k.. */
		*siInfoValue = (!is_ms || smajor < 7) ? SQL_CB_NON_NULL : SQL_CB_NULL;
		break;
		/* TODO continue merge here ... freddy77 */
	case SQL_CURSOR_COMMIT_BEHAVIOR:
		/* currently cursors are not supported however sql server close automaticly cursors on commit */
		*usiInfoValue = SQL_CB_CLOSE;
		break;
	case SQL_DATABASE_NAME:
		p = tds_dstr_cstr(&dbc->attr.attr_current_catalog);
		break;
	case SQL_DATA_SOURCE_NAME:
		p = tds_dstr_cstr(&dbc->dsn);
		break;
	case SQL_DATA_SOURCE_READ_ONLY:
		/* TODO: determine the right answer from connection 
		 * attribute SQL_ATTR_ACCESS_MODE */
		*uiInfoValue = 0;	/* false, writable */
		break;
	case SQL_DBMS_NAME:
		p = tds->product_name;
		break;
	case SQL_DBMS_VER:
		if (rgbInfoValue && cbInfoValueMax > 5)
			tds_version(dbc->tds_socket, (char *) rgbInfoValue);
		else
			p = "unknown version";
		break;
	case SQL_DEFAULT_TXN_ISOLATION:
		*uiInfoValue = SQL_TXN_READ_COMMITTED;
		break;
	case SQL_DRIVER_NAME:	/* ODBC 2.0 */
		p = "libtdsodbc.so";
		break;
	case SQL_DRIVER_ODBC_VER:
		p = "03.00";
		break;
	case SQL_DRIVER_VER:
		p = VERSION;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_DYNAMIC_CURSOR_ATTRIBUTES1:
	case SQL_DYNAMIC_CURSOR_ATTRIBUTES2:
		/* Cursors not supported yet */
		*uiInfoValue = 0;
		break;
#endif
	case SQL_FILE_USAGE:
		*uiInfoValue = SQL_FILE_NOT_SUPPORTED;
		break;
#if (ODBCVER >= 0x0300)
	case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1:
	case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2:
		/* Cursors not supported yet */
		*uiInfoValue = 0;
		break;
#endif
	case SQL_IDENTIFIER_QUOTE_CHAR:
		p = "\"";
		break;
	case SQL_KEYSET_CURSOR_ATTRIBUTES1:
	case SQL_KEYSET_CURSOR_ATTRIBUTES2:
		/* Cursors not supported yet */
		*uiInfoValue = 0;
		break;
	case SQL_NEED_LONG_DATA_LEN:
		/* current implementation do not require length, however future will, so is correct to return yes */
		p = "Y";
		break;
	case SQL_QUOTED_IDENTIFIER_CASE:
		/* TODO usually insensitive */
		*usiInfoValue = SQL_IC_MIXED;
		break;
	case SQL_SCHEMA_USAGE:
		*uiInfoValue =
			SQL_OU_DML_STATEMENTS | SQL_OU_INDEX_DEFINITION | SQL_OU_PRIVILEGE_DEFINITION | SQL_OU_PROCEDURE_INVOCATION
			| SQL_OU_TABLE_DEFINITION;
		break;
	case SQL_SCROLL_CONCURRENCY:
		*uiInfoValue = SQL_SCCO_READ_ONLY;
		break;
	case SQL_SCROLL_OPTIONS:
		*uiInfoValue = SQL_SO_FORWARD_ONLY | SQL_SO_STATIC;
		break;
	case SQL_SPECIAL_CHARACTERS:
		/* TODO others ?? */
		p = "\'\"[]{}";
		break;
#if (ODBCVER >= 0x0300)
	case SQL_STATIC_CURSOR_ATTRIBUTES1:
	case SQL_STATIC_CURSOR_ATTRIBUTES2:
		/* Cursors not supported yet */
		*uiInfoValue = 0;
		break;
#endif
	case SQL_TXN_CAPABLE:
		/* transaction for DML and DDL */
		*siInfoValue = SQL_TC_ALL;
		break;
	case SQL_XOPEN_CLI_YEAR:
		/* TODO check specifications */
		p = "1995";
		break;
		/* TODO support for other options */
	default:
		log_unimplemented_type("SQLGetInfo", fInfoType);
		odbc_errs_add(&dbc->errs, 0, "HY092", "Option not supported", NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}

	/* char data */
	if (p)
		ODBC_RETURN(dbc, odbc_set_string(rgbInfoValue, cbInfoValueMax, pcbInfoValue, p, -1));

	ODBC_RETURN(dbc, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLGetStmtOption(SQLHSTMT hstmt, SQLUSMALLINT fOption, SQLPOINTER pvParam)
{
	SQLUINTEGER *piParam = (SQLUINTEGER *) pvParam;

	INIT_HSTMT;

	switch (fOption) {
	case SQL_ROWSET_SIZE:
		*piParam = 1;
		break;
	default:
		tdsdump_log(TDS_DBG_INFO1, "odbc:SQLGetStmtOption: Statement option %d not implemented\n", fOption);
		odbc_errs_add(&stmt->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

static void
odbc_upper_column_names(TDS_STMT * stmt)
{
	TDSRESULTINFO *resinfo;
	TDSCOLINFO *colinfo;
	TDSSOCKET *tds;
	int icol;
	char *p;

	tds = stmt->hdbc->tds_socket;
	if (!tds || !tds->res_info)
		return;

	resinfo = tds->res_info;
	for (icol = 0; icol < resinfo->num_cols; ++icol) {
		colinfo = resinfo->columns[icol];
		/* upper case */
		/* TODO procedure */
		for (p = colinfo->column_name; *p; ++p)
			if ('a' <= *p && *p <= 'z')
				*p = *p & (~0x20);
	}
}

SQLRETURN SQL_API
SQLGetTypeInfo(SQLHSTMT hstmt, SQLSMALLINT fSqlType)
{
	SQLRETURN res;
	TDSSOCKET *tds;
	TDS_INT row_type;
	TDS_INT compute_id;
	int varchar_pos = -1, n;
	static const char sql_templ[] = "EXEC sp_datatype_info %d";
	char sql[sizeof(sql_templ) + 30];

	INIT_HSTMT;

	tds = stmt->hdbc->tds_socket;

	/* For MSSQL6.5 and Sybase 11.9 sp_datatype_info work */
	/* FIXME what about early Sybase products ? */
	/* TODO ODBC3 convert type to ODBC version 2 (date) */
	sprintf(sql, sql_templ, fSqlType);
	if (TDS_IS_MSSQL(tds) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3)
		strcat(sql, ",3");
	if (SQL_SUCCESS != odbc_set_stmt_query(stmt, sql, strlen(sql)))
		ODBC_RETURN(stmt, SQL_ERROR);

      redo:
	res = _SQLExecute(stmt);

	odbc_upper_column_names(stmt);

	if (TDS_IS_MSSQL(stmt->hdbc->tds_socket) || fSqlType != 12 || res != SQL_SUCCESS)
		ODBC_RETURN(stmt, res);

	/* Sybase return first nvarchar for char... and without length !!! */
	/* Some program use first entry so we discard all entry bfore varchar */
	n = 0;
	while (tds->res_info) {
		TDSRESULTINFO *resinfo;
		TDSCOLINFO *colinfo;
		char *name;

		/* if next is varchar leave next for SQLFetch */
		if (n == (varchar_pos - 1))
			break;

		switch (tds_process_row_tokens(stmt->hdbc->tds_socket, &row_type, &compute_id)) {
		case TDS_NO_MORE_ROWS:
			/* discard other tokens */
			tds_process_simple_query(tds);
			if (n >= varchar_pos && varchar_pos > 0)
				goto redo;
			break;
		}
		if (!tds->res_info)
			break;
		++n;

		resinfo = tds->res_info;
		colinfo = resinfo->columns[0];
		name = (char *) (resinfo->current_row + colinfo->column_offset);
		/* skip nvarchar and sysname */
		if (colinfo->column_cur_size == 7 && memcmp("varchar", name, 7) == 0) {
			varchar_pos = n;
		}
	}
	ODBC_RETURN(stmt, res);
}

SQLRETURN SQL_API
SQLParamData(SQLHSTMT hstmt, SQLPOINTER FAR * prgbValue)
{
	struct _sql_param_info *param;

	INIT_HSTMT;

	if (stmt->prepared_query_need_bytes) {
		param = odbc_find_param(stmt, stmt->prepared_query_param_num);
		if (!param)
			ODBC_RETURN(stmt, SQL_ERROR);

		*prgbValue = param->varaddr;
		ODBC_RETURN(stmt, SQL_NEED_DATA);
	}

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLPutData(SQLHSTMT hstmt, SQLPOINTER rgbValue, SQLINTEGER cbValue)
{
	INIT_HSTMT;

	if (stmt->prepared_query && stmt->param_head) {
		SQLRETURN res = continue_parse_prepared_query(stmt, rgbValue, cbValue);

		if (SQL_NEED_DATA == res)
			ODBC_RETURN(stmt, SQL_SUCCESS);
		if (SQL_SUCCESS != res)
			ODBC_RETURN(stmt, res);
	}

	return _SQLExecute(stmt);
}


static SQLRETURN SQL_API
_SQLSetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER ValuePtr, SQLINTEGER StringLength)
{
	SQLULEN u_value = (SQLULEN) ValuePtr;
	int len = 0;
	SQLRETURN ret = SQL_SUCCESS;

	INIT_HDBC;

	switch (Attribute) {
	case SQL_ATTR_AUTOCOMMIT:
		/* spinellia@acm.org */
		ODBC_RETURN(dbc, change_autocommit(dbc, u_value));
		break;
	case SQL_ATTR_CONNECTION_TIMEOUT:
		/* TODO set socket options ??? */
		dbc->attr.attr_connection_timeout = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_ACCESS_MODE:
		dbc->attr.attr_access_mode = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_CURRENT_CATALOG:
		if (!IS_VALID_LEN(StringLength)) {
			odbc_errs_add(&dbc->errs, 0, "HY090", NULL, NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		len = odbc_get_string_size(StringLength, (SQLCHAR *) ValuePtr);
		ret = change_database(dbc, (char *) ValuePtr, len);
		ODBC_RETURN(dbc, ret);
		break;
	case SQL_ATTR_LOGIN_TIMEOUT:
		dbc->attr.attr_login_timeout = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_ODBC_CURSORS:
		dbc->attr.attr_odbc_cursors = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_PACKET_SIZE:
		dbc->attr.attr_packet_size = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_QUIET_MODE:
		dbc->attr.attr_quite_mode = (SQLHWND) u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
#ifdef TDS_NO_DM
	case SQL_ATTR_TRACE:
		dbc->attr.attr_trace = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_TRACEFILE:
		if (!IS_VALID_LEN(StringLength)) {
			odbc_errs_add(&dbc->errs, 0, "HY090", NULL, NULL);
			ODBC_RETURN(dbc, SQL_ERROR);
		}
		len = odbc_get_string_size(StringLength, (SQLCHAR *) ValuePtr);
		tds_dstr_copyn(&dbc->attr.attr_tracefile, (const char *) ValuePtr, len);
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
#endif
	case SQL_ATTR_TXN_ISOLATION:
		dbc->attr.attr_txn_isolation = u_value;
		ODBC_RETURN(dbc, SQL_SUCCESS);
		break;
	case SQL_ATTR_TRANSLATE_LIB:
	case SQL_ATTR_TRANSLATE_OPTION:
		odbc_errs_add(&dbc->errs, 0, "HYC00", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
		break;
	}
	odbc_errs_add(&dbc->errs, 0, "HY092", NULL, NULL);
	ODBC_RETURN(dbc, SQL_ERROR);
}

SQLRETURN SQL_API
SQLSetConnectAttr(SQLHDBC hdbc, SQLINTEGER Attribute, SQLPOINTER ValuePtr, SQLINTEGER StringLength)
{
	return (_SQLSetConnectAttr(hdbc, Attribute, ValuePtr, StringLength));
}

SQLRETURN SQL_API
SQLSetConnectOption(SQLHDBC hdbc, SQLUSMALLINT fOption, SQLUINTEGER vParam)
{
	return (_SQLSetConnectAttr(hdbc, (SQLINTEGER) fOption, (SQLPOINTER) vParam, 0));
}

/* TODO correct code above ??? */
#if 0
SQLRETURN SQL_API
SQLSetConnectOption(SQLHDBC hdbc, SQLUSMALLINT fOption, SQLUINTEGER vParam)
{
	INIT_HDBC;

	switch (fOption) {
	case SQL_AUTOCOMMIT:
		/* spinellia@acm.org */
		return SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) vParam, 0);
	default:
		tdsdump_log(TDS_DBG_INFO1, "odbc:SQLSetConnectOption: Statement option %d not implemented\n", fOption);
		odbc_errs_add(&dbc->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(dbc, SQL_ERROR);
	}
	ODBC_RETURN(dbc, SQL_SUCCESS);
}
#endif

SQLRETURN SQL_API
SQLSetStmtOption(SQLHSTMT hstmt, SQLUSMALLINT fOption, SQLUINTEGER vParam)
{
	INIT_HSTMT;

	switch (fOption) {
	case SQL_ROWSET_SIZE:
		/* Always 1 */
		break;
	case SQL_CURSOR_TYPE:
		if (vParam == SQL_CURSOR_FORWARD_ONLY)
			ODBC_RETURN(stmt, SQL_SUCCESS);
		/* fall through */
	default:
		tdsdump_log(TDS_DBG_INFO1, "odbc:SQLSetStmtOption: Statement option %d not implemented\n", fOption);
		odbc_errs_add(&stmt->errs, 0, "HY092", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	ODBC_RETURN(stmt, SQL_SUCCESS);
}

SQLRETURN SQL_API
SQLSpecialColumns(SQLHSTMT hstmt, SQLUSMALLINT fColType, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName,
		  SQLCHAR FAR * szSchemaName, SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName,
		  SQLUSMALLINT fScope, SQLUSMALLINT fNullable)
{
	int retcode;
	char nullable[2], scope[2], col_type[2];

	INIT_HSTMT;

#ifdef TDS_NO_DM
	/* Check column type */
	if (fColType != SQL_BEST_ROWID && fColType != SQL_ROWVER) {
		odbc_errs_add(&stmt->errs, 0, "HY097", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* check our buffer lengths */
	if (!IS_VALID_LEN(cbCatalogName) || !IS_VALID_LEN(cbSchemaName) || !IS_VALID_LEN(cbTableName)) {
		odbc_errs_add(&stmt->errs, 0, "HY090", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* Check nullable */
	if (fNullable != SQL_NO_NULLS && fNullable != SQL_NULLABLE) {
		odbc_errs_add(&stmt->errs, 0, "HY099", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	if (!odbc_get_string_size(cbTableName, szTableName)) {
		odbc_errs_add(&stmt->errs, 0, "HY009", "SQLSpecialColumns: The table name parameter is required", NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	switch (fScope) {
	case SQL_SCOPE_CURROW:
	case SQL_SCOPE_TRANSACTION:
	case SQL_SCOPE_SESSION:
		break;
	default:
		odbc_errs_add(&stmt->errs, 0, "HY098", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
#endif

	nullable[1] = 0;
	if (fNullable == SQL_NO_NULLS)
		nullable[0] = 'O';
	else
		nullable[0] = 'U';

	scope[1] = 0;
	if (fScope == SQL_SCOPE_CURROW)
		scope[0] = 'C';
	else
		scope[0] = 'T';

	col_type[1] = 0;
	if (fScope == SQL_BEST_ROWID)
		col_type[0] = 'R';
	else
		col_type[0] = 'V';

	retcode =
		odbc_stat_execute(stmt, "sp_special_columns ", TDS_IS_MSSQL(stmt->hdbc->tds_socket) ? 6 : 4,
				  "", szTableName, cbTableName,
				  "@owner", szSchemaName, cbSchemaName,
				  "@qualifier", szCatalogName, cbCatalogName,
				  "@col_type", col_type, 1, "@scope", scope, 1, "@nullable", nullable, 1);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 5, "COLUMN_SIZE");
		odbc_col_setname(stmt, 6, "BUFFER_LENGTH");
		odbc_col_setname(stmt, 7, "DECIMAL_DIGITS");
	}
	ODBC_RETURN(stmt, retcode);
}

SQLRETURN SQL_API
SQLStatistics(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
	      SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName, SQLUSMALLINT fUnique,
	      SQLUSMALLINT fAccuracy)
{
	int retcode;
	char unique[2], accuracy[1];

	INIT_HSTMT;

#ifdef TDS_NO_DM
	/* check our buffer lengths */
	if (!IS_VALID_LEN(cbCatalogName) || !IS_VALID_LEN(cbSchemaName) || !IS_VALID_LEN(cbTableName)) {
		odbc_errs_add(&stmt->errs, 0, "HY090", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* check our uniqueness value */
	if (fUnique != SQL_INDEX_UNIQUE && fUnique != SQL_INDEX_ALL) {
		odbc_errs_add(&stmt->errs, 0, "HY100", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	/* check our accuracy value */
	if (fAccuracy != SQL_QUICK && fAccuracy != SQL_ENSURE) {
		odbc_errs_add(&stmt->errs, 0, "HY101", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}

	if (!odbc_get_string_size(cbTableName, szTableName)) {
		odbc_errs_add(&stmt->errs, 0, "HY009", NULL, NULL);
		ODBC_RETURN(stmt, SQL_ERROR);
	}
#endif

	accuracy[1] = 0;
	if (fAccuracy == SQL_ENSURE)
		accuracy[0] = 'E';
	else
		accuracy[0] = 'Q';

	unique[1] = 0;
	if (fUnique == SQL_INDEX_UNIQUE)
		unique[0] = 'Y';
	else
		unique[0] = 'N';

	retcode =
		odbc_stat_execute(stmt, "sp_statistics ", TDS_IS_MSSQL(stmt->hdbc->tds_socket) ? 5 : 4,
				  "@table_qualifier", szCatalogName, cbCatalogName,
				  "@table_owner", szSchemaName, cbSchemaName,
				  "@table_name", szTableName, cbTableName, "@is_unique", unique, 1, "@accuracy", accuracy, 1);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
		odbc_col_setname(stmt, 8, "ORDINAL_POSITION");
		odbc_col_setname(stmt, 10, "ASC_OR_DESC");
	}
	ODBC_RETURN(stmt, retcode);
}


SQLRETURN SQL_API
SQLTables(SQLHSTMT hstmt, SQLCHAR FAR * szCatalogName, SQLSMALLINT cbCatalogName, SQLCHAR FAR * szSchemaName,
	  SQLSMALLINT cbSchemaName, SQLCHAR FAR * szTableName, SQLSMALLINT cbTableName, SQLCHAR FAR * szTableType,
	  SQLSMALLINT cbTableType)
{
	int retcode;

	INIT_HSTMT;

	retcode =
		odbc_stat_execute(stmt, "sp_tables ", 4,
				  "@table_name", szTableName, cbTableName,
				  "@table_owner", szSchemaName, cbSchemaName,
				  "@table_qualifier", szCatalogName, cbCatalogName, "@table_type", szTableType, cbTableType);
	if (SQL_SUCCEEDED(retcode) && stmt->hdbc->henv->attr.attr_odbc_version == SQL_OV_ODBC3) {
		odbc_col_setname(stmt, 1, "TABLE_CAT");
		odbc_col_setname(stmt, 2, "TABLE_SCHEM");
	}
	ODBC_RETURN(stmt, retcode);
}

/** 
 * Log a useful message about unimplemented options
 * Defying belief, Microsoft defines mutually exclusive options that
 * some ODBC implementations #define as duplicate values (meaning, of course, 
 * that they couldn't be implemented in the same function because they're 
 * indistinguishable.  
 * 
 * Those duplicates are commented out below.
 */
static void
log_unimplemented_type(const char function_name[], int fType)
{
	const char *name, *category;

	switch (fType) {
#ifdef SQL_ALTER_SCHEMA
	case SQL_ALTER_SCHEMA:
		name = "SQL_ALTER_SCHEMA";
		category = "Supported SQL";
		break;
#endif
#ifdef SQL_ANSI_SQL_DATETIME_LITERALS
	case SQL_ANSI_SQL_DATETIME_LITERALS:
		name = "SQL_ANSI_SQL_DATETIME_LITERALS";
		category = "Supported SQL";
		break;
#endif
	case SQL_COLLATION_SEQ:
		name = "SQL_COLLATION_SEQ";
		category = "Data Source Information";
		break;
	case SQL_CONVERT_BIGINT:
		name = "SQL_CONVERT_BIGINT";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_BINARY:
		name = "SQL_CONVERT_BINARY";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_BIT:
		name = "SQL_CONVERT_BIT";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_CHAR:
		name = "SQL_CONVERT_CHAR";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_DATE:
		name = "SQL_CONVERT_DATE";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_DECIMAL:
		name = "SQL_CONVERT_DECIMAL";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_DOUBLE:
		name = "SQL_CONVERT_DOUBLE";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_FLOAT:
		name = "SQL_CONVERT_FLOAT";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_FUNCTIONS:
		name = "SQL_CONVERT_FUNCTIONS";
		category = "Scalar Function Information";
		break;
	case SQL_CONVERT_INTEGER:
		name = "SQL_CONVERT_INTEGER";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_INTERVAL_DAY_TIME:
		name = "SQL_CONVERT_INTERVAL_DAY_TIME";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_INTERVAL_YEAR_MONTH:
		name = "SQL_CONVERT_INTERVAL_YEAR_MONTH";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_LONGVARBINARY:
		name = "SQL_CONVERT_LONGVARBINARY";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_LONGVARCHAR:
		name = "SQL_CONVERT_LONGVARCHAR";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_NUMERIC:
		name = "SQL_CONVERT_NUMERIC";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_REAL:
		name = "SQL_CONVERT_REAL";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_SMALLINT:
		name = "SQL_CONVERT_SMALLINT";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_TIME:
		name = "SQL_CONVERT_TIME";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_TIMESTAMP:
		name = "SQL_CONVERT_TIMESTAMP";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_TINYINT:
		name = "SQL_CONVERT_TINYINT";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_VARBINARY:
		name = "SQL_CONVERT_VARBINARY";
		category = "Conversion Information";
		break;
	case SQL_CONVERT_VARCHAR:
		name = "SQL_CONVERT_VARCHAR";
		category = "Conversion Information";
		break;
	case SQL_CORRELATION_NAME:
		name = "SQL_CORRELATION_NAME";
		category = "Supported SQL";
		break;
	case SQL_CREATE_ASSERTION:
		name = "SQL_CREATE_ASSERTION";
		category = "Supported SQL";
		break;
	case SQL_CREATE_CHARACTER_SET:
		name = "SQL_CREATE_CHARACTER_SET";
		category = "Supported SQL";
		break;
	case SQL_CREATE_COLLATION:
		name = "SQL_CREATE_COLLATION";
		category = "Supported SQL";
		break;
	case SQL_CREATE_DOMAIN:
		name = "SQL_CREATE_DOMAIN";
		category = "Supported SQL";
		break;
	case SQL_CREATE_SCHEMA:
		name = "SQL_CREATE_SCHEMA";
		category = "Supported SQL";
		break;
	case SQL_CREATE_TABLE:
		name = "SQL_CREATE_TABLE";
		category = "Supported SQL";
		break;
	case SQL_CREATE_TRANSLATION:
		name = "SQL_CREATE_TRANSLATION";
		category = "Supported SQL";
		break;
	case SQL_CURSOR_ROLLBACK_BEHAVIOR:
		name = "SQL_CURSOR_ROLLBACK_BEHAVIOR";
		category = "Data Source Information";
		break;
	case SQL_CURSOR_SENSITIVITY:
		name = "SQL_CURSOR_SENSITIVITY";
		category = "Data Source Information";
		break;
	case SQL_DDL_INDEX:
		name = "SQL_DDL_INDEX";
		category = "Supported SQL";
		break;
	case SQL_DESCRIBE_PARAMETER:
		name = "SQL_DESCRIBE_PARAMETER";
		category = "Data Source Information";
		break;
	case SQL_DM_VER:
		name = "SQL_DM_VER";
		category = "Added for ODBC 3.x";
		break;
	case SQL_DRIVER_HDBC:
		name = "SQL_DRIVER_HDBC";
		category = "Driver Information";
		break;
	case SQL_DRIVER_HDESC:
		name = "SQL_DRIVER_HDESC";
		category = "Driver Information";
		break;
	case SQL_DRIVER_HENV:
		name = "SQL_DRIVER_HENV";
		category = "Driver Information";
		break;
	case SQL_DRIVER_HLIB:
		name = "SQL_DRIVER_HLIB";
		category = "Driver Information";
		break;
	case SQL_DRIVER_HSTMT:
		name = "SQL_DRIVER_HSTMT";
		category = "Driver Information";
		break;
	case SQL_DROP_ASSERTION:
		name = "SQL_DROP_ASSERTION";
		category = "Supported SQL";
		break;
	case SQL_DROP_CHARACTER_SET:
		name = "SQL_DROP_CHARACTER_SET";
		category = "Supported SQL";
		break;
	case SQL_DROP_COLLATION:
		name = "SQL_DROP_COLLATION";
		category = "Supported SQL";
		break;
	case SQL_DROP_DOMAIN:
		name = "SQL_DROP_DOMAIN";
		category = "Supported SQL";
		break;
	case SQL_DROP_SCHEMA:
		name = "SQL_DROP_SCHEMA";
		category = "Supported SQL";
		break;
	case SQL_DROP_TABLE:
		name = "SQL_DROP_TABLE";
		category = "Supported SQL";
		break;
	case SQL_DROP_TRANSLATION:
		name = "SQL_DROP_TRANSLATION";
		category = "Supported SQL";
		break;
	case SQL_DROP_VIEW:
		name = "SQL_DROP_VIEW";
		category = "Supported SQL";
		break;
	case SQL_EXPRESSIONS_IN_ORDERBY:
		name = "SQL_EXPRESSIONS_IN_ORDERBY";
		category = "Supported SQL";
		break;
	case SQL_FETCH_DIRECTION:
		name = "SQL_FETCH_DIRECTION";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_GETDATA_EXTENSIONS:
		name = "SQL_GETDATA_EXTENSIONS";
		category = "Driver Information";
		break;
	case SQL_GROUP_BY:
		name = "SQL_GROUP_BY";
		category = "Supported SQL";
		break;
	case SQL_IDENTIFIER_CASE:
		name = "SQL_IDENTIFIER_CASE";
		category = "Supported SQL";
		break;
	case SQL_INDEX_KEYWORDS:
		name = "SQL_INDEX_KEYWORDS";
		category = "Supported SQL";
		break;
	case SQL_INFO_SCHEMA_VIEWS:
		name = "SQL_INFO_SCHEMA_VIEWS";
		category = "Driver Information";
		break;
	case SQL_INSERT_STATEMENT:
		name = "SQL_INSERT_STATEMENT";
		category = "Supported SQL";
		break;
	case SQL_KEYWORDS:
		name = "SQL_KEYWORDS";
		category = "Supported SQL";
		break;
	case SQL_LIKE_ESCAPE_CLAUSE:
		name = "SQL_LIKE_ESCAPE_CLAUSE";
		category = "Supported SQL";
		break;
	case SQL_LOCK_TYPES:
		name = "SQL_LOCK_TYPES";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_MAX_ASYNC_CONCURRENT_STATEMENTS:
		name = "SQL_MAX_ASYNC_CONCURRENT_STATEMENTS";
		category = "Driver Information";
		break;
	case SQL_MAX_BINARY_LITERAL_LEN:
		name = "SQL_MAX_BINARY_LITERAL_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_CHAR_LITERAL_LEN:
		name = "SQL_MAX_CHAR_LITERAL_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMNS_IN_GROUP_BY:
		name = "SQL_MAX_COLUMNS_IN_GROUP_BY";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMNS_IN_INDEX:
		name = "SQL_MAX_COLUMNS_IN_INDEX";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMNS_IN_ORDER_BY:
		name = "SQL_MAX_COLUMNS_IN_ORDER_BY";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMNS_IN_SELECT:
		name = "SQL_MAX_COLUMNS_IN_SELECT";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMNS_IN_TABLE:
		name = "SQL_MAX_COLUMNS_IN_TABLE";
		category = "SQL Limits";
		break;
	case SQL_MAX_COLUMN_NAME_LEN:
		name = "SQL_MAX_COLUMN_NAME_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_CURSOR_NAME_LEN:
		name = "SQL_MAX_CURSOR_NAME_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_IDENTIFIER_LEN:
		name = "SQL_MAX_IDENTIFIER_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_INDEX_SIZE:
		name = "SQL_MAX_INDEX_SIZE";
		category = "SQL Limits";
		break;
	case SQL_MAX_OWNER_NAME_LEN:
		name = "SQL_MAX_SCHEMA_NAME_LEN/SQL_MAX_OWNER_NAME_LEN";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_MAX_PROCEDURE_NAME_LEN:
		name = "SQL_MAX_PROCEDURE_NAME_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_QUALIFIER_NAME_LEN:
		name = "SQL_MAX_CATALOG_NAME_LEN/SQL_MAX_QUALIFIER_NAME_LEN";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_MAX_ROW_SIZE:
		name = "SQL_MAX_ROW_SIZE";
		category = "SQL Limits";
		break;
	case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
		name = "SQL_MAX_ROW_SIZE_INCLUDES_LONG";
		category = "SQL Limits";
		break;
	case SQL_MAX_STATEMENT_LEN:
		name = "SQL_MAX_STATEMENT_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_TABLES_IN_SELECT:
		name = "SQL_MAX_TABLES_IN_SELECT";
		category = "SQL Limits";
		break;
	case SQL_MAX_TABLE_NAME_LEN:
		name = "SQL_MAX_TABLE_NAME_LEN";
		category = "SQL Limits";
		break;
	case SQL_MAX_USER_NAME_LEN:
		name = "SQL_MAX_USER_NAME_LEN";
		category = "SQL Limits";
		break;
	case SQL_MULTIPLE_ACTIVE_TXN:
		name = "SQL_MULTIPLE_ACTIVE_TXN";
		category = "Data Source Information";
		break;
	case SQL_MULT_RESULT_SETS:
		name = "SQL_MULT_RESULT_SETS";
		category = "Data Source Information";
		break;
	case SQL_NON_NULLABLE_COLUMNS:
		name = "SQL_NON_NULLABLE_COLUMNS";
		category = "Supported SQL";
		break;
	case SQL_NULL_COLLATION:
		name = "SQL_NULL_COLLATION";
		category = "Data Source Information";
		break;
	case SQL_NUMERIC_FUNCTIONS:
		name = "SQL_NUMERIC_FUNCTIONS";
		category = "Scalar Function Information";
		break;
	case SQL_ODBC_API_CONFORMANCE:
		name = "SQL_ODBC_API_CONFORMANCE";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_ODBC_INTERFACE_CONFORMANCE:
		name = "SQL_ODBC_INTERFACE_CONFORMANCE";
		category = "Driver Information";
		break;
	case SQL_ODBC_SQL_CONFORMANCE:
		name = "SQL_ODBC_SQL_CONFORMANCE";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_ODBC_SQL_OPT_IEF:
		name = "SQL_INTEGRITY/SQL_ODBC_SQL_OPT_IEF";
		category = "Renamed for ODBC 3.x";
		break;
#ifdef SQL_ODBC_STANDARD_CLI_CONFORMANCE
	case SQL_ODBC_STANDARD_CLI_CONFORMANCE:
		name = "SQL_ODBC_STANDARD_CLI_CONFORMANCE";
		category = "Driver Information";
		break;
#endif
	case SQL_ODBC_VER:
		name = "SQL_ODBC_VER";
		category = "Driver Information";
		break;
	case SQL_OJ_CAPABILITIES:
		name = "SQL_OJ_CAPABILITIES";
		category = "Supported SQL";
		break;
	case SQL_ORDER_BY_COLUMNS_IN_SELECT:
		name = "SQL_ORDER_BY_COLUMNS_IN_SELECT";
		category = "Supported SQL";
		break;
	case SQL_OUTER_JOINS:
		name = "SQL_OUTER_JOINS";
		category = "Supported SQL";
		break;
	case SQL_OWNER_TERM:
		name = "SQL_SCHEMA_TERM/SQL_OWNER_TERM";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_PARAM_ARRAY_ROW_COUNTS:
		name = "SQL_PARAM_ARRAY_ROW_COUNTS";
		category = "Driver Information";
		break;
	case SQL_PARAM_ARRAY_SELECTS:
		name = "SQL_PARAM_ARRAY_SELECTS";
		category = "Driver Information";
		break;
	case SQL_POSITIONED_STATEMENTS:
		name = "SQL_POSITIONED_STATEMENTS";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_POS_OPERATIONS:
		name = "SQL_POS_OPERATIONS";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_PROCEDURES:
		name = "SQL_PROCEDURES";
		category = "Supported SQL";
		break;
	case SQL_PROCEDURE_TERM:
		name = "SQL_PROCEDURE_TERM";
		category = "Data Source Information";
		break;
	case SQL_QUALIFIER_LOCATION:
		name = "SQL_CATALOG_LOCATION/SQL_QUALIFIER_LOCATION";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_QUALIFIER_NAME_SEPARATOR:
		name = "SQL_CATALOG_NAME_SEPARATOR/SQL_QUALIFIER_NAME_SEPARATOR";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_QUALIFIER_TERM:
		name = "SQL_CATALOG_TERM/SQL_QUALIFIER_TERM";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_QUALIFIER_USAGE:
		name = "SQL_CATALOG_USAGE/SQL_QUALIFIER_USAGE";
		category = "Renamed for ODBC 3.x";
		break;
	case SQL_ROW_UPDATES:
		name = "SQL_ROW_UPDATES";
		category = "Driver Information";
		break;
	case SQL_SEARCH_PATTERN_ESCAPE:
		name = "SQL_SEARCH_PATTERN_ESCAPE";
		category = "Driver Information";
		break;
	case SQL_SERVER_NAME:
		name = "SQL_SERVER_NAME";
		category = "Driver Information";
		break;
	case SQL_SQL_CONFORMANCE:
		name = "SQL_SQL_CONFORMANCE";
		category = "Supported SQL";
		break;
	case SQL_STATIC_SENSITIVITY:
		name = "SQL_STATIC_SENSITIVITY";
		category = "Deprecated in ODBC 3.x";
		break;
	case SQL_STRING_FUNCTIONS:
		name = "SQL_STRING_FUNCTIONS";
		category = "Scalar Function Information";
		break;
	case SQL_SUBQUERIES:
		name = "SQL_SUBQUERIES";
		category = "Supported SQL";
		break;
	case SQL_SYSTEM_FUNCTIONS:
		name = "SQL_SYSTEM_FUNCTIONS";
		category = "Scalar Function Information";
		break;
	case SQL_TABLE_TERM:
		name = "SQL_TABLE_TERM";
		category = "Data Source Information";
		break;
	case SQL_TIMEDATE_ADD_INTERVALS:
		name = "SQL_TIMEDATE_ADD_INTERVALS";
		category = "Scalar Function Information";
		break;
	case SQL_TIMEDATE_DIFF_INTERVALS:
		name = "SQL_TIMEDATE_DIFF_INTERVALS";
		category = "Scalar Function Information";
		break;
	case SQL_TIMEDATE_FUNCTIONS:
		name = "SQL_TIMEDATE_FUNCTIONS";
		category = "Scalar Function Information";
		break;
	case SQL_TXN_ISOLATION_OPTION:
		name = "SQL_TXN_ISOLATION_OPTION";
		category = "Data Source Information";
		break;
	case SQL_UNION:
		name = "SQL_UNION";
		category = "Supported SQL";
		break;
	case SQL_USER_NAME:
		name = "SQL_USER_NAME";
		category = "Data Source Information";
		break;
	default:
		name = "unknown";
		category = "unknown";
		break;
	}

	tdsdump_log(TDS_DBG_INFO1, "odbc: not implemented: %s: option/type %d(%s) [category %s]\n", function_name, fType, name,
		    category);

	return;
}

#define ODBC_MAX_STAT_PARAM 8

static SQLRETURN
odbc_stat_execute(TDS_STMT * stmt, const char *begin, int nparams, ...)
{
	struct param
	{
		char *name;
		SQLCHAR *value;
		int len;
	}
	params[ODBC_MAX_STAT_PARAM];
	int i, len;
	char *proc, *p;
	int retcode;
	va_list marker;


	assert(nparams < ODBC_MAX_STAT_PARAM);

	/* read all params and calc len required */
	va_start(marker, nparams);
	len = strlen(begin) + 2;
	for (i = 0; i < nparams; ++i) {
		params[i].name = va_arg(marker, char *);

		params[i].value = va_arg(marker, SQLCHAR *);
		params[i].len = odbc_get_string_size(va_arg(marker, int), params[i].value);

		len += strlen(params[i].name) + tds_quote_string(stmt->hdbc->tds_socket, NULL, params[i].value, params[i].len) + 3;
	}
	va_end(marker);

	/* allocate space for string */
	if (!(proc = (char *) malloc(len))) {
		odbc_errs_add(&stmt->errs, 0, "HY001", NULL, NULL);
		return SQL_ERROR;
	}

	/* build string */
	p = proc;
	strcpy(p, begin);
	p += strlen(begin);
	for (i = 0; i < nparams; ++i) {
		if (params[i].len <= 0)
			continue;
		if (params[i].name[0]) {
			strcpy(p, params[i].name);
			p += strlen(params[i].name);
			*p++ = '=';
		}
		p += tds_quote_string(stmt->hdbc->tds_socket, p, params[i].value, params[i].len);
		*p++ = ',';
	}
	*--p = '\0';

	/* set it */
	retcode = odbc_set_stmt_query(stmt, proc, p - proc);
	free(proc);

	if (retcode != SQL_SUCCESS)
		return retcode;

	/* execute it */
	retcode = _SQLExecute(stmt);
	if (SQL_SUCCEEDED(retcode))
		odbc_upper_column_names(stmt);

	return retcode;
}

static SQLRETURN
odbc_free_dynamic(TDS_STMT * stmt)
{
	TDSSOCKET *tds = stmt->hdbc->tds_socket;

	if (stmt->dyn) {
		if (tds_submit_unprepare(tds, stmt->dyn) == TDS_SUCCEED) {
			if (tds_process_simple_query(tds) != TDS_SUCCEED)
				ODBC_RETURN(stmt, SQL_ERROR);
			tds_free_dynamic(tds, stmt->dyn);
			stmt->dyn = NULL;
		} else {
			/* TODO if fail add to odbc to free later, when we are in idle */
			ODBC_RETURN(stmt, SQL_ERROR);
		}
	}
	return SQL_SUCCESS;
}
