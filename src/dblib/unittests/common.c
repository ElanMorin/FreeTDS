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

#if HAVE_LIBGEN_H
#include <libgen.h>
#endif /* HAVE_LIBGEN_H */

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */

#include <sqlfront.h>
#include <sqldb.h>

#include "common.h"

static char software_version[] = "$Id: common.c,v 1.10 2002/12/14 14:47:46 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

typedef struct _tag_memcheck_t
{
	int item_number;
	int special;
	struct _tag_memcheck_t *next;
}
memcheck_t;


static memcheck_t *breadcrumbs = NULL;
static int num_breadcrumbs = 0;
static const int BREADCRUMB = 0xABCD7890;

char USER[512];
char SERVER[512];
char PASSWORD[512];
char DATABASE[512];
static char *DIRNAME = NULL;

#if HAVE_MALLOC_OPTIONS
extern const char *malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

void
set_malloc_options(void)
{

#if HAVE_MALLOC_OPTIONS
	/*
	 * Options for malloc
	 * A- all warnings are fatal
	 * J- init memory to 0xD0
	 * R- always move memory block on a realloc
	 */
	malloc_options = "AJR";
#endif /* HAVE_MALLOC_OPTIONS */
}

/**
 * pass argv[0].  Set up directory so we can find the PWD file, regardless of 
 * where it was invoked from.
 */
int
read_PWD(char invoked_as[])
{
	if (invoked_as)
		DIRNAME = dirname(invoked_as);

	return read_login_info();
}

int
read_login_info(void)
{
	FILE *in;
	char line[512];
	char *s1, *s2;
	char filename[MAXPATHLEN];
	static const char *PWD = "../../../PWD";

	strcpy(filename, PWD);
	in = fopen(filename, "r");
	if (!in) {
		sprintf(filename, "%s/%s", (DIRNAME) ? DIRNAME : ".", PWD);

		in = fopen(filename, "r");
		if (!in) {
			fprintf(stderr, "Can not open %s file\n\n", filename);
			return 1;
		}
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
	printf("found %s.%s for %s in \"%s\"\n", SERVER, DATABASE, USER, filename);
	return 0;
}

void
check_crumbs(void)
{
	int i;
	memcheck_t *ptr = breadcrumbs;

	i = num_breadcrumbs;
	while (ptr != NULL) {
		if (ptr->special != BREADCRUMB || ptr->item_number != i) {
			fprintf(stderr, "Somebody overwrote one of the bread crumbs!!!\n");
			abort();
		}

		i--;
		ptr = ptr->next;
	}
}



void
add_bread_crumb(void)
{
	memcheck_t *tmp;

	check_crumbs();

	tmp = (memcheck_t *) calloc(sizeof(memcheck_t), 1);
	if (tmp == NULL) {
		fprintf(stderr, "Out of memory");
		abort();
		exit(1);
	}

	num_breadcrumbs++;

	tmp->item_number = num_breadcrumbs;
	tmp->special = BREADCRUMB;
	tmp->next = breadcrumbs;

	breadcrumbs = tmp;
}

int
syb_msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname, int line)
{
	char var_value[31];
	int i;
	char *c;

	/*
	 * Check for "database changed", or "language changed" messages from
	 * the client.  If we get one of these, then we need to pull the
	 * name of the database or charset from the message and set the
	 * appropriate variable.
	 */
	if (msgno == 5701 ||	/* database context change */
	    msgno == 5703 ||	/* language changed */
	    msgno == 5704) {	/* charset changed */

		/* fprintf( stderr, "msgno = %d: %s\n", msgno, msgtext ) ; */

		if (msgtext != NULL && (c = strchr(msgtext, '\'')) != NULL) {
			i = 0;
			for (++c; i <= 30 && *c != '\0' && *c != '\''; ++c)
				var_value[i++] = *c;
			var_value[i] = '\0';

#if 0
			switch (msgno) {
			case 5701:
				env_set(g_env, "database", var_value);
				break;
			case 5703:
				env_set(g_env, "language", var_value);
				break;
			case 5704:
				env_set(g_env, "charset", var_value);
				-break;
			default:
				break;
			}
#endif
		}
		return 0;
	}

	/*
	 * If the severity is something other than 0 or the msg number is
	 * 0 (user informational messages).
	 */
	if (severity >= 0 || msgno == 0) {
		/*
		 * If the message was something other than informational, and
		 * the severity was greater than 0, then print information to
		 * stderr with a little pre-amble information.
		 */
		if (msgno > 0 && severity > 0) {
			fprintf(stderr, "Msg %d, Level %d, State %d\n", (int) msgno, (int) severity, (int) msgstate);
			fprintf(stderr, "Server '%s'", srvname);
			if (procname != NULL && *procname != '\0')
				fprintf(stderr, ", Procedure '%s'", procname);
			if (line > 0)
				fprintf(stderr, ", Line %d", line);
			fprintf(stderr, "\n");
			fprintf(stderr, "%s\n", msgtext);
			fflush(stderr);
		} else {
			/*
			 * Otherwise, it is just an informational (e.g. print) message
			 * from the server, so send it to stdout.
			 */
			fprintf(stdout, "%s\n", msgtext);
			fflush(stdout);
		}
	}

	return 0;
}

int
syb_err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr)
{

	/*
	 * For server messages, cancel the query and rely on the
	 * message handler to spew the appropriate error messages out.
	 */
	if (dberr == SYBESMSG)
		return INT_CANCEL;

#if 0
	/*
	 * For any other type of severity (that is not a server
	 * message), we increment the batch_failcount.
	 */
	env_set(g_env, "batch_failcount", "1");
#endif

	fprintf(stderr,
		"DB-LIBRARY error (severity %d, dberr %d, oserr %d, dberrstr %s, oserrstr %s):\n",
		severity, dberr, oserr, dberrstr ? dberrstr : "(null)", oserrstr ? oserrstr : "(null)");
	fflush(stderr);

	/*
	 * If the dbprocess is dead or the dbproc is a NULL pointer and
	 * we are not in the middle of logging in, then we need to exit.
	 * We can't do anything from here on out anyway.
	 * It's OK to end up here in response to a dbconvert() that
	 * resulted in overflow, so don't exit in that case.
	 */
	if ((dbproc == NULL) || DBDEAD(dbproc)) {
		if (dberr != SYBECOFL) {
			exit(255);
		}
	}

	return INT_CANCEL;
}
