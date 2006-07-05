/* 
 * Purpose: Test remote procedure calls
 * Functions:  dbretdata dbretlen dbretname dbretstatus dbrettype dbrpcinit dbrpcparam dbrpcsend 
 */

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

#ifdef DBNTWIN32
#include "winhackery.h"
#endif

#include <sqlfront.h>
#include <sqldb.h>

#include "common.h"

static char software_version[] = "$Id: rpc.c,v 1.25 2006/07/05 19:44:52 jklowden Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static char cmd[4096];
static int init_proc(DBPROCESS * dbproc, const char *name);

typedef struct {
	char *name, *value;
	int type, len;
} RETPARAM;

static RETPARAM* save_retparam(RETPARAM *param, char *name, char *value, int type, int len);

static const char procedure_sql[] = 
		"CREATE PROCEDURE %s \n"
			"  @null_input varchar(30) OUTPUT \n"
			", @first_type varchar(30) OUTPUT \n"
			", @nullout int OUTPUT\n"
			", @nrows int OUTPUT \n"
			", @c varchar(20)\n"
		"AS \n"
		"BEGIN \n"
			"select @null_input = max(convert(varchar(30), name)) from systypes \n"
			"select @first_type = min(convert(varchar(30), name)) from systypes \n"
			"select name from sysobjects where 0=1\n"
			"select distinct convert(varchar(30), name) as 'type'  from systypes \n"
				"where name in ('int', 'char', 'text') \n"
			"select @nrows = @@rowcount \n"
			"select distinct convert(varchar(30), name) as name  from sysobjects where type = 'S' \n"
			"return 42 \n"
		"END \n";

static int
init_proc(DBPROCESS * dbproc, const char *name)
{
	int res = 0;

	if (name[0] != '#') {
		fprintf(stdout, "Dropping procedure %s\n", name);
		add_bread_crumb();
		sprintf(cmd, "DROP PROCEDURE %s", name);
		dbcmd(dbproc, cmd);
		add_bread_crumb();
		dbsqlexec(dbproc);
		add_bread_crumb();
		while (dbresults(dbproc) != NO_MORE_RESULTS) {
			/* nop */
		}
		add_bread_crumb();
	}

	fprintf(stdout, "Creating procedure %s\n", name);
	sprintf(cmd, procedure_sql, name);
	dbcmd(dbproc, cmd);
	if (dbsqlexec(dbproc) == FAIL) {
		add_bread_crumb();
		res = 1;
		if (name[0] == '#')
			fprintf(stdout, "Failed to create procedure %s. Wrong permission or not MSSQL.\n", name);
		else
			fprintf(stdout, "Failed to create procedure %s. Wrong permission.\n", name);
	}
	while (dbresults(dbproc) != NO_MORE_RESULTS) {
		/* nop */
	}
	return res;
}

static RETPARAM*
save_retparam(RETPARAM *param, char *name, char *value, int type, int len)
{
	if (param->name)
		free(param->name);
	if (param->value)
		free(param->value);
	
	param->name = strdup(name);
	param->value = strdup(value);
	
	param->type = type;
	param->len = len;
	
	return param;
}

