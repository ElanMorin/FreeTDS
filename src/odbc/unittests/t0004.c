#include "common.h"

/* Test for SQLMoreResults */

static char software_version[] = "$Id: t0004.c,v 1.12 2003/05/20 10:48:27 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static int
Test(int use_indicator)
{
	char buf[128];
	SQLINTEGER ind;
	SQLINTEGER pind = use_indicator ? &ind : NULL;

	strcpy(buf, "I don't exist");
	ind = strlen(buf);

	if (SQLBindParameter(Statement, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 20, 0, buf, 128, pind) != SQL_SUCCESS) {
		printf("Unable to bind parameter\n");
		exit(1);
	}

	if (SQLPrepare(Statement, (SQLCHAR *) "SELECT id, name FROM sysobjects WHERE name = ?", SQL_NTS) != SQL_SUCCESS) {
		printf("Unable to prepare statement\n");
		exit(1);
	}

	if (SQLExecute(Statement) != SQL_SUCCESS) {
		printf("Unable to execute statement\n");
		exit(1);
	}

	if (SQLFetch(Statement) != SQL_NO_DATA) {
		printf("Data not expected\n");
		exit(1);
	}

	if (SQLMoreResults(Statement) != SQL_NO_DATA) {
		printf("Not expected another recordset\n");
		exit(1);
	}

	/* use same binding above */
	strcpy(buf, "sysobjects");
	ind = strlen(buf);

	if (SQLExecute(Statement) != SQL_SUCCESS) {
		printf("Unable to execute statement\n");
		exit(1);
	}

	if (SQLFetch(Statement) != SQL_SUCCESS) {
		printf("Data expected\n");
		exit(1);
	}

	if (SQLFetch(Statement) != SQL_NO_DATA) {
		printf("Data not expected\n");
		exit(1);
	}

	if (SQLMoreResults(Statement) != SQL_NO_DATA) {
		printf("Not expected another recordset\n");
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	Connect();

	Test(1);
	Test(0);

	Disconnect();

	printf("Done.\n");
	return 0;
}
