/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 2004 Frediano Ziglio
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

#undef NDEBUG

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <assert.h>

#include "tds.h"
#include "tdsstring.h"
#include "tds_checks.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

static const char software_version[] = "$Id: tds_checks.c,v 1.1 2004/12/01 13:11:36 freddy77 Exp $";
static const void *const no_unused_var_warn[] = { software_version, no_unused_var_warn };

#if ENABLE_EXTRA_CHECKS

void
tds_check_tds_extra(const TDSSOCKET * tds)
{
	const int invalid_state = 0;
	int found, i;

	assert(tds);

	/* teset state and connection */
	switch (tds->state) {
	case TDS_DEAD:
	case TDS_QUERYING:
	case TDS_PENDING:
	case TDS_IDLE:
	case TDS_CANCELED:
		break;
	default:
		assert(invalid_state);
	}

	assert(tds->state == TDS_DEAD || !TDS_IS_SOCKET_INVALID(tds->s));
	assert(tds->state != TDS_DEAD || TDS_IS_SOCKET_INVALID(tds->s));

	/* test env */
	tds_check_env_extra(tds->env);

	/* test buffers and positions */
	assert(tds->in_pos <= tds->in_len && tds->in_len <= tds->in_buf_max);
	/* TODO remove blocksize from env and use out_len ?? */
/*	assert(tds->out_pos <= tds->out_len); */
/* 	assert(tds->out_len == 0 || tds->out_buf != NULL); */
	assert(tds->out_pos <= tds->env->block_size);
	assert(tds->env->block_size == 0 || tds->out_buf != NULL);
	assert(tds->in_buf_max == 0 || tds->in_buf != NULL);

	/* test res_info */
	if (tds->res_info)
		tds_check_resultinfo_extra(tds->res_info);

	/* test num_comp_info, comp_info */
	assert(tds->num_comp_info >= 0);
	for (i = 0; i < tds->num_comp_info; ++i) {
		assert(tds->comp_info);
		tds_check_resultinfo_extra(tds->comp_info[i]);
	}

	/* TODO param_info */
	if (tds->param_info)
		tds_check_resultinfo_extra(tds->param_info);

	/* TODO test cursor */

	/* test num_dyms, cur_dyn, dyns*/
	found = 0;
	assert(tds->num_dyns >= 0);
	for (i = 0; i < tds->num_dyns; ++i) {
		assert(tds->dyns);
		if (tds->dyns[i] == tds->cur_dyn)
			found = 1;
		tds_check_dynamic_extra(tds->dyns[i]);
	}
	assert(found || tds->cur_dyn == NULL);

	/* test tds_ctx*/
	tds_check_context_extra(tds->tds_ctx);

	/* TODO test char_conv_count, char_convs */
	/* TODO current_results should be one of res_info, comp_info, param_info or dynamic */
}

void
tds_check_context_extra(const TDSCONTEXT * ctx)
{
	assert(ctx);
}

void
tds_check_env_extra(const TDSENV * env)
{
	assert(env);

	assert(env->block_size >= 0 && env->block_size <= 65536);
}

void
tds_check_column_extra(const TDSCOLUMN * column)
{
	assert(column);

	assert(column->column_varint_size <= 5);

	assert(column->column_scale <= column->column_prec);
	assert(column->column_prec <= 77);

	assert(strlen(column->table_name) < sizeof(column->table_name));
	assert(strlen(column->column_name) < sizeof(column->column_name));
	
	/* TODO check type and server type same or SQLNCHAR -> SQLCHAR */
	/* TODO check current size <= size */
	/* TODO check size of fixed type correct */
	/* TODO check size of N types (ie intN) it's supported */
}

void
tds_check_resultinfo_extra(const TDSRESULTINFO * res_info)
{
	int i;
	assert(res_info);
	assert(res_info->num_cols >= 0);
	for (i = 0; i < res_info->num_cols; ++i) {
		assert(res_info->columns);
		tds_check_column_extra(res_info->columns[i]);
		assert(res_info->columns[i]->column_offset < res_info->row_size 
			|| (i==0 && res_info->columns[i]->column_offset == 0) 
			|| (res_info->columns[i]->column_offset == res_info->row_size && res_info->columns[i]->column_size == 0));
	}
	assert(res_info->row_size >= 0);
	assert(res_info->null_info_size <= res_info->row_size);
	/* space for parameters can be computed later */
	assert((res_info->num_cols + 7)/8 <= res_info->null_info_size || (res_info->null_info_size == 0 && !res_info->current_row));

	assert(res_info->computeid >= 0);

	assert(res_info->by_cols >= 0);
	assert(res_info->by_cols == 0 || res_info->bycolumns);
}

void
tds_check_cursor_extra(const TDSCURSOR * cursor)
{
}

void
tds_check_dynamic_extra(const TDSDYNAMIC * dyn)
{
	assert(dyn);

	if (dyn->res_info)
		tds_check_resultinfo_extra(dyn->res_info);
	if (dyn->params)
		tds_check_resultinfo_extra(dyn->params);

	assert(!dyn->emulated || dyn->query);
}

#endif /* ENABLE_EXTRA_CHECKS */
