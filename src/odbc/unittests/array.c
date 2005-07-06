#include "common.h"
#include <assert.h>

/* Test using array binding */

static char software_version[] = "$Id: array.c,v 1.7 2005/07/06 10:16:39 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static const char *test_query = NULL;

static void
ResetStatement(void)
{
	SQLFreeStmt(Statement, SQL_DROP);
	Statement = SQL_NULL_HSTMT;
	if (SQLAllocStmt(Connection, &Statement) != SQL_SUCCESS)
		ODBC_REPORT_ERROR("Unable to allocate statement");
}

static void *
xmalloc(size_t s)
{
	void *p = malloc(s);
	if (!p) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return p;
}

#define XMALLOC_N(t, n) (t*) xmalloc(n*sizeof(t))

static void
query_test(int prepare, SQLRETURN expected, const char *expected_status)
{
#define DESC_LEN 51
#define ARRAY_SIZE 10

	SQLUINTEGER *ids = XMALLOC_N(SQLUINTEGER,ARRAY_SIZE);
	typedef SQLCHAR desc_t[DESC_LEN];
	desc_t *descs = XMALLOC_N(desc_t, ARRAY_SIZE);
	SQLINTEGER *id_lens = XMALLOC_N(SQLINTEGER,ARRAY_SIZE), *desc_lens = XMALLOC_N(SQLINTEGER,ARRAY_SIZE);
	SQLUSMALLINT i, processed, *statuses = XMALLOC_N(SQLUSMALLINT,ARRAY_SIZE);
	RETCODE ret;
	char status[20];

	assert(Statement != SQL_NULL_HSTMT);
	ResetStatement();

	CommandWithResult(Statement, "drop table #tmp1");
	Command(Statement, "create table #tmp1 (id tinyint, value char(20))");

	SQLSetStmtAttr(Statement, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0);
	SQLSetStmtAttr(Statement, SQL_ATTR_PARAMSET_SIZE, (void *) ARRAY_SIZE, 0);
	SQLSetStmtAttr(Statement, SQL_ATTR_PARAM_STATUS_PTR, statuses, 0);
	SQLSetStmtAttr(Statement, SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0);
	SQLBindParameter(Statement, 1, SQL_PARAM_INPUT, SQL_C_ULONG, SQL_INTEGER, 5, 0, ids, 0, id_lens);
	SQLBindParameter(Statement, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, DESC_LEN - 1, 0, descs, DESC_LEN, desc_lens);

	processed = ARRAY_SIZE + 1;
	for (i = 0; i < ARRAY_SIZE; i++) {
		statuses[i] = SQL_PARAM_DIAG_UNAVAILABLE;
		ids[i] = i * 132;
		sprintf((char *) descs[i], "data %d", i * 7);
		id_lens[i] = 0;
		desc_lens[i] = SQL_NTS;
	}

	if (!prepare) {
		ret = SQLExecDirect(Statement, (SQLCHAR *) test_query, SQL_NTS);
	} else {
		SQLPrepare(Statement, (SQLCHAR *) test_query, SQL_NTS);
		ret = SQLExecute(Statement);
	}
	if (ret != expected) {
		char buf[256];

		sprintf(buf, "Invalid result: got %d exptected %d processed %d", ret, expected, processed);
		ODBC_REPORT_ERROR(buf);
	}

	for (i = 0; i < ARRAY_SIZE; i++)
		SQLMoreResults(Statement);

	if (processed > ARRAY_SIZE) {
		char buf[256];

		sprintf(buf, "Invalid processed number: %d", processed);
		ODBC_REPORT_ERROR(buf);
	}

	for (i = 0; i < processed; ++i) {
		switch (statuses[i]) {
		case SQL_PARAM_SUCCESS:
		case SQL_PARAM_SUCCESS_WITH_INFO:
			status[i] = 'V';
			break;

		case SQL_PARAM_ERROR:
			status[i] = '!';
			break;

		case SQL_PARAM_UNUSED:
			status[i] = ' ';
			break;

		case SQL_PARAM_DIAG_UNAVAILABLE:
			status[i] = '?';
			break;
		default:
			fprintf(stderr, "Invalid status returned\n");
			exit(1);
		}
	}
	status[i] = 0;

	if (expected_status && strcmp(expected_status, status) != 0) {
		fprintf(stderr, "Invalid status\n\tgot      '%s'\n\texpected '%s'\n", status, expected_status);
		exit(1);
	}

	free(ids);
	free(descs);
	free(id_lens);
	free(desc_lens);
	free(statuses);
}

int
main(int argc, char *argv[])
{
	use_odbc_version3 = 1;
	Connect();

	test_query = "INSERT INTO #tmp1 (id, value) VALUES (?, ?)";
	query_test(0, SQL_ERROR, "VV!!!!!!!!");
	/* FIXME test why is different and what should be correct result */
	query_test(1, driver_is_freetds() ? SQL_ERROR : SQL_SUCCESS_WITH_INFO, "VV!!!!!!!!");

	test_query = "INSERT INTO #tmp1 (id) VALUES (?) UPDATE #tmp1 SET value = ?";
	query_test(0, SQL_SUCCESS_WITH_INFO, "VVVV!V!V!V");
	/* FIXME test why is different and what should be correct result */
	query_test(1, driver_is_freetds() ? SQL_ERROR : SQL_SUCCESS_WITH_INFO, "VV!!!!!!!!");

	/* with result, see how SQLMoreResult work */
	test_query = "INSERT INTO #tmp1 (id) VALUES (?) SELECT * FROM #tmp1 UPDATE #tmp1 SET value = ?";
	/* IMHO our driver is better here -- freddy77 */
	query_test(0, SQL_SUCCESS, driver_is_freetds() ? "VVVVV!V!V!" : "VVVVVV!VVV");
	query_test(1, SQL_SUCCESS, "VVVVVVVVVV");

	/* TODO record binding, array fetch, sqlputdata */

	Disconnect();

	printf("Success!.\n");
	return 0;
}

