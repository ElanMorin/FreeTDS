/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-2003  Brian Bruns
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

#include "common.h"

#include <tdsconvert.h>

static char software_version[] = "$Id: flags.c,v 1.5 2003/06/11 20:11:00 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static TDSLOGIN *login;
static TDSSOCKET *tds;

static void
fatal_error(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void
check_flags(TDSCOLINFO * curcol, int n, const char *possible_results)
{
	char msg[256];
	char flags[256];
	int l;
	char *all_res = strdup(possible_results);
	char *res;
	int correct = 0;

	flags[0] = 0;
	if (curcol->column_nullable)
		strcat(flags, "nullable ");
	if (curcol->column_writeable)
		strcat(flags, "writable ");
	if (curcol->column_identity)
		strcat(flags, "identity ");
	if (curcol->column_key)
		strcat(flags, "key ");
	if (curcol->column_hidden)
		strcat(flags, "hidden ");
	l = strlen(flags);
	if (l)
		flags[l - 1] = 0;

	/* one result is valid ?? */
	for (res = strtok(all_res, "-"); res; res = strtok(NULL, "-"))
		if (strcmp(flags, res) == 0)
			correct = 1;

	if (!correct) {
		sprintf(msg, "flags:%s\nwrong column %d flags", flags, n + 1);
		fatal_error(msg);
	}
}

static void
test_begin(const char *cmd)
{
	TDS_INT result_type;

	fprintf(stdout, "%s: Testing query\n", cmd);
	if (tds_submit_query(tds, cmd, NULL) != TDS_SUCCEED)
		fatal_error("tds_submit_query() failed");

	if (tds_process_result_tokens(tds, &result_type, NULL) != TDS_SUCCEED)
		fatal_error("tds_process_result_tokens() failed");

	if (result_type != TDS_ROWFMT_RESULT)
		fatal_error("expected row fmt() failed");

	/* test columns results */
	if (tds->curr_resinfo != tds->res_info)
		fatal_error("wrong curr_resinfo");
}

static void
test_end(void)
{
	TDS_INT result_type;
	int done_flags;

	if (tds_process_result_tokens(tds, &result_type, &done_flags) != TDS_SUCCEED)
		fatal_error("tds_process_result_tokens() failed");

	if (result_type != TDS_DONE_RESULT)
		fatal_error("expected done failed");

	if (done_flags & TDS_DONE_ERROR)
		fatal_error("query failed");

	if (tds_process_result_tokens(tds, &result_type, NULL) != TDS_NO_MORE_RESULTS)
		fatal_error("tds_process_result_tokens() failed");
}

int
main(int argc, char **argv)
{
	TDSRESULTINFO *info;

	fprintf(stdout, "%s: Testing flags from server\n", __FILE__);
	if (try_tds_login(&login, &tds, __FILE__, 0) != TDS_SUCCEED) {
		fprintf(stderr, "try_tds_login() failed\n");
		return 1;
	}

	if (run_query(tds, "create table #tmp1 (i numeric(10,0) identity primary key, b varchar(20) null, c int not null)") !=
	    TDS_SUCCEED)
		fatal_error("creating table error");

	/* TDS 4.2 without FOR BROWSE clause seem to forget flags... */
	if (!IS_TDS42(tds)) {
		/* check select of all fields */
		test_begin("select * from #tmp1");
		info = tds->curr_resinfo;

		if (info->num_cols != 3)
			fatal_error("wrong number or columns returned");

		check_flags(info->columns[0], 0, "identity");
		check_flags(info->columns[1], 1, "nullable writable");
		check_flags(info->columns[2], 2, "writable");

		test_end();
	}


	/* check select of 2 field */
	test_begin("select c, b from #tmp1 for browse");
	info = tds->curr_resinfo;

	if (info->num_cols != 3)
		fatal_error("wrong number or columns returned");

	check_flags(info->columns[0], 0, "writable");
	if (!IS_TDS42(tds))
		check_flags(info->columns[1], 1, "nullable writable");
	else
		check_flags(info->columns[1], 1, "writable");
	check_flags(info->columns[2], 2, "writable key hidden-writable identity key hidden");

	test_end();

	try_tds_logout(login, tds, 0);
	return 0;
}
