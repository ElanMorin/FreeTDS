/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998, 1999, 2000, 2001  Brian Bruns
 * Copyright (C) 2002, 2003, 2004    James K. Lowden
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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <assert.h>

#include "tds.h"
#include "sybfront.h"
#include "sybdb.h"
#include "dblib.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char software_version[] = "$Id: rpc.c,v 1.32 2004/07/29 10:22:40 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

static void rpc_clear(DBREMOTE_PROC * rpc);
static void param_clear(DBREMOTE_PROC_PARAM * pparam);

static TDSPARAMINFO *param_info_alloc(TDSSOCKET * tds, DBREMOTE_PROC * rpc);

/**
 * Initialize a remote procedure call. 
 *
 * Only supported option would be DBRPCRECOMPILE, 
 * which causes the stored procedure to be recompiled before executing.
 * FIXME: I don't know the value for DBRPCRECOMPILE and have not added it to sybdb.h
 */

RETCODE
dbrpcinit(DBPROCESS * dbproc, char *rpcname, DBSMALLINT options)
{
	DBREMOTE_PROC **rpc;
	int dbrpcrecompile = 0;

	/* sanity */
	if (dbproc == NULL || rpcname == NULL)
		return FAIL;

	if (options & DBRPCRESET) {
		rpc_clear(dbproc->rpc);
		dbproc->rpc = NULL;
		return SUCCEED;
	}

	/* any bits we want from the options argument */
	dbrpcrecompile = options & DBRPCRECOMPILE;
	options &= ~DBRPCRECOMPILE;	/* turn that one off, now that we've extracted it */

	/* all other options except DBRPCRECOMPILE are invalid */
	if (options) {
		/* should show client error message */
		return FAIL;
	}

	/* to allocate, first find a free node */
	for (rpc = &dbproc->rpc; *rpc != NULL; rpc = &(*rpc)->next) {
		/* check existing nodes for name match (there shouldn't be one) */
		if (!(*rpc)->name)
			return FAIL;
		if (strcmp((*rpc)->name, rpcname) == 0)
			return FAIL /* dbrpcsend should free pointer */ ;
	}

	/* rpc now contains the address of the dbproc's first empty (null) DBREMOTE_PROC* */

	/* allocate */
	*rpc = (DBREMOTE_PROC *) malloc(sizeof(DBREMOTE_PROC));
	if (*rpc == NULL)
		return FAIL;
	memset(*rpc, 0, sizeof(DBREMOTE_PROC));

	(*rpc)->name = strdup(rpcname);
	if ((*rpc)->name == NULL)
		return FAIL;

	/* store */
	(*rpc)->options = options & DBRPCRECOMPILE;
	(*rpc)->param_list = (DBREMOTE_PROC_PARAM *) NULL;

	/* completed */
	tdsdump_log(TDS_DBG_INFO1, "dbrpcinit() added rpcname \"%s\"\n", rpcname);

	return SUCCEED;
}

/**
 * \ingroup dblib_api
 * \brief Add a parameter to a remote procedure call.
 * Call between dbrpcinit() and dbrpcsend()
 * \param dbproc contains all information needed by db-lib to manage communications with the server.
 * \param paramname literal name of the parameter, according to the stored procedure (starts with '@').  Optional.  
 *        If not used, parameters will be passed in order instead of by name. 
 * \param status must be DBRPCRETURN, if this parameter is a return parameter, else 0. 
 * \param type datatype of the value parameter e.g., SYBINT4, SYBCHAR.
 * \param maxlen Maximum output size of the parameter's value to be returned by the stored procedure, usually the size of your host variable. 
 *        Fixed-length datatypes take -1 (NULL or not).  
 *        Non-OUTPUT parameters also use -1.  
 *	   Use 0 to send a NULL value for a variable length datatype.  
 * \param datalen For variable-length datatypes, the byte size of the data to be sent, exclusive of any null terminator. 
 *        For fixed-length datatypes use -1.  To send a NULL value, use 0.  
 * \param value Address of your host variable.  
 * \retval SUCCEED normal.
 * \retval FAIL on error
 * \sa dbrpcinit(), dbrpcsend()
 */
RETCODE
dbrpcparam(DBPROCESS * dbproc, char *paramname, BYTE status, int type, DBINT maxlen, DBINT datalen, BYTE * value)
{
	char *name = NULL;
	DBREMOTE_PROC *rpc;
	DBREMOTE_PROC_PARAM **pparam;
	DBREMOTE_PROC_PARAM *param;

	/* sanity */
	if (dbproc == NULL || value == NULL)
		return FAIL;
	if (dbproc->rpc == NULL)
		return FAIL;

	/* Correctness:
	 * Parameter 			maxlen 			datalen 
	 * -----------------------	-------------------	----------------------------------
	 * Fixed-length 		-1 			-1 
	 * Fixed-length, NULL 		-1 			 0 
	 * Variable-length 		Max output size		input size without null terminator 
	 * Variable-length, NULL 	 0 			 0 
	 */
	if (is_char_type(type)) {
		if (maxlen < 0 || datalen < 0) {
			tdsdump_log(TDS_DBG_INFO1, "dbrpcparam(): variable-length type %d, maxlen=%d, datalen=%d\n", 
							type, maxlen, datalen);
			return FAIL;
		}
	} else {
		if (maxlen != -1 || datalen > 0) {
			tdsdump_log(TDS_DBG_INFO1, "dbrpcparam(): fixed-length type %d, maxlen=%d, datalen=%d\n", 
							type, maxlen, datalen);
			return FAIL;
		}
	}

	/* TODO add other tests for correctness */

	/* allocate */
	param = (DBREMOTE_PROC_PARAM *) malloc(sizeof(DBREMOTE_PROC_PARAM));
	if (param == NULL)
		return FAIL;

	if (paramname) {
		name = strdup(paramname);
		if (name == NULL)
			return FAIL;
	}

	/* initialize */
	param->next = (DBREMOTE_PROC_PARAM *) NULL;	/* NULL signifies end of linked list */
	param->name = name;
	param->status = status;
	param->type = type;
	param->maxlen = maxlen;
	param->datalen = datalen;
	param->value = value;

	/*
	 * Add a parameter to the current rpc.  
	 * 
	 * Traverse the dbproc's procedure list to find the current rpc, 
	 * then traverse the parameter linked list until its end,
	 * then tack on our parameter's address.  
	 */
	for (rpc = dbproc->rpc; rpc->next != NULL; rpc = rpc->next)	/* find "current" procedure */
		;
	for (pparam = &rpc->param_list; *pparam != NULL; pparam = &(*pparam)->next);

	/* pparam now contains the address of the end of the rpc's parameter list */

	*pparam = param;	/* add to the end of the list */

	tdsdump_log(TDS_DBG_INFO1, "dbrpcparam() added parameter \"%s\"\n", (paramname) ? paramname : "");

	return SUCCEED;
}