int
main(int argc, char **argv)
{
	LOGINREC *login;
	DBPROCESS *dbproc;
	RETPARAM save_param;
	
	int i, r;
	char teststr[1024];
	int failed = 0;
	char *retname = NULL;
	int rettype = 0, retlen = 0;
	int return_status = 0;
	char proc[] = "#t0022", 
	     param0[] = "@null_input", 
	     param1[] = "@first_type", 
	     param2[] = "@nullout",
	     param3[] = "@nrows",
	     param4[] = "@c";
	char *proc_name = proc;

	char param_data1[64];
	int param_data2;
	int param_data3;
	RETCODE erc, row_code;
	int num_resultset = 0;
	int num_empty_resultset = 0;

	set_malloc_options();
	
	memset(&save_param, 0, sizeof(save_param));

	read_login_info(argc, argv);

	fprintf(stdout, "Start\n");
	add_bread_crumb();

	dbinit();

	add_bread_crumb();
	dberrhandle(syb_err_handler);
	dbmsghandle(syb_msg_handler);

	fprintf(stdout, "About to logon\n");

	add_bread_crumb();
	login = dblogin();
	DBSETLPWD(login, PASSWORD);
	DBSETLUSER(login, USER);
	DBSETLAPP(login, "#t0022");

	fprintf(stdout, "About to open %s.%s\n", SERVER, DATABASE);

	add_bread_crumb();
	dbproc = dbopen(login, SERVER);
	if (strlen(DATABASE))
		dbuse(dbproc, DATABASE);
	add_bread_crumb();
	dbloginfree(login);
	add_bread_crumb();

	add_bread_crumb();

	if (init_proc(dbproc, proc_name))
		if (init_proc(dbproc, ++proc_name))
			exit(1);

	/* set up and send the rpc */
	erc = dbrpcinit(dbproc, proc_name, 0);	/* no options */
	printf("executing dbrpcinit\n");
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcinit\n");
		failed = 1;
	}

	printf("executing dbrpcparam\n");
	erc = dbrpcparam(dbproc, param0, DBRPCRETURN, SYBCHAR, /*maxlen= */ -1, /* datlen= */ 0, NULL);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcparam\n");
		failed = 1;
	}

	printf("executing dbrpcparam\n");
	erc = dbrpcparam(dbproc, param1, DBRPCRETURN, SYBCHAR, /*maxlen= */ sizeof(param_data1), /* datlen= */ 0, (BYTE *) & param_data1);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcparam\n");
		failed = 1;
	}

	printf("executing dbrpcparam\n");
	erc = dbrpcparam(dbproc, param2, DBRPCRETURN, SYBINT4, /*maxlen= */ -1, /* datalen= */ 0, (BYTE *) & param_data2);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcparam\n");
		failed = 1;
	}

	printf("executing dbrpcparam\n");
	erc = dbrpcparam(dbproc, param3, DBRPCRETURN, SYBINT4, /*maxlen= */ -1, /* datalen= */ -1, (BYTE *) & param_data3);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcparam\n");
		failed = 1;
	}

	/* test for strange parameters using input */
	printf("executing dbrpcparam\n");
	erc = dbrpcparam(dbproc, param4, 0, SYBVARCHAR, /*maxlen= */ 0, /* datalen= */ 0, NULL);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcparam\n");
		failed = 1;
	}

	printf("executing dbrpcsend\n");
	param_data3 = 0x11223344;
	erc = dbrpcsend(dbproc);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbrpcsend\n");
		exit(1);
	}

	/* wait for it to execute */
	printf("executing dbsqlok\n");
	erc = dbsqlok(dbproc);
	if (erc == FAIL) {
		fprintf(stderr, "Failed: dbsqlok\n");
		exit(1);
	}

	add_bread_crumb();

	/* retrieve outputs per usual */
	r = 0;
	while ((erc = dbresults(dbproc)) != NO_MORE_RESULTS) {
		static const char dashes5[]  = "-----", 
				  dashes15[] = "---------------", 
				  dashes30[] = "------------------------------";
		if (erc == SUCCEED) { 
			const int ncols = dbnumcols(dbproc);
			int empty_resultset = 1;
			++num_resultset;
			printf("bound 1 of %d columns ('%s') in result %d.\n", ncols, dbcolname(dbproc, 1), ++r);
			dbbind(dbproc, 1, STRINGBIND, -1, (BYTE *) teststr);
			
			printf("\t%s\n\t-----------\n", dbcolname(dbproc, 1));
			while ((row_code = dbnextrow(dbproc)) != NO_MORE_ROWS) {
				empty_resultset = 0;
				if (row_code == REG_ROW) {
					printf("\t%s\n", teststr);
				} else {
					/* not supporting computed rows in this unit test */
					failed = 1;
					fprintf(stderr, "Failed.  Expected a row\n");
					exit(1);
				}
			}
			if (empty_resultset)
				++num_empty_resultset;
			
#define NORMAL_PROCESSING 0
#define USE_DBHASRETSTAT 0
#if NORMAL_PROCESSING
			/* check return status */
			printf("retrieving return status...\n");
			if (dbhasretstat(dbproc) == TRUE) {
				printf("%d\n", return_status = dbretstatus(dbproc));
			} else {
				printf("none\n");
			}
			
			/* check return parameter values */
			printf("retrieving output parameters...\n");
			printf("%-5s %-15s %5s %6s  %-30s\n", "param", "name", "type", "length", "data"); 
			printf("%-5s %-15s %5s %5s- %-30s\n", dashes5, dashes15, dashes5, dashes5, dashes30); 
			for (i = 1; i <= dbnumrets(dbproc); i++) {
				add_bread_crumb();
				retname = dbretname(dbproc, i);
				rettype = dbrettype(dbproc, i);
				retlen = dbretlen(dbproc, i);
				dbconvert(dbproc, rettype, dbretdata(dbproc, i), retlen, SYBVARCHAR, (BYTE*) teststr, -1);
				printf("%-5d %-15s %5d %6d  %-30s\n", i, retname, rettype, retlen, teststr); 
				add_bread_crumb();
				
				save_retparam(&save_param, retname, teststr, rettype, retlen);
			}
			
			if (num_resultset == 3 && i < 4) {	/* dbnumrets missed something */
				fprintf(stderr, "Expected 4 output parameters.\n");
				exit(1);
			}
#else
	if(USE_DBHASRETSTAT) dbhasretstat(dbproc);
#endif
			
		} else {
			add_bread_crumb();
			fprintf(stderr, "Expected a result set.\n");
			exit(1);
		}
	} /* while dbresults */
	
	/* 
	 * Test the last parameter for expected outcome 
	 */
	if ((save_param.name == NULL) || strcmp(save_param.name, param3)) {
		fprintf(stderr, "Expected retname to be '%s', got ", param3);
		if (save_param.name == NULL) 
			fprintf(stderr, "<NULL> instead.\n");
		else
			fprintf(stderr, "'%s' instead.\n", save_param.name);
		exit(1);
	}
	if (strcmp(save_param.value, "3")) {
		fprintf(stderr, "Expected retdata to be 3.\n");
		exit(1);
	}
	if (save_param.type != SYBINT4) {
		fprintf(stderr, "Expected rettype to be SYBINT4 was %d.\n", save_param.type);
		exit(1);
	}
	if (save_param.len != 4) {
		fprintf(stderr, "Expected retlen to be 4.\n");
		exit(1);
	}

	if(42 != return_status) {
		fprintf(stderr, "Expected status to be 42.\n");
		exit(1);
	}

	printf("Good: Got 4 output parameters and 1 return status of %d.\n", return_status);


	/* Test number of result sets */
	if (num_resultset != 3) {
		fprintf(stderr, "Expected 3 resultset got %d.\n", num_resultset);
		exit(1);
	}
	if (num_empty_resultset != 1) {
		fprintf(stderr, "Expected an empty resultset got %d.\n", num_empty_resultset);
		exit(1);
	}
	printf("Good: Got %d resultsets and %d empty resultset.\n", num_resultset, num_empty_resultset);

	add_bread_crumb();


	fprintf(stdout, "Dropping procedure\n");
	add_bread_crumb();
	sprintf(cmd, "DROP PROCEDURE %s", proc_name);
	dbcmd(dbproc, cmd);
	add_bread_crumb();
	dbsqlexec(dbproc);
	add_bread_crumb();
	while (dbresults(dbproc) != NO_MORE_RESULTS) {
		/* nop */
	}
	add_bread_crumb();
	dbexit();
	add_bread_crumb();

	fprintf(stdout, "dblib %s on %s\n", (failed ? "failed!" : "okay"), __FILE__);
	free_bread_crumb();

	if (save_param.name)
		free(save_param.name);
	if (save_param.value)
		free(save_param.value);

	return failed ? 1 : 0;
}
