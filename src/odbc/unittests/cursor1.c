#include "common.h"

/* Test cursors */

static char software_version[] = "$Id: cursor1.c,v 1.2 2006/02/06 15:45:32 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

#define CHK(func,params) \
	if (func params != SQL_SUCCESS) \
		ODBC_REPORT_ERROR(#func)

#define CHK_INFO(func,params) \
	if (!SQL_SUCCEEDED(func params)) \
		ODBC_REPORT_ERROR(#func)

#define SWAP_STMT(a,b) do { SQLHSTMT xyz = a; a = b; b = xyz; } while(0)

static void
CheckNoRow(const char *query)
{
	SQLRETURN retcode;

	retcode = SQLExecDirect(Statement, (SQLCHAR *) query, SQL_NTS);
	if (retcode == SQL_NO_DATA)
		return;

	if (!SQL_SUCCEEDED(retcode))
		ODBC_REPORT_ERROR("SQLExecDirect");
	
	do {
		SQLSMALLINT cols;

		retcode = SQLNumResultCols(Statement, &cols);
		if (retcode != SQL_SUCCESS || cols != 0) {
			fprintf(stderr, "Data not expected here, query:\n\t%s\n", query);
			exit(1);
		}
	} while ((retcode=SQLMoreResults(Statement)) == SQL_SUCCESS);
	if (retcode != SQL_NO_DATA)
		ODBC_REPORT_ERROR("SQLMoreResults");
}

static void
Test(int use_sql)
{
#define ROWS 4
#define C_LEN 10

	SQLUINTEGER n[ROWS];
	SQLCHAR c[ROWS][C_LEN];
	SQLINTEGER c_len[ROWS], n_len[ROWS];

	SQLUSMALLINT statuses[ROWS];
	SQLUSMALLINT i;
	SQLULEN num_row;
	SQLHSTMT stmt2;
	SQLRETURN retcode;

	/* create test table */
	Command(Statement, "IF OBJECT_ID('tempdb..#test') IS NOT NULL DROP TABLE #test");
	Command(Statement, "CREATE TABLE #test(i int, c varchar(6))");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(1, 'a')");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(2, 'bb')");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(3, 'ccc')");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(4, 'dddd')");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(5, 'eeeee')");
	Command(Statement, "INSERT INTO #test(i, c) VALUES(6, 'ffffff')");

	/* set cursor options */
	ResetStatement();
	CHK(SQLSetStmtAttr, (Statement, SQL_ATTR_CONCURRENCY, (SQLPOINTER) SQL_CONCUR_ROWVER, 0));
	CHK(SQLSetStmtAttr, (Statement, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER) SQL_CURSOR_DYNAMIC, 0));
	CHK(SQLSetStmtAttr, (Statement, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER) ROWS, 0));
	CHK(SQLSetStmtAttr, (Statement, SQL_ATTR_ROW_STATUS_PTR, (SQLPOINTER) statuses, 0));
	CHK(SQLSetStmtAttr, (Statement, SQL_ATTR_ROWS_FETCHED_PTR, &num_row, 0));
	CHK(SQLSetCursorName, (Statement, "C1", SQL_NTS));

	/* */
	CHK(SQLExecDirect, (Statement, "SELECT i, c FROM #test", SQL_NTS));

	/* bind some rows at a time */
	CHK(SQLBindCol, (Statement, 1, SQL_C_ULONG, n, 0, n_len));
	CHK(SQLBindCol, (Statement, 2, SQL_C_CHAR, c, C_LEN, c_len));

	/* allocate an additional statement */
	CHK(SQLAllocStmt, (Connection, &stmt2));

	while ((retcode = SQLFetchScroll(Statement, SQL_FETCH_NEXT, 0)) != SQL_ERROR) {
		if (retcode == SQL_NO_DATA)
			break;

		/* print, just for debug */
		for (i = 0; i < num_row; ++i)
			printf("row %d i %d c %s\n", (int) (i + 1), (int) n[i], c[i]);
		printf("---\n");

		/* delete a row */
		i = 1;
		if (i > 0 && i <= num_row) {
			CHK(SQLSetPos, (Statement, i, use_sql ? SQL_POSITION : SQL_DELETE, SQL_LOCK_NO_CHANGE));
			if (use_sql) {
				SWAP_STMT(Statement, stmt2);
				CHK(SQLPrepare, (Statement, "DELETE FROM #test WHERE CURRENT OF C1", SQL_NTS));
				CHK(SQLExecute, (Statement));
				SWAP_STMT(Statement, stmt2);
			}
		}

		/* update another row */
		i = 2;
		if (i > 0 && i <= num_row) {
			strcpy(c[i - 1], "foo");
			c_len[i - 1] = 3;
			CHK(SQLSetPos, (Statement, i, use_sql ? SQL_POSITION : SQL_UPDATE, SQL_LOCK_NO_CHANGE));
			if (use_sql) {
				SWAP_STMT(Statement, stmt2);
				CHK(SQLPrepare, (Statement, "UPDATE #test SET c=? WHERE CURRENT OF C1", SQL_NTS));
				CHK(SQLBindParameter,
				    (Statement, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, C_LEN, 0, c[i - 1], 0, NULL));
				CHK(SQLExecute, (Statement));
				/* FIXME this is not necessary for mssql driver */
				SQLMoreResults(Statement);
				SWAP_STMT(Statement, stmt2);
			}
		}
	}

	if (retcode == SQL_ERROR)
		ODBC_REPORT_ERROR("SQLFetchScroll");

	CHK(SQLFreeStmt, (stmt2, SQL_DROP));

	ResetStatement();

	/* test values */
	CheckNoRow("IF (SELECT COUNT(*) FROM #test) <> 4 SELECT 1");
	CheckNoRow("IF NOT EXISTS(SELECT * FROM #test WHERE i = 2 AND c = 'foo') SELECT 1");
	CheckNoRow("IF NOT EXISTS(SELECT * FROM #test WHERE i = 3 AND c = 'ccc') SELECT 1");
	CheckNoRow("IF NOT EXISTS(SELECT * FROM #test WHERE i = 4 AND c = 'dddd') SELECT 1");
	CheckNoRow("IF NOT EXISTS(SELECT * FROM #test WHERE i = 6 AND c = 'foo') SELECT 1");
}

int
main(int argc, char *argv[])
{
#ifndef ENABLE_DEVELOPING
	return 0;
#endif

	Connect();

	Test(1);

	Test(0);

	Disconnect();
	return 0;
}
