#include "common.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <ctype.h>

static char software_version[] = "$Id: common.c,v 1.32 2004/05/02 07:30:40 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

HENV Environment;
HDBC Connection;
HSTMT Statement;
int use_odbc_version3 = 0;

char USER[512];
char SERVER[512];
char PASSWORD[512];
char DATABASE[512];
char DRIVER[1024];

static int
check_lib(char *path, const char *file)
{
	int len = strlen(path);
	FILE *f;

	strcat(path, file);
	f = fopen(path, "rb");
	if (f) {
		fclose(f);
		return 1;
	}
	path[len] = 0;
	return 0;
}

/* some platforms do not have setenv, define a replacement */
#if !HAVE_SETENV
void
odbc_setenv(const char *name, const char *value, int overwrite)
{
#if HAVE_PUTENV
	char buf[1024];

	sprintf(buf, "%s=%s", name, value);
	putenv(buf);
#endif
}
#endif

int
read_login_info(void)
{
	FILE *in;
	char line[512];
	char *s1, *s2;
	char path[1024];
	int len;

	in = fopen(".." TDS_SDIR_SEPARATOR ".." TDS_SDIR_SEPARATOR ".." TDS_SDIR_SEPARATOR "PWD", "r");
	if (!in)
		in = fopen("PWD", "r");

	if (!in) {
		fprintf(stderr, "Can not open PWD file\n\n");
		return 1;
	}
	while (fgets(line, 512, in)) {
		s1 = strtok(line, "=");
		s2 = strtok(NULL, "\n");
		if (!s1 || !s2)
			continue;
		if (!strcmp(s1, "UID")) {
			strcpy(USER, s2);
		} else if (!strcmp(s1, "SRV")) {
			strcpy(SERVER, s2);
		} else if (!strcmp(s1, "PWD")) {
			strcpy(PASSWORD, s2);
		} else if (!strcmp(s1, "DB")) {
			strcpy(DATABASE, s2);
		}
	}
	fclose(in);

#ifndef WIN32
	/* find our driver */
	if (!getcwd(path, sizeof(path)))
		return 0;
	len = strlen(path);
	if (len < 10 || strcmp(path + len - 10, "/unittests") != 0)
		return 0;
	path[len - 9] = 0;
	/* TODO this must be extended with all possible systems... */
	if (!check_lib(path, ".libs/libtdsodbc.so") && !check_lib(path, ".libs/libtdsodbc.sl")
	    && !check_lib(path, ".libs/libtdsodbc.dll") && !check_lib(path, ".libs/libtdsodbc.dylib"))
		return 0;
	strcpy(DRIVER, path);

	/* craft out odbc.ini, avoid to read wrong one */
	in = fopen("odbc.ini", "w");
	if (in) {
		fprintf(in, "[%s]\nDriver = %s\nDatabase = %s\nServername = %s\n", SERVER, DRIVER, DATABASE, SERVER);
		fclose(in);
		setenv("ODBCINI", "./odbc.ini", 1);
		setenv("SYSODBCINI", "./odbc.ini", 1);
	}
#endif
	return 0;
}

void
ReportError(const char *errmsg, int line, const char *file)
{
	SQLSMALLINT handletype;
	SQLHANDLE handle;
	SQLRETURN ret;
	unsigned char sqlstate[6];
	unsigned char msg[256];


	if (Statement) {
		handletype = SQL_HANDLE_STMT;
		handle = Statement;
	} else if (Connection) {
		handletype = SQL_HANDLE_DBC;
		handle = Connection;
	} else {
		handletype = SQL_HANDLE_ENV;
		handle = Environment;
	}
	if (errmsg[0]) {
		if (line)
			printf("%s:%d %s\n", file, line, errmsg);
		else
			printf("%s\n", errmsg);
	}
	ret = SQLGetDiagRec(handletype, handle, 1, sqlstate, NULL, msg, sizeof(msg), NULL);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		fprintf(stderr, "SQL error %s -- %s\n", sqlstate, msg);
	exit(1);
}

