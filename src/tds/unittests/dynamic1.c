/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-2002  Brian Bruns
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

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <tds.h>
#include "common.h"

static char software_version[] = "$Id: dynamic1.c,v 1.1 2002/11/22 12:55:23 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static void
fatal_error(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void
test(TDSSOCKET * tds, TDSDYNAMIC * dyn, TDS_INT n, const char *s)
{
	TDSPARAMINFO *params;
	TDSCOLINFO *curcol;
	int len = strlen(s);

	tds_free_input_params(dyn);

	if (!(params = tds_alloc_param_result(dyn->params)))
		fatal_error("out of memory!");
	dyn->params = params;

	curcol = params->columns[0];
	curcol->column_type = SYBINT4;

	/* TODO test error */
	tds_alloc_param_row(params, curcol);
	n = 123;
	memcpy(&params->current_row[curcol->column_offset], &n, sizeof(n));

	if (!(params = tds_alloc_param_result(dyn->params)))
		fatal_error("out of memory!");
	dyn->params = params;

	curcol = params->columns[1];
	curcol->column_type = SYBVARCHAR;
	curcol->column_size = 40;
	curcol->column_varint_size = 1;
	curcol->column_cur_size = len;

	tds_alloc_param_row(params, curcol);
	memcpy(&params->current_row[curcol->column_offset], s, len);

	if (tds_submit_execute(tds, dyn) != TDS_SUCCEED)
		fatal_error("tds_submit_execute() error");
}

int
main(int argc, char **argv)
{
	TDSLOGIN *login;
	TDSSOCKET *tds;
	int verbose = 0;
	TDSDYNAMIC *dyn;
	int rc;
	TDS_INT n;

	fprintf(stdout, "%s: Test dynamic queries\n", __FILE__);
	rc = try_tds_login(&login, &tds, __FILE__, verbose);
	if (rc != TDS_SUCCEED)
		fatal_error("try_tds_login() failed");

	run_query(tds, "DROP TABLE #dynamic1");
	if (run_query(tds, "CREATE TABLE #dynamic1 (i INT, c VARCHAR(40))") != TDS_SUCCEED)
		fatal_error("creating table error");

	if (tds->cur_dyn)
		fatal_error("already a dynamic query??");

	/* prepare to insert */
	if (tds_submit_prepare(tds, "INSERT INTO #dynamic1(i,c) VALUES(?,?)", NULL) != TDS_SUCCEED)
		fatal_error("tds_submit_prepare() error");

	dyn = tds->cur_dyn;
	if (!dyn)
		fatal_error("dynamic not present??");

	/* insert one record */
	test(tds, dyn, 123, "dynamic");

	/* some test */
	if (run_query(tds, "DECLARE @n INT SELECT @n = COUNT(*) FROM #dynamic1 IF @n <> 1 SELECT 0") != TDS_SUCCEED)
		fatal_error("checking rows");

	if (run_query(tds, "DECLARE @n INT SELECT @n = COUNT(*) FROM #dynamic1 WHERE i = 123 AND c = 'dynamic' IF @n <> 1 SELECT 0")
	    != TDS_SUCCEED)
		fatal_error("checking rows 1");

	/* insert one record */
	test(tds, dyn, 654321, "a longer string");

	/* some test */
	if (run_query(tds, "DECLARE @n INT SELECT @n = COUNT(*) FROM #dynamic1 IF @n <> 2 SELECT 0") != TDS_SUCCEED)
		fatal_error("checking rows");

	if (run_query(tds, "DECLARE @n INT SELECT @n = COUNT(*) FROM #dynamic1 WHERE i = 123 AND c = 'dynamic' IF @n <> 1 SELECT 0")
	    != TDS_SUCCEED)
		fatal_error("checking rows 1");

	if (run_query
	    (tds,
	     "DECLARE @n INT SELECT @n = COUNT(*) FROM #dynamic1 WHERE i = 654321 AND c = 'a longer string' IF @n <> 1 SELECT 0") !=
	    TDS_SUCCEED)
		fatal_error("checking rows 1");

	if (run_query(tds, "DROP TABLE #dynamic1") != TDS_SUCCEED)
		fatal_error("dropping table error");

	try_tds_logout(login, tds, verbose);
	return 0;
}
