#include "common.h"


static char software_version[] = "$Id: date.c,v 1.7 2004/04/15 19:27:33 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static void
DoTest(int n)
{
	int res;

	SQLCHAR output[256];

	SQLSMALLINT colType;
	SQLUINTEGER colSize;
	SQLSMALLINT colScale, colNullable;
	SQLINTEGER dataSize;

	TIMESTAMP_STRUCT ts;

	if (CommandWithResult(Statement, "select convert(datetime, '2002-12-27 18:43:21')") != SQL_SUCCESS) {
		printf("Unable to execute statement\n");
		CheckReturn();
		exit(1);
	}

	res = SQLFetch(Statement);
	if (res != SQL_SUCCESS && res != SQL_SUCCESS_WITH_INFO) {
		printf("Unable to fetch row\n");
		CheckReturn();
		exit(1);
	}

	if (SQLDescribeCol(Statement, 1, output, sizeof(output), NULL, &colType, &colSize, &colScale, &colNullable) != SQL_SUCCESS) {
		printf("Error getting data\n");
		CheckReturn();
		exit(1);
	}

	if (n == 0) {
		memset(&ts, 0, sizeof(ts));
		if (SQLGetData(Statement, 1, SQL_C_TIMESTAMP, &ts, sizeof(ts), &dataSize) != SQL_SUCCESS) {
			printf("Unable to get data col %d\n", 1);
			CheckReturn();
			exit(1);
		}
		sprintf((char *) output, "%04d-%02d-%02d %02d:%02d:%02d.000", ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
	} else {
		if (SQLGetData(Statement, 1, SQL_C_CHAR, output, sizeof(output), &dataSize) != SQL_SUCCESS) {
			printf("Unable to get data col %d\n", 1);
			CheckReturn();
			exit(1);
		}
	}

	printf("Date returned: %s\n", output);
	if (strcmp((char *) output, "2002-12-27 18:43:21.000") != 0) {
		printf("Invalid returned date\n");
		exit(1);
	}

	res = SQLFetch(Statement);
	if (res != SQL_NO_DATA) {
		printf("Unable to fetch row\n");
		CheckReturn();
		exit(1);
	}

	res = SQLCloseCursor(Statement);
	if (!SQL_SUCCEEDED(res)) {
		printf("Unable to close cursr\n");
		CheckReturn();
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	Connect();

	DoTest(0);
	DoTest(1);

	Disconnect();

	printf("Done.\n");
	return 0;
}