void
CheckReturn(void)
{
	ReportError("", 0, "");
}

int
Connect(void)
{

	int res;


	char command[512];

	if (read_login_info())
		exit(1);

	if (SQLAllocEnv(&Environment) != SQL_SUCCESS) {
		printf("Unable to allocate env\n");
		exit(1);
	}

	if (use_odbc_version3)
		SQLSetEnvAttr(Environment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER) (SQL_OV_ODBC3), SQL_IS_UINTEGER);

	if (SQLAllocConnect(Environment, &Connection) != SQL_SUCCESS) {
		printf("Unable to allocate connection\n");
		SQLFreeEnv(Environment);
		exit(1);
	}
	printf("odbctest\n--------\n\n");
	printf("connection parameters:\nserver:   '%s'\nuser:     '%s'\npassword: '%s'\ndatabase: '%s'\n",
	       SERVER, USER, "????" /* PASSWORD */ , DATABASE);

	res = SQLConnect(Connection, (SQLCHAR *) SERVER, SQL_NTS, (SQLCHAR *) USER, SQL_NTS, (SQLCHAR *) PASSWORD, SQL_NTS);
	if (!SQL_SUCCEEDED(res)) {
		printf("Unable to open data source (ret=%d)\n", res);
		CheckReturn();
		exit(1);
	}

	if (SQLAllocStmt(Connection, &Statement) != SQL_SUCCESS) {
		printf("Unable to allocate statement\n");
		CheckReturn();
		exit(1);
	}

	sprintf(command, "use %s", DATABASE);
	printf("%s\n", command);

	if (!SQL_SUCCEEDED(SQLExecDirect(Statement, (SQLCHAR *) command, SQL_NTS))) {
		printf("Unable to execute statement\n");
		CheckReturn();
		exit(1);
	}
	return 0;
}

int
Disconnect(void)
{
	if (Statement) {
		SQLFreeStmt(Statement, SQL_DROP);
		Statement = SQL_NULL_HSTMT;
	}

	if (Connection) {
		SQLDisconnect(Connection);
		SQLFreeConnect(Connection);
		Connection = SQL_NULL_HDBC;
	}

	if (Environment) {
		SQLFreeEnv(Environment);
		Environment = SQL_NULL_HENV;
	}
	return 0;
}

void
Command(HSTMT stmt, const char *command)
{
	int result = 0;

	printf("%s\n", command);
	result = SQLExecDirect(stmt, (SQLCHAR *) command, SQL_NTS);
	if (result != SQL_SUCCESS && result != SQL_NO_DATA) {
		printf("Unable to execute statement\n");
		CheckReturn();
		exit(1);
	}
}

SQLRETURN
CommandWithResult(HSTMT stmt, const char *command)
{
	printf("%s\n", command);
	return SQLExecDirect(stmt, (SQLCHAR *) command, SQL_NTS);
}

static int ms_db = -1;
int
db_is_microsoft(void)
{
	char buf[64];
	SQLSMALLINT len;
	int i;

	if (ms_db < 0) {
		buf[0] = 0;
		SQLGetInfo(Connection, SQL_DBMS_NAME, buf, sizeof(buf), &len);
		for (i = 0; buf[i]; ++i)
			buf[i] = tolower(buf[i]);
		ms_db = (strstr(buf, "microsoft") != NULL);
	}
	return ms_db;
}

static int freetds_driver = -1;
int
driver_is_freetds(void)
{
	char buf[64];
	SQLSMALLINT len;
	int i;

	if (freetds_driver < 0) {
		buf[0] = 0;
		SQLGetInfo(Connection, SQL_DRIVER_NAME, buf, sizeof(buf), &len);
		for (i = 0; buf[i]; ++i)
			buf[i] = tolower(buf[i]);
		freetds_driver = (strstr(buf, "tds") != NULL);
	}
	return freetds_driver;
}