/**
 * Execute the procedure and free associated memory
 */
RETCODE
dbrpcsend(DBPROCESS * dbproc)
{
	DBREMOTE_PROC *rpc;

	/* sanity */
	if (dbproc == NULL || dbproc->rpc == NULL	/* dbrpcinit should allocate pointer */
	    || dbproc->rpc->name == NULL) {	/* can't be ready without a name */
		return FAIL;
	}

	dbproc->dbresults_state = DBRESINIT;

	/* FIXME do stuff */
	tdsdump_log(TDS_DBG_FUNC, "dbrpcsend()\n");

	for (rpc = dbproc->rpc; rpc != NULL; rpc = rpc->next) {
		int erc;
		TDSPARAMINFO *pparam_info = NULL;

		/*
		 * liam@inodes.org: allow stored procedures to have no
		 * paramaters 
		 */
		if (rpc->param_list != NULL) {
			pparam_info = param_info_alloc(dbproc->tds_socket, rpc);
			if (!pparam_info)
				return FAIL;
		}
		erc = tds_submit_rpc(dbproc->tds_socket, dbproc->rpc->name, pparam_info);
		tds_free_param_results(pparam_info);
		if (erc == TDS_FAIL)
			return FAIL;
	}

	/* free up the memory */
	rpc_clear(dbproc->rpc);
	dbproc->rpc = NULL;

	return SUCCEED;
}

/** 
 * Tell the TDSPARAMINFO structure where the data go.  This is a kind of "bind" operation.
 */
static const unsigned char *
param_row_alloc(TDSPARAMINFO * params, TDSCOLUMN * curcol, void *value, int size)
{
	const unsigned char *row = tds_alloc_param_row(params, curcol);

	if (!row)
		return NULL;
	memcpy(&params->current_row[curcol->column_offset], value, size);

	return row;
}

/** 
 * Allocate memory and copy the rpc information into a TDSPARAMINFO structure.
 */
static TDSPARAMINFO *
param_info_alloc(TDSSOCKET * tds, DBREMOTE_PROC * rpc)
{
	int i;
	DBREMOTE_PROC_PARAM *p;
	TDSCOLUMN *pcol;
	TDSPARAMINFO *params = NULL, *new_params;

	/* sanity */
	if (rpc == NULL)
		return NULL;

	/* see v 1.10 2002/11/23 for first broken attempt */

	for (i = 0, p = rpc->param_list; p != NULL; p = p->next, i++) {
		const unsigned char *prow;

		if (!(new_params = tds_alloc_param_result(params))) {
			tds_free_param_results(params);
			fprintf(stderr, "out of rpc memory!");
			return NULL;
		}
		params = new_params;

		pcol = params->columns[i];

		/* meta data */
		if (p->name) {
			strncpy(pcol->column_name, p->name, sizeof(pcol->column_name));
			pcol->column_name[sizeof(pcol->column_name) - 1] = 0;
			pcol->column_namelen = strlen(pcol->column_name);
		}
		tds_set_param_type(tds, pcol, p->type);
		if (pcol->column_varint_size) {
			if (p->maxlen < 0) {
				tdsdump_log(TDS_DBG_FUNC, "param_info_alloc(): p->maxlen is %d"
							     "parameters of variable-size datatypes "
							     "require a non-negative input length\n", p->maxlen);
				tds_free_param_results(params);
				return NULL;
			}
			pcol->column_size = p->maxlen;

			/* actual data */
			pcol->column_cur_size = p->datalen;
		}
		pcol->column_output = p->status;

		prow = param_row_alloc(params, pcol, p->value, pcol->column_cur_size);

		if (!prow) {
			tds_free_param_results(params);
			fprintf(stderr, "out of memory for rpc row!");
			return NULL;
		}

	}

	return params;

}

/**
 * erase the procedure list
 */
static void
rpc_clear(DBREMOTE_PROC * rpc)
{
	DBREMOTE_PROC * next;

	while (rpc) {
		next = rpc->next;
		param_clear(rpc->param_list);
		if (rpc->name);
			free(rpc->name);
		free(rpc);
		rpc = next;
	}
}

/**
 * erase the parameter list
 */
static void
param_clear(DBREMOTE_PROC_PARAM * pparam)
{
	DBREMOTE_PROC_PARAM * next;

	while (pparam) {
		next = pparam->next;
		if (pparam->name)
			free(pparam->name);
		/* free self */
		free(pparam);
		pparam = next;
	}
}
