/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-1999  Brian Bruns
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

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#include "tds.h"
#include "tdsconvert.h"
#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char  software_version[]   = "$Id: token.c,v 1.92 2002/11/04 19:49:20 castellano Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};

static int tds_process_msg(TDSSOCKET *tds,int marker);
static int tds_process_compute_result(TDSSOCKET *tds);
static int tds_process_compute_names(TDSSOCKET *tds);
static int tds7_process_compute_result(TDSSOCKET *tds);
static int tds_process_result(TDSSOCKET *tds);
static int tds_process_col_name(TDSSOCKET *tds);
static int tds_process_col_info(TDSSOCKET *tds);
static int tds_process_compute(TDSSOCKET *tds);
static int tds_process_row(TDSSOCKET *tds);
static int tds_process_param_result(TDSSOCKET *tds);
static int tds7_process_result(TDSSOCKET *tds);
static int tds_process_param_result_tokens(TDSSOCKET *tds);
static void tds_process_dyn_result(TDSSOCKET *tds);
static int tds_process_dynamic(TDSSOCKET *tds);
static int tds_process_auth(TDSSOCKET *tds);
static int tds_get_varint_size(int datatype);
static int tds_get_cardinal_type(int datatype);
static int tds_get_data(TDSSOCKET *tds,TDSCOLINFO *curcol,unsigned char *current_row, int i);
static int tds_get_data_info(TDSSOCKET *tds,TDSCOLINFO *curcol);

/*
** The following little table is indexed by precision and will
** tell us the number of bytes required to store the specified
** precision.
*/
extern const int g__numeric_bytes_per_prec[];

/**
 * \defgroup token Token processing
 * Handle tokens in packets. Many PDU (packets data unit) contain tokens.
 * (like result description, rows, data, errors and many other).
 */

/* TODO do a best check for alignment than this */
union { void *p; int i; } align_struct;
#define ALIGN_SIZE sizeof(align_struct)

/** \addtogroup token
 *  \@{ 
 */

/**
 * tds_process_default_tokens() is a catch all function that is called to
 * process tokens not known to other tds_process_* routines
 */
int tds_process_default_tokens(TDSSOCKET *tds, int marker)
{
int order_len;
int tok_size;
int   more_results;
int   cancelled;

   tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_default_tokens() marker is %x\n", marker);

   if (IS_TDSDEAD(tds)) {
        tdsdump_log(TDS_DBG_FUNC, "%L leaving tds_process_default_tokens() connection dead\n");
	tds->state=TDS_DEAD;
	return TDS_FAIL;
   }

   switch(marker) {
	case TDS_AUTH_TOKEN:
		tds_process_auth(tds);
		break;
	case TDS_ENV_CHG_TOKEN:
      {
         tds_process_env_chg(tds);
         break;
      }
      case TDS_DONE_TOKEN:
      case TDS_DONEPROC_TOKEN:
      case TDS_DONEINPROC_TOKEN:
      {
         tds_process_end(tds, marker, &more_results, &cancelled);
         if (!more_results) {
            tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_default_tokens() setting state to COMPLETED\n");
            tds->state=TDS_COMPLETED;
         }
         break;
      }
      case TDS_124_TOKEN:
         tds_get_n(tds,NULL,8);
         break;
      case TDS_RET_STAT_TOKEN:
	tds->has_status=1;
	tds->ret_status=tds_get_int(tds);
        break;
      case TDS_ERR_TOKEN:
      case TDS_MSG_TOKEN:
      case TDS_EED_TOKEN:
         tds_process_msg(tds,marker);
         break;
      case TDS_CAP_TOKEN:
	 tok_size = tds_get_smallint(tds);
         tds_get_n(tds,tds->capabilities,tok_size > TDS_MAX_CAPABILITY ? TDS_MAX_CAPABILITY : tok_size);
         break;
      case TDS_LOGIN_ACK_TOKEN:
         tds_get_n(tds,NULL,tds_get_smallint(tds));
         break;
      case TDS_ORDER_BY_TOKEN:
         order_len = tds_get_smallint(tds);
         tds_get_n(tds, NULL, order_len);
         break;
	/* PARAM_TOKEN can be returned inserting text in db, to return new timestamp */
	case TDS_PARAM_TOKEN:
		tds_unget_byte(tds);
		tds_process_param_result_tokens(tds);
		break;
      case TDS_CONTROL_TOKEN: 
         tds_get_n(tds, NULL, tds_get_smallint(tds));
         break;
      case TDS7_RESULT_TOKEN:
         tds7_process_result(tds); 
         break;
      case TDS_RESULT_TOKEN:
         tds_process_result(tds); 
         break;
      case TDS_COL_NAME_TOKEN:
         tds_process_col_name(tds); 
         break;
      case TDS_ROW_TOKEN:
         tds_process_row(tds); 
         break;
      case TDS5_DYN_TOKEN:
      case TDS5_PARAMFMT_TOKEN:
      case TDS5_PARAMS_TOKEN:
	 tdsdump_log(TDS_DBG_WARN, "eating token %d\n",marker);
         tds_get_n(tds, NULL, tds_get_smallint(tds));
         break;
      default:
	 tdsdump_log(TDS_DBG_ERROR, "Unknown marker: %d(%x)!!\n",marker,(unsigned char)marker); 
         return TDS_FAIL;
   }	
   return TDS_SUCCEED;
}	

static int
tds_set_spid(TDSSOCKET *tds)
{
TDS_INT result_type;
TDS_INT row_type;
TDS_INT compute_id;

   if (tds_submit_query(tds, "select @@spid") != TDS_SUCCEED) {
      return TDS_FAIL;
   }
   if (tds_process_result_tokens(tds, &result_type) != TDS_SUCCEED) {
      return TDS_FAIL;
   }
   if (tds->res_info->num_cols != 1) {
      return TDS_FAIL;
   }
   if (tds->res_info->columns[0]->column_type != SYBINT2) {
      return TDS_FAIL;
   }
   if (tds_process_row_tokens(tds, &row_type, &compute_id) != TDS_SUCCEED) {
      return TDS_FAIL;
   }
   tds->spid = *((TDS_USMALLINT *) (tds->res_info->current_row
                                    + tds->res_info->columns[0]->column_offset));
   if (tds_process_row_tokens(tds, &row_type, &compute_id) != TDS_NO_MORE_ROWS) {
      return TDS_FAIL;
   }
   if (tds_process_result_tokens(tds, &result_type) != TDS_NO_MORE_RESULTS) {
      return TDS_FAIL;
   }
   return TDS_SUCCEED;
}

/**
 * tds_process_login_tokens() is called after sending the login packet 
 * to the server.  It returns the success or failure of the login 
 * dependent on the protocol version. 4.2 sends an ACK token only when
 * successful, TDS 5.0 sends it always with a success byte within
 */
int tds_process_login_tokens(TDSSOCKET *tds)
{
int succeed=0;
int marker;
int len, product_name_len;
unsigned char major_ver, minor_ver;
unsigned char ack;
TDS_UINT product_version;
#ifdef WORDS_BIGENDIAN
char *tmpbuf;
#endif

	tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_login_tokens()\n");
	/* get_incoming(tds->s); */
	do {
		marker=tds_get_byte(tds);
		switch(marker) {
			case TDS_AUTH_TOKEN:
				tds_process_auth(tds);
				break;
			case TDS_LOGIN_ACK_TOKEN:
				/* TODO function */
				len = tds_get_smallint(tds);
				ack = tds_get_byte(tds);
				major_ver = tds_get_byte(tds);
				minor_ver = tds_get_byte(tds);
				tds_get_n(tds, NULL, 2);
				product_name_len = tds_get_byte(tds);
				product_version = 0;
				/* TODO get server product and string */
				len -= 10;
				if (major_ver >= 7) {
					product_version |= 0x80000000u;
					tds_get_n(tds, NULL, len);
				} else if (major_ver >= 5) {
					tds_get_n(tds, NULL, len);
				} else {
					char buf[32+1];
					int l = len > 32 ? 32 : len;
					tds_get_n(tds, buf, l);
					buf[l] = 0;
					if (strstr(buf,"Microsoft"))
						product_version |= 0x80000000u;
					if (l < len)
						tds_get_n(tds, NULL, len - l);
				}
				product_version |= 
					((TDS_UINT)tds_get_byte(tds)) << 24;
				product_version |= 
					((TDS_UINT)tds_get_byte(tds)) << 16;
				product_version |= 
					((TDS_UINT)tds_get_byte(tds)) << 8;
				product_version |= tds_get_byte(tds);
				tds->product_version = product_version;
#ifdef WORDS_BIGENDIAN
/*
				if (major_ver==7) {
					tds->broken_dates=1;
				}
*/
#endif
/*
				tmpbuf = (char *) malloc(len);
				tds_get_n(tds, tmpbuf, len);
				tdsdump_log(TDS_DBG_INFO1, "%L login ack marker = %d\n%D\n", marker, tmpbuf, len);
				free(tmpbuf);
*/
				/* TDS 5.0 reports 5 on success 6 on failure
				** TDS 4.2 reports 1 on success and is not
				** present on failure */
				if (ack==5 || ack==1) succeed=TDS_SUCCEED;
				break;
			default:
				if (tds_process_default_tokens(tds,marker)==TDS_FAIL)
					return TDS_FAIL;
				break;
		}
	} while (marker!=TDS_DONE_TOKEN);
	tds->spid = tds->rows_affected;
	if (tds->spid == 0) {
		if (tds_set_spid(tds) != TDS_SUCCEED) {
			tdsdump_log(TDS_DBG_ERROR, "%L tds_set_spid() failed\n");
		}
	}
	tdsdump_log(TDS_DBG_FUNC, "%L leaving tds_process_login_tokens() returning %d\n",succeed);
	return succeed;
}

static int tds_process_auth(TDSSOCKET *tds)
{
int pdu_size, ntlm_size;
char nonce[10];
/* char domain[30]; */
int where = 0;

	pdu_size = tds_get_smallint(tds);
	tdsdump_log(TDS_DBG_INFO1, "TDS_AUTH_TOKEN PDU size %d\n", pdu_size);

	tds_get_n(tds, NULL, 8); /* NTLMSSP\0 */ 
	where += 8;
	tds_get_int(tds); /* sequence -> 2 */
	where += 4;
	tds_get_n(tds, NULL, 4); /* domain len (2 time) */
	where += 4;
	ntlm_size = tds_get_int(tds); /* size of remainder of ntlmssp packet */
	where += 4;
	tdsdump_log(TDS_DBG_INFO1, "TDS_AUTH_TOKEN NTLMSSP size %d\n", ntlm_size);
	tds_get_n(tds, NULL, 4); /* flags */
	where += 4;
	tds_get_n(tds, nonce, 8); 
	where += 8;
	tdsdump_log(TDS_DBG_INFO1, "TDS_AUTH_TOKEN nonce\n");
	tdsdump_dump_buf(nonce, 8);
	tds_get_n(tds, NULL, 8); 
	where += 8;
	
	/*
	tds_get_ntstring(tds, domain, 30); 
	tdsdump_log(TDS_DBG_INFO1, "TDS_AUTH_TOKEN domain %s\n", domain);
	where += strlen(domain);
	*/

	tds_get_n(tds, NULL, pdu_size - where); 
	tdsdump_log(TDS_DBG_INFO1,"%L Draining %d bytes\n", pdu_size - where);

	tds7_send_auth(tds, nonce);

	return TDS_SUCCEED;
}

/**
 * process TDS result-type message streams.
 * tds_process_result_tokens() is called after submitting a query with
 * tds_submit_query() and is responsible for calling the routines to
 * populate tds->res_info if appropriate (some query have no result sets)
 * @param tds A pointer to the TDSSOCKET structure managing a client/server operation.
 * @param result_type A pointer to an integer variable which 
 *        tds_process_result_tokens sets to indicate the current type of result.
 *  @par
 *  <b>Values that indicate command status</b>
 *  <table>
 *   <tr><td>TDS_CMD_DONE</td><td>The results of a  command have been completely processed</td></tr>
 *   <tr><td>TDS_CMD_FAIL</td><td>The server encountered an error while executing a command</td></tr>
 *  </table>
 *  <b>Values that indicate results information is available</b>
 *  <table><tr>
 *    <td>TDS_ROWFMT_RESULT</td><td>Regular Data format information</td>
 *    <td>tds->res_info now contains the result details ; tds->curr_resinfo now points to that data</td>
 *   </tr><tr>
 *    <td>TDS_COMPUTEFMT_ RESULT</td><td>Compute data format information</td>
 *    <td>tds->comp_info now contains the result data; tds->curr_resinfo now points to that data</td>
 *   </tr><tr>
 *    <td>TDS_DESCRIBE_RESULT</td><td></td>
 *    <td></td>
 *  </tr></table>
 *  <b>Values that indicate data is available</b>
 *  <table><tr>
 *   <td><b>Value</b></td><td><b>Meaning</b></td><td><b>Information returned</b></td>
 *   </tr><tr>
 *    <td>TDS_ROW_RESULT</td><td>Regular row results</td>
 *    <td>1 or more rows of regular data can now be retrieved</td>
 *   </tr><tr>
 *    <td>TDS_COMPUTE_RESULT</td><td>Compute row results</td>
 *    <td>A single row of compute data can now be retrieved</td>
 *   </tr><tr>
 *    <td>TDS_PARAM_RESULT</td><td>Return parameter results</td>
 *    <td></td>
 *   </tr><tr>
 *    <td>TDS_STATUS_RESULT</td><td>Stored procedure status results</td>
 *    <td>tds->ret_status contain the returned code</td>
 *  </tr></table>
 * @todo Complete TDS_DESCRIBE_RESULT and TDS_PARAM_RESULT description
 * @retval TDS_SUCCEED if a result set is available for processing.
 * @retval TDS_NO_MORE_RESULTS if all results have been completely processed.
 * @par Examples
 * The following code is cut from ct_results(), the ct-library function
 * @include token1.c
 */
int tds_process_result_tokens(TDSSOCKET *tds, TDS_INT *result_type)
{
int marker;
int more_results = 0;
int cancelled;

	if (tds->state==TDS_COMPLETED) {
        tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_result_tokens() state is COMPLETED\n");
		return TDS_NO_MORE_RESULTS;
	}

	for(;;) {

		marker=tds_get_byte(tds);
		tdsdump_log(TDS_DBG_INFO1, "%L processing result tokens.  marker is  %x\n", marker);

		switch(marker) {
			case TDS_ERR_TOKEN:
			case TDS_MSG_TOKEN:
			case TDS_EED_TOKEN:
                tds_process_msg(tds,marker);
				break;
			case TDS7_RESULT_TOKEN:
					tds7_process_result(tds);
					*result_type = TDS_ROWFMT_RESULT;
					return TDS_SUCCEED;
					break;
			case TDS_RESULT_TOKEN:
					tds_process_result(tds);
					*result_type = TDS_ROWFMT_RESULT;
					return TDS_SUCCEED;
				break;
			case TDS_COL_NAME_TOKEN:
					tds_process_col_name(tds);
                break;
           case TDS_COL_INFO_TOKEN:
                tds_process_col_info(tds); 
					*result_type = TDS_ROWFMT_RESULT;
					return TDS_SUCCEED;
					break;
           case TDS_PARAM_TOKEN: 
				tds_unget_byte(tds);
				tds_process_param_result_tokens(tds);
				*result_type = TDS_PARAM_RESULT;
				return TDS_SUCCEED;
                break;
           case TDS_COMPUTE_NAMES_TOKEN:
                tds_process_compute_names(tds); 
                break;
           case TDS_COMPUTE_RESULT_TOKEN:
                tds_process_compute_result(tds); 
				*result_type = TDS_COMPUTEFMT_RESULT;
				return TDS_SUCCEED;
                break;
           case TDS7_COMPUTE_RESULT_TOKEN:
                tds7_process_compute_result(tds); 
				*result_type = TDS_COMPUTEFMT_RESULT;
				return TDS_SUCCEED;
				break;
			case TDS_ROW_TOKEN:
                /* overstepped the mark...*/
				*result_type = TDS_ROW_RESULT;
				tds->res_info->rows_exist=1;
                tds->curr_resinfo = tds->res_info;
				tds_unget_byte(tds);
				return TDS_SUCCEED;
                break;
            case TDS_CMP_ROW_TOKEN:
				*result_type = TDS_COMPUTE_RESULT;
                tds->res_info->rows_exist = 1;
                tds_unget_byte(tds);
				return TDS_SUCCEED;
                break;
			case TDS_RET_STAT_TOKEN:
				tds->has_status=1;
				tds->ret_status=tds_get_int(tds);
				*result_type = TDS_STATUS_RESULT;
				return TDS_SUCCEED;
				break;
			case TDS5_DYN_TOKEN:
				tds->cur_dyn_elem = tds_process_dynamic(tds);
				break;
			case TDS5_PARAMFMT_TOKEN:
				tds_process_dyn_result(tds);
				*result_type = TDS_DESCRIBE_RESULT;
				return TDS_SUCCEED;
				break;
			case TDS_DONE_TOKEN:
			case TDS_DONEPROC_TOKEN:
			case TDS_DONEINPROC_TOKEN:
                if (tds_process_end(tds, marker, &more_results, &cancelled) == TDS_SUCCEED)
					*result_type = TDS_CMD_DONE;
                else
					*result_type = TDS_CMD_FAIL;
				return TDS_SUCCEED;
				break;
			default:
                if (tds_process_default_tokens(tds,marker) == TDS_FAIL) {
					return TDS_FAIL;
                }
				break;
		}
    } 
}

/**
 * process TDS row-type message streams.
 * tds_process_row_tokens() is called once a result set has been obtained
 * with tds_process_result_tokens(). It calls tds_process_row() to copy
 * data into the row buffer.
 * @param tds A pointer to the TDSSOCKET structure managing a 
 *    client/server operation.
 * @param rowtype A pointer to an integer variable which 
 *    tds_process_row_tokens sets to indicate the current type of row
 * @param computeid A pointer to an integer variable which 
 *    tds_process_row_tokens sets to identify the compute_id of the row 
 *    being returned. A compute row is a row that is generated by a 
 *    compute clause. The compute_id matches the number of the compute row 
 *    that was read; the first compute row is 1, the second is 2, and so forth.
 * @par Possible values of *rowtype
 *        @li @c TDS_REG_ROW      A regular data row
 *        @li @c TDS_COMP_ROW     A row of compute data
 *        @li @c TDS_NO_MORE_ROWS There are no more rows of data in this result set
 * @retval TDS_SUCCEED A row of data is available for processing.
 * @retval TDS_NO_MORE_ROWS All rows have been completely processed.
 * @retval TDS_FAIL An unexpected error occurred
 * @par Examples
 * The following code is cut from dbnextrow(), the db-library function
 * @include token2.c
 */
int tds_process_row_tokens(TDSSOCKET *tds, TDS_INT *rowtype, TDS_INT *computeid)
{
int marker;
int   more_results;
int   cancelled;
TDS_SMALLINT compute_id;
TDSRESULTINFO *info;
int i;

	if (tds->state==TDS_COMPLETED) {
        *rowtype = TDS_NO_MORE_ROWS;
        tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_row_tokens() state is COMPLETED\n");
		return TDS_NO_MORE_ROWS;
	}

	while (1) {

		marker=tds_get_byte(tds);
		tdsdump_log(TDS_DBG_INFO1, "%L processing row tokens.  marker is  %x\n", marker);

		switch(marker) {
			case TDS_RESULT_TOKEN:
			case TDS7_RESULT_TOKEN:

				tds_unget_byte(tds);
                *rowtype = TDS_NO_MORE_ROWS;
				return TDS_NO_MORE_ROWS;

			case TDS_ROW_TOKEN:

				tds_process_row(tds);
                *rowtype = TDS_REG_ROW;
                tds->curr_resinfo = tds->res_info;
		        return TDS_SUCCEED;

            case TDS_CMP_ROW_TOKEN:

                *rowtype = TDS_COMP_ROW;
                compute_id = tds_get_smallint(tds);
            
                for ( i = 0;  ; ++i ) {
                    if (i >= tds->num_comp_info)
                       return TDS_FAIL;
                    info = tds->comp_info[i];
                    if (info->computeid == compute_id)
                       break;
                }

                tds->curr_resinfo = info;
                tds_process_compute(tds); 
                if (computeid)
                   *computeid = compute_id;
				return TDS_SUCCEED;

			case TDS_DONE_TOKEN:
			case TDS_DONEPROC_TOKEN:
			case TDS_DONEINPROC_TOKEN:

				tds_process_end(tds, marker, &more_results, &cancelled);
                *rowtype = TDS_NO_MORE_ROWS;
				return TDS_NO_MORE_ROWS;

			default:
				if (tds_process_default_tokens(tds,marker)==TDS_FAIL)
					return TDS_FAIL;
				break;
		}
	} 
	return TDS_SUCCEED;
}
/**
 * tds_process_col_name() is one half of the result set under TDS 4.2
 * it contains all the column names, a TDS_COLINFO_TOKEN should 
 * immediately follow this token with the datatype/size information
 * This is a 4.2 only function
 */
static int tds_process_col_name(TDSSOCKET *tds)
{
int hdrsize, len=0;
int col,num_cols=0;
struct tmp_col_struct {
	char *column_name;
	int column_namelen;
	struct tmp_col_struct *next;
};
struct tmp_col_struct *head=NULL, *cur=NULL, *prev;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;

	hdrsize = tds_get_smallint(tds);

	/* this is a little messy...TDS 5.0 gives the number of columns
	** upfront, while in TDS 4.2, you're expected to figure it out
	** by the size of the message. So, I use a link list to get the
	** colum names and then allocate the result structure, copy
	** and delete the linked list */
	while (len<hdrsize) {
		prev = cur;
		cur = (struct tmp_col_struct *) 
			malloc(sizeof (struct tmp_col_struct));
		if (prev) prev->next=cur;
		if (!head) head = cur;

		cur->column_namelen = tds_get_byte(tds);
		cur->column_name = (char *) malloc(cur->column_namelen+1);
		tds_get_n(tds,cur->column_name, cur->column_namelen);
		cur->column_name[cur->column_namelen]='\0';
		cur->next=NULL;

		len += cur->column_namelen + 1;
		num_cols++;
	}

	/* free results/computes/params etc... */
	tds_free_all_results(tds);

	tds->res_info = tds_alloc_results(num_cols);
	info = tds->res_info;
    tds->curr_resinfo = tds->res_info;
	/* tell the upper layers we are processing results */
	tds->state = TDS_PENDING; 
	cur=head;
	for (col=0;col<info->num_cols;col++) 
	{
		curcol=info->columns[col];
		curcol->column_namelen = cur->column_namelen;
		strncpy(curcol->column_name, cur->column_name, 
			sizeof(curcol->column_name));
		prev=cur; cur=cur->next;
		free(prev->column_name);
		free(prev);
	}
	return TDS_SUCCEED;
} 

/**
 * tds_process_col_info() is the other half of result set processing
 * under TDS 4.2. It follows tds_process_col_name(). It contains all the 
 * column type and size information.
 * This is a 4.2 only function
 */
static int tds_process_col_info(TDSSOCKET *tds)
{
int col,hdrsize;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;
TDS_SMALLINT tabnamesize;
int bytes_read = 0;
int rest;
int remainder;
char ci_flags[4];

	hdrsize = tds_get_smallint(tds);

	info = tds->res_info;
	for (col=0; col<info->num_cols; col++) 
	{
		curcol=info->columns[col];
		/* Used to ignore next 4 bytes, now we'll actually parse (some of)
			the data in them. (mlilback, 11/7/01) */
		tds_get_n(tds, ci_flags, 4);
		curcol->column_nullable = ci_flags[3] & 0x01;
		curcol->column_writeable = (ci_flags[3] & 0x08) > 0;
		curcol->column_identity = (ci_flags[3] & 0x10) > 0;
		/* on with our regularly scheduled code (mlilback, 11/7/01) */
		curcol->column_type = tds_get_byte(tds);

		curcol->column_varint_size  = tds_get_varint_size(curcol->column_type);
		tdsdump_log(TDS_DBG_INFO1, "%L processing result. type = %d, varint_size %d\n", 
		curcol->column_type, curcol->column_varint_size);

		switch(curcol->column_varint_size) {
			case 4: 
				curcol->column_size = tds_get_int(tds);
				/* junk the table name -- for now */
				tabnamesize = tds_get_smallint(tds);
				tds_get_n(tds,NULL,tabnamesize);
				bytes_read += 5+4+2+tabnamesize;
				break;
			case 1: 
				curcol->column_size = tds_get_byte(tds);
				bytes_read += 5+1;
				break;
			case 0: 
				curcol->column_size = tds_get_size_by_type(curcol->column_type);
				bytes_read += 5+0;
				break;
		}

		if (is_blob_type(curcol->column_type)) {
			curcol->column_offset = info->row_size;
		} else {
			curcol->column_offset = info->row_size;
			info->row_size += curcol->column_size + 1;
		}
		remainder = info->row_size % ALIGN_SIZE; 
		if (remainder) info->row_size += (ALIGN_SIZE - remainder);
	}

	/* get the rest of the bytes */
	rest = hdrsize - bytes_read;
	if (rest > 0) {
		tdsdump_log(TDS_DBG_INFO1,"NOTE:tds_process_col_info: draining %d bytes\n", rest);
		tds_get_n(tds, NULL, rest);
	}

	info->current_row = tds_alloc_row(info);

	return TDS_SUCCEED;
}

/**
 * tds_process_param_result() processes output parameters of a stored 
 * procedure. This differs from regular row/compute results in that there
 * is no total number of parameters given, they just show up singley.
 */
static int tds_process_param_result(TDSSOCKET *tds)
{
int hdrsize;
TDSCOLINFO *curparam;
TDSPARAMINFO *info;
int i;

	hdrsize = tds_get_smallint(tds);
	info = tds_alloc_param_result(tds->param_info);
	tds->param_info = info;
	curparam = info->columns[info->num_cols - 1];

	/* FIXME check support for tds7+ (seem to use same format of tds5 for data...)
	 * perhaps varint_size can be 2 or collation can be specified ?? */
	tds_get_data_info(tds,curparam);

	curparam->column_cur_size = curparam->column_size; /* needed ?? */

	tds_alloc_param_row(info, curparam);

	i =  tds_get_data(tds,curparam,info->current_row,info->num_cols - 1);
	/* is this the id of our prepared statement ?? */
	if (IS_TDS7_PLUS(tds) && tds->cur_dyn_elem && tds->dyns[tds->cur_dyn_elem]->num_id == 0 && info->num_cols == 1) {
		tds->dyns[tds->cur_dyn_elem]->num_id = *(TDS_INT*)(info->current_row+curparam->column_offset);
	}
	return i;
}
static int tds_process_param_result_tokens(TDSSOCKET *tds)
{
int marker;

	while ((marker=tds_get_byte(tds))==TDS_PARAM_TOKEN) {
		tds_process_param_result(tds);
	}
	tds_unget_byte(tds);
	return TDS_SUCCEED;
}
/**
 * tds_process_compute_result() processes compute result sets.  These functions
 * need work but since they get little use, nobody has complained!
 * It is very similar to normal result sets.
 */
static int tds_process_compute_result(TDSSOCKET *tds)
{
int hdrsize;
int col, num_cols;
TDS_TINYINT by_cols = 0;
TDS_TINYINT *cur_by_col;
TDS_SMALLINT compute_id = 0;
TDS_SMALLINT localelen = 0;
TDSCOLINFO *curcol;
TDSCOMPUTEINFO *info;
int remainder;
int             i;


	hdrsize = tds_get_smallint(tds);

    /* compute statement id which this relates */
    /* to. You can have more than one compute  */
    /* statement in a SQL statement            */

    compute_id = tds_get_smallint(tds);   

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. compute_id = %d\n", compute_id);

    /* number of compute columns returned - so */
    /* COMPUTE SUM(x), AVG(x)... would return  */
    /* num_cols = 2                            */

	num_cols = tds_get_byte(tds);

	for ( i = 0; ; ++i ) {
		if (i >= tds->num_comp_info)
			return TDS_FAIL;
        info = tds->comp_info[i];
        tdsdump_log (TDS_DBG_FUNC, "%L in dbaltcolid() found computeid = %d\n", info->computeid);
        if (info->computeid == compute_id)
           break;
	}

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. num_cols = %d\n", num_cols);

	for (col = 0; col < num_cols; col++) 
{
        tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 2\n");
		curcol = info->columns[col];

        curcol->column_operator = tds_get_byte(tds);
		curcol->column_operand  = tds_get_byte(tds);  

        /* if no name has been defined for the compute column, */
        /* put in "max", "avg" etc.                            */

        if (curcol->column_namelen == 0) {
           strcpy(curcol->column_name, tds_prtype(curcol->column_operator));
           curcol->column_namelen = strlen(curcol->column_name);
        }

		/*  User defined data type of the column */
		curcol->column_usertype = tds_get_int(tds);  

	curcol->column_type = tds_get_byte(tds); 
		
	curcol->column_type_save = curcol->column_type;

	curcol->column_varint_size  = tds_get_varint_size(curcol->column_type);

	switch(curcol->column_varint_size) {
		case 4: 
			curcol->column_size = tds_get_int(tds);
			break;
		case 2: 
			curcol->column_size = tds_get_smallint(tds);
			break;
		case 1: 
			curcol->column_size = tds_get_byte(tds);
			break;
		case 0: 
			curcol->column_size = tds_get_size_by_type(curcol->column_type);
			break;
	}
        tdsdump_log(TDS_DBG_INFO1, "%L processing result. column_size %d\n", curcol->column_size);

		curcol->column_type = tds_get_cardinal_type(curcol->column_type);

		localelen = tds_get_byte(tds);

        if (localelen > 0 ) {
			tds_get_n(tds, NULL, localelen);
        }

		curcol->column_offset = info->row_size;
		if (!is_blob_type(curcol->column_type)) {
			info->row_size += curcol->column_size + 1;
		}
	if (is_numeric_type(curcol->column_type)) {
            info->row_size += sizeof(TDS_NUMERIC) + 1;
	}

		/* actually this 4 should be a machine dependent #define */
		remainder = info->row_size % ALIGN_SIZE;
		if (remainder) info->row_size += (ALIGN_SIZE - remainder);
	}

    by_cols = tds_get_byte(tds);       

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds compute result. by_cols = %d\n", by_cols);

	if (by_cols) {
		info->bycolumns = (TDS_TINYINT *) malloc(by_cols);
		memset(info->bycolumns,'\0', by_cols);
	}
    info->by_cols = by_cols;

    cur_by_col = info->bycolumns;
	for (col = 0; col < by_cols; col++) 
	{
        *cur_by_col = tds_get_byte(tds);
        cur_by_col++;
	}

	info->current_row = tds_alloc_compute_row(info);

	return TDS_SUCCEED;
}

/**
 * Read data information from wire
 * @param curcol column where to store information
 */
static int
tds7_get_data_info(TDSSOCKET *tds,TDSCOLINFO *curcol)
{
TDS_SMALLINT tabnamelen;
TDS_SMALLINT collate_type;
int colnamelen;

	/*  User defined data type of the column */
	curcol->column_usertype = tds_get_smallint(tds);  

	curcol->column_flags = tds_get_smallint(tds);     /*  Flags */

	curcol->column_nullable  =  curcol->column_flags & 0x01;
	curcol->column_writeable = (curcol->column_flags & 0x08) > 0;
	curcol->column_identity  = (curcol->column_flags & 0x10) > 0;

	curcol->column_type = tds_get_byte(tds); 
		
	curcol->column_type_save = curcol->column_type;
	collate_type = is_collate_type(curcol->column_type);
	curcol->column_varint_size  = tds_get_varint_size(curcol->column_type);

	switch(curcol->column_varint_size) {
		case 4: 
			curcol->column_size = tds_get_int(tds);
			break;
		case 2: 
			curcol->column_size = tds_get_smallint(tds);
			break;
		case 1: 
			curcol->column_size = tds_get_byte(tds);
			break;
		case 0: 
			curcol->column_size = tds_get_size_by_type(curcol->column_type);
			break;
	}

	curcol->column_unicodedata = 0;

	if (is_unicode(curcol->column_type))
		curcol->column_unicodedata = 1;

	curcol->column_type = tds_get_cardinal_type(curcol->column_type);

	/* numeric and decimal have extra info */
	if (is_numeric_type(curcol->column_type)) {
		curcol->column_prec = tds_get_byte(tds); /* precision */
		curcol->column_scale = tds_get_byte(tds); /* scale */
	}

	if (IS_TDS80(tds)) {
		if (collate_type) 
			tds_get_n(tds, curcol->column_collation, 5);
	}

	if (is_blob_type(curcol->column_type)) {
		tabnamelen = tds_get_smallint(tds);
		tds_get_string(tds, NULL, tabnamelen);
	}

	/* under 7.0 lengths are number of characters not 
	** number of bytes...tds_get_string handles this */
	colnamelen = tds_get_byte(tds);
	tds_get_string(tds,curcol->column_name, colnamelen);

	return TDS_SUCCEED;
}

/**
 * tds7_process_result() is the TDS 7.0 result set processing routine.  It 
 * is responsible for populating the tds->res_info structure.
 * This is a TDS 7.0 only function
 */
static int tds7_process_result(TDSSOCKET *tds)
{
int col, num_cols;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;
int remainder;

	tds_free_all_results(tds);

	/* read number of columns and allocate the columns structure */
	num_cols = tds_get_smallint(tds);
	tds->res_info = tds_alloc_results(num_cols);
	info = tds->res_info;
    tds->curr_resinfo = tds->res_info;

	/* tell the upper layers we are processing results */
	tds->state = TDS_PENDING; 

	/* loop through the columns populating COLINFO struct from
	** server response */
	for (col=0;col<num_cols;col++) {

		curcol = info->columns[col];

		tds7_get_data_info(tds,curcol);
		
		/* the column_offset is the offset into the row buffer
		** where this column begins, text types are no longer
		** stored in the row buffer because the max size can
		** be too large (2gig) to allocate */
		curcol->column_offset = info->row_size;
		if (!is_blob_type(curcol->column_type)) {
			info->row_size += curcol->column_size + 1;
		}
		if (is_numeric_type(curcol->column_type)) {
			info->row_size += sizeof(TDS_NUMERIC) + 1;
		}
		
		remainder = info->row_size % ALIGN_SIZE;
		if (remainder) info->row_size += (ALIGN_SIZE - remainder);
		
	}

	/* all done now allocate a row for tds_process_row to use */
	info->current_row = tds_alloc_row(info);

	return TDS_SUCCEED;

}

/**
 * Read data information from wire
 * @param curcol column where to store information
 */
static int 
tds_get_data_info(TDSSOCKET *tds,TDSCOLINFO *curcol)
{
int colnamelen;

	colnamelen = tds_get_byte(tds);
	tds_get_string(tds,curcol->column_name, colnamelen);
	curcol->column_name[colnamelen]='\0';

	curcol->column_flags = tds_get_byte(tds);     /*  Flags */
	curcol->column_nullable  = (curcol->column_flags & 0x20) > 1;

	curcol->column_usertype = tds_get_int(tds);
	curcol->column_type = tds_get_byte(tds);

	curcol->column_varint_size  = tds_get_varint_size(curcol->column_type);

	tdsdump_log(TDS_DBG_INFO1, "%L processing result. type = %d, varint_size %d\n", curcol->column_type, curcol->column_varint_size);
	switch(curcol->column_varint_size) {
		case 4: 
			curcol->column_size = tds_get_int(tds);
			/* junk the table name -- for now */
			tds_get_n(tds,NULL,tds_get_smallint(tds));
			break;
		case 1: 
			curcol->column_size = tds_get_byte(tds);
			break;
		case 0: 
			curcol->column_size = tds_get_size_by_type(curcol->column_type);
			break;
		/* FIXME can varint_size be 2 ?? */
	}
	tdsdump_log(TDS_DBG_INFO1, "%L processing result. column_size %d\n", curcol->column_size);

	/* numeric and decimal have extra info */
	if (is_numeric_type(curcol->column_type)) {
		curcol->column_prec = tds_get_byte(tds); /* precision */
		curcol->column_scale = tds_get_byte(tds); /* scale */
	}

	return TDS_SUCCEED;
}

/**
 * tds_process_result() is the TDS 5.0 result set processing routine.  It 
 * is responsible for populating the tds->res_info structure.
 * This is a TDS 5.0 only function
 */
static int tds_process_result(TDSSOCKET *tds)
{
int hdrsize;
int col, num_cols;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;
int remainder;

	tds_free_all_results(tds);

	hdrsize = tds_get_smallint(tds);

	/* read number of columns and allocate the columns structure */
	num_cols = tds_get_smallint(tds);
	tds->res_info = tds_alloc_results(num_cols);
	info = tds->res_info;
    tds->curr_resinfo = tds->res_info;

	/* tell the upper layers we are processing results */
	tds->state = TDS_PENDING; 

	/* loop through the columns populating COLINFO struct from
	** server response */
	for (col=0;col<info->num_cols;col++) 
	{
		curcol=info->columns[col];

		tds_get_data_info(tds,curcol);
		
		/* skip locale information */
		/* NOTE do not put into tds_get_data_info, param do not have locale information */
		tds_get_byte(tds);

		curcol->column_offset = info->row_size;
		if (is_numeric_type(curcol->column_type)) {
			info->row_size += sizeof(TDS_NUMERIC) + 1;
		} else if (!is_blob_type(curcol->column_type)) {
			info->row_size += curcol->column_size + 1;
		}

		remainder = info->row_size % ALIGN_SIZE; 
		if (remainder) info->row_size += (ALIGN_SIZE - remainder);
	}
	info->current_row = tds_alloc_row(info);

	return TDS_SUCCEED;
}

/*
** tds_process_compute() processes compute rows and places them in the row
** buffer.  
*/
static int tds_process_compute(TDSSOCKET *tds)
{
int i;
TDSCOLINFO *curcol;
TDSCOMPUTEINFO *info;

    info = tds->curr_resinfo;
	
	for (i = 0; i < info->num_cols; i++) {
		curcol = info->columns[i];
		if (tds_get_data(tds,curcol,info->current_row,i) != TDS_SUCCEED)
			return TDS_FAIL;
	}
	return TDS_SUCCEED;

}


/**
 * Read a data from wire
 * @param curcol column where store column information
 * @param pointer to row data to store information
 * @param i column position in current_row
 * @return TDS_FAIL on error or TDS_SUCCEED
 */
static int 
tds_get_data(TDSSOCKET *tds,TDSCOLINFO *curcol,unsigned char *current_row, int i)
{
unsigned char *dest;
TDS_NUMERIC *num;
TDS_VARBINARY *varbin;
int len,colsize;

	tdsdump_log(TDS_DBG_INFO1, "%L processing row.  column is %d varint size = %d\n", i, curcol->column_varint_size);
	switch (curcol->column_varint_size) {
		case 4: /* Its a BLOB... */
			len = tds_get_byte(tds);
			if (len == 16) { /*  Jeff's hack */
				tds_get_n(tds,curcol->column_textptr,16);
				tds_get_n(tds,curcol->column_timestamp,8);
				colsize = tds_get_int(tds);
			} else {
				colsize = 0;
			}
			break;
		case 2: 
			colsize = tds_get_smallint(tds);
			/* handle empty no-NULL string*/
			if (colsize == 0) {
				tds_clr_null(current_row, i);
				curcol->column_cur_size = 0;
				return TDS_SUCCEED;
			}
			if (colsize == -1)
				colsize=0;
			break;
		case 1: 
			colsize = tds_get_byte(tds);
			break;
		case 0: 
			colsize = tds_get_size_by_type(curcol->column_type);
			break;
		default:
			colsize = 0;
			break;
	}

	tdsdump_log(TDS_DBG_INFO1, "%L processing row.  column size is %d \n", colsize);
	/* set NULL flag in the row buffer */
	if (colsize==0) {
		tds_set_null(current_row, i);
		return TDS_SUCCEED;
	}

	tds_clr_null(current_row, i);

	if (is_numeric_type(curcol->column_type)) {

		/* 
		** handling NUMERIC datatypes: 
		** since these can be passed around independent
		** of the original column they were from, I decided
		** to embed the TDS_NUMERIC datatype in the row buffer
		** instead of using the wire representation even though
		** it uses a few more bytes
		*/
		num = (TDS_NUMERIC *) &(current_row[curcol->column_offset]);
		memset(num, '\0', sizeof(TDS_NUMERIC));
		num->precision = curcol->column_prec;
		num->scale = curcol->column_scale;
		
		/* server is going to crash freetds ??*/
		if (colsize > sizeof(num->array))
			return TDS_FAIL;
		tds_get_n(tds,num->array,colsize);

		/* corrected colsize for column_cur_size */
		colsize = sizeof(TDS_NUMERIC);
		if (IS_TDS7_PLUS(tds)) 
		{
			tdsdump_log(TDS_DBG_INFO1, "%L swapping numeric data...\n");
			tds_swap_datatype(tds_get_conversion_type(curcol->column_type, colsize), (unsigned char *)num );
		}

	} else if (curcol->column_type == SYBVARBINARY) {
		varbin = (TDS_VARBINARY *) &(current_row[curcol->column_offset]);
		varbin->len = colsize;
		/*  It is important to re-zero out the whole
		column_size varbin array here because the result
		of the query ("colsize") may be any number of
		bytes <= column_size (because the result
		will be truncated if the rest of the data
		in the column would be all zeros).  */
		memset(varbin->array,'\0',curcol->column_size);

		/* server is going to crash freetds ??*/
		if (colsize > sizeof(varbin->array))
			return TDS_FAIL;
		tds_get_n(tds,varbin->array,colsize);
	} else if (is_blob_type(curcol->column_type)) {
		if (curcol->column_unicodedata) colsize /= 2;
		if (curcol->column_textvalue == NULL) {
			curcol->column_textvalue = malloc(colsize+1); /* FIXME +1 needed by tds_get_string */
		} else {
			curcol->column_textvalue = realloc(curcol->column_textvalue,colsize+1); /* FIXME +1 needed by tds_get_string */
		}
		if (curcol->column_textvalue == NULL) {
			return TDS_FAIL;
		}
		curcol->column_cur_size = colsize;
		if (curcol->column_unicodedata) {
			tds_get_string(tds,curcol->column_textvalue,colsize);
		} else {
			tds_get_n(tds,curcol->column_textvalue,colsize);
		}
	} else {
		dest = &(current_row[curcol->column_offset]);
		if (curcol->column_unicodedata) {
			colsize /= 2;
			/* server is going to crash freetds ??*/
			if (colsize > curcol->column_size)
				return TDS_FAIL;
			tds_get_string(tds,dest,colsize);
		} else {
			/* server is going to crash freetds ??*/
			if (colsize > curcol->column_size)
				return TDS_FAIL;
			tds_get_n(tds,dest,colsize);
		}
		if (curcol->column_type == SYBDATETIME4) {
			tdsdump_log(TDS_DBG_INFO1, "%L datetime4 %d %d %d %d\n", dest[0], dest[1], dest[2], dest[3]);
		}
	}

	/* Value used to properly know value in dbdatlen. (mlilback, 11/7/01) */
	curcol->column_cur_size = colsize;

#ifdef WORDS_BIGENDIAN
	/* MS SQL Server 7.0 has broken date types from big endian 
	** machines, this swaps the low and high halves of the 
	** affected datatypes
	**
	** Thought - this might be because we don't have the
	** right flags set on login.  -mjs
	**
	** Nope its an actual MS SQL bug -bsb
	*/
	if (tds->broken_dates &&
		(curcol->column_type == SYBDATETIME ||
		curcol->column_type == SYBDATETIME4 ||
		curcol->column_type == SYBDATETIMN ||
		curcol->column_type == SYBMONEY ||
		curcol->column_type == SYBMONEY4 ||
		(curcol->column_type == SYBMONEYN && curcol->column_size > 4))) 
		/* above line changed -- don't want this for 4 byte SYBMONEYN 
			values (mlilback, 11/7/01) */
	{
		unsigned char temp_buf[8];

		memcpy(temp_buf,dest,colsize/2);
		memcpy(dest,&dest[colsize/2],colsize/2);
		memcpy(&dest[colsize/2],temp_buf,colsize/2);
	}
	if (tds->emul_little_endian && !is_numeric_type(curcol->column_type)) {
		tdsdump_log(TDS_DBG_INFO1, "%L swapping coltype %d\n",
		tds_get_conversion_type(curcol->column_type,colsize));
		tds_swap_datatype(tds_get_conversion_type(curcol->column_type, colsize), dest);
	}
#endif
	return TDS_SUCCEED;
}

/*
** tds_process_row() processes rows and places them in the row buffer.  There
** is also some special handling for some of the more obscure datatypes here.
*/
static int tds_process_row(TDSSOCKET *tds)
{
int i;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;

	info = tds->res_info;
	if (!info)
		return TDS_FAIL;

    tds->curr_resinfo = info;

	info->row_count++;
	for (i=0;i<info->num_cols;i++) {
		curcol = info->columns[i];
		if (tds_get_data(tds,curcol,info->current_row,i) != TDS_SUCCEED)
			return TDS_FAIL;
	}
	return TDS_SUCCEED;
}

/*
** tds_process_end() processes any of the DONE, DONEPROC, or DONEINPROC
** tokens.
*/
TDS_INT tds_process_end(
   TDSSOCKET     *tds,
   int            marker,
   int           *more_results_parm,
   int           *was_cancelled_parm)
{
int more_results, was_cancelled, error;
int tmp;

   tmp  = tds_get_smallint(tds);

   more_results = (tmp & 0x1) != 0;
   was_cancelled = (tmp & 0x20) != 0;
   error         = (tmp & 0x2)  != 0;


   tdsdump_log(TDS_DBG_FUNC, "%L inside tds_process_end() more_results = %d, was_cancelled = %d \n",
               more_results, was_cancelled);
   if (tds->res_info)  {
      tds->res_info->more_results=more_results;
   }

   if (more_results_parm)
	*more_results_parm = more_results;
   if (was_cancelled_parm)
	*was_cancelled_parm = was_cancelled;

   if (was_cancelled || !(more_results)) {
       tds->state = TDS_COMPLETED;
   }

   tds_get_smallint(tds);

   /* rows affected is in the tds struct because a query may affect rows but
   ** have no result set. */

   tds->rows_affected = tds_get_int(tds);

   if (error)
      return TDS_FAIL;
   else
      return TDS_SUCCEED;
}



/*
** tds_client_msg() sends a message to the client application from the CLI or
** TDS layer. A client message is one that is generated from with the library
** and not from the server.  The message is sent to the CLI (the 
** err_handler) so that it may forward it to the client application or
** discard it if no msg handler has been by the application. tds->parent
** contains a void pointer to the parent of the tds socket. This can be cast
** back into DBPROCESS or CS_CONNECTION by the CLI and used to determine the
** proper recipient function for this message.
*/
int tds_client_msg(TDSCONTEXT *tds_ctx, TDSSOCKET *tds, int msgnum, int level, int state, int line, const char *message)
{
int ret;
TDSMSGINFO msg_info;

        if(tds_ctx->err_handler) {
			memset(&msg_info, 0, sizeof(TDSMSGINFO));
			msg_info.msg_number=msgnum;
        	msg_info.msg_level=level; /* severity? */
        	msg_info.msg_state=state;
        	msg_info.server=strdup("OpenClient");
        	msg_info.line_number=line;
        	msg_info.message=strdup(message);
        	ret = tds_ctx->err_handler(tds_ctx, tds, &msg_info);
		tds_free_msg(&msg_info);
		/* message handler returned FAIL/CS_FAIL
		** mark socket as dead */
		if (ret && tds) {
			tds->state=TDS_DEAD;
		}	
	}
	return 0;
}

/*
** tds_process_env_chg() 
** when ever certain things change on the server, such as database, character
** set, language, or block size.  A environment change message is generated
** There is no action taken currently, but certain functions at the CLI level
** that return the name of the current database will need to use this.
*/
int tds_process_env_chg(TDSSOCKET *tds)
{
int size, type;
char *oldval, *newval;
int  new_block_size;
TDSENVINFO *env = tds->env;

	size = tds_get_smallint(tds);
	/* this came in a patch, apparently someone saw an env message
	** that was different from what we are handling? -- brian
	** changed back because it won't handle multibyte chars -- 7.0
	*/
	/* tds_get_n(tds,NULL,size); */

	type = tds_get_byte(tds);

	if (type==0x07) {
		size = tds_get_byte(tds);
		if (size) tds_get_n(tds, NULL, size);
		size = tds_get_byte(tds);
		if (size) tds_get_n(tds, NULL, size);
		return TDS_SUCCEED;
	}

	/* fetch the new value */
	size = tds_get_byte(tds);
	newval = (char *) malloc((size+1)*2);
	tds_get_string(tds,newval,size);
	newval[size]='\0';

	/* fetch the old value */
	size = tds_get_byte(tds);
	oldval = (char *) malloc((size+1)*2); /* may be a unicode string */
	tds_get_string(tds,oldval,size);
	oldval[size]='\0';

	switch (type) {
		case TDS_ENV_BLOCKSIZE:
			new_block_size = atoi(newval);
			if (new_block_size > env->block_size) {
				tdsdump_log(TDS_DBG_INFO1, "%L increasing block size from %s to %d\n", oldval, new_block_size);
				/* 
				** I'm not aware of any way to shrink the 
				** block size but if it is possible, we don't 
				** handle it.
				*/
				tds->out_buf = (unsigned char*) realloc(tds->out_buf, new_block_size);
				env->block_size = new_block_size;
			}
			break;
	}
	free(oldval);
	free(newval);

	return TDS_SUCCEED;
}

int tds_process_column_row(TDSSOCKET *tds)
{
int colsize, i;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;
unsigned char *dest;


      info = tds->res_info;
      info->row_count++;

      for (i=0;i<(info->num_cols -1);i++)
      {
              curcol = info->columns[i];
              if (!is_fixed_type(curcol->column_type)) {
                      colsize = tds_get_byte(tds);
              } else {
                      colsize = tds_get_size_by_type(curcol->column_type);
              }
		dest = &(info->current_row[curcol->column_offset]);
		tds_get_n(tds,dest,colsize);
		dest[colsize]='\0';
              /* printf("%s\n",curcol->column_value); */
	}

	/* now skip over some stuff and get the rest of the columns */
	tds_get_n(tds,NULL,25);
	colsize = tds_get_byte(tds);
	tds_get_n(tds,NULL,3);
	curcol = info->columns[i];
	dest = &(info->current_row[curcol->column_offset]);
	tds_get_n(tds,dest,colsize);
	dest[colsize]='\0';

	return TDS_SUCCEED;
}

/*
** tds_process_msg() is called for MSG, ERR, or EED tokens and is responsible
** for calling the CLI's message handling routine
** returns TDS_SUCCEED if informational, TDS_ERROR if error.
*/
static int tds_process_msg(TDSSOCKET *tds,int marker)
{
int rc;
int len;
int len_msg;
int len_svr;
int len_sqlstate;
TDSMSGINFO msg_info;

	/* make sure message has been freed */
	memset(&msg_info, 0, sizeof(TDSMSGINFO));

	/* packet length */
	len = tds_get_smallint(tds);

	/* message number */
	rc = tds_get_int(tds);
	msg_info.msg_number = rc;

	/* msg state */
	msg_info.msg_state = tds_get_byte(tds);

	/* msg level */
	msg_info.msg_level = tds_get_byte(tds);

	/* determine if msg or error */
	if (marker==TDS_EED_TOKEN) {
		if (msg_info.msg_level<=10) 
                    msg_info.priv_msg_type = 0;
		else 
                    msg_info.priv_msg_type = 1;
		/* junk this info for now */
		len_sqlstate = tds_get_byte(tds);
		msg_info.sql_state = (char*)malloc(len_sqlstate+1);
		tds_get_n(tds, msg_info.sql_state,len_sqlstate);
		msg_info.sql_state[len_sqlstate] = '\0';

		/* unknown values */
		tds_get_byte(tds);
		tds_get_smallint(tds);
	} 
	else if (marker==TDS_MSG_TOKEN) {
		msg_info.priv_msg_type = 0;
	} else if (marker==TDS_ERR_TOKEN) {
		msg_info.priv_msg_type = 1;
	} else {
		tdsdump_log(TDS_DBG_ERROR,"tds_process_msg() called with unknown marker!\n");
		return TDS_FAIL;
	}

	/* the message */
	len_msg = tds_get_smallint(tds);
	msg_info.message = (char*)malloc(len_msg+1);
	tds_get_string(tds, msg_info.message, len_msg);
	msg_info.message[len_msg] = '\0';

	/* server name */
	len_svr = tds_get_byte(tds);
	msg_info.server = (char*)malloc(len_svr+1);
	tds_get_string(tds, msg_info.server, len_svr);
	msg_info.server[len_svr] = '\0';

	/* stored proc name if available */
	rc = tds_get_byte(tds);
	if (rc > 0) {
		msg_info.proc_name=tds_msg_get_proc_name(tds, rc);
	} else {
		msg_info.proc_name=strdup("");
	}

	/* line number in the sql statement where the problem occured */
	msg_info.line_number = tds_get_smallint(tds);

	/* call the msg_handler that was set by an upper layer 
	** (dblib, ctlib or some other one).  Call it with the pointer to 
	** the "parent" structure.
	*/

	if (tds->tds_ctx->msg_handler) {
	  tds->tds_ctx->msg_handler(tds->tds_ctx, tds, &msg_info);
	} else {
		if (msg_info.msg_number)
			tdsdump_log(TDS_DBG_WARN,
			"%L Msg %d, Level %d, State %d, Server %s, Line %d\n%s\n",
			msg_info.msg_number,
			msg_info.msg_level,
			msg_info.msg_state,
			msg_info.server,
			msg_info.line_number,
			msg_info.message);
	}
	tds_free_msg(&msg_info);
	return TDS_SUCCEED;
}

char *tds_msg_get_proc_name(TDSSOCKET *tds, int procnamelen)
{
char *proc_name;

    proc_name = (char*)malloc(procnamelen + 1);
    tds_get_string(tds, proc_name, procnamelen);
    proc_name[procnamelen] = '\0';

	return proc_name;

}

/*
** tds_process_cancel() processes the incoming token stream until it finds
** an end token (DONE, DONEPROC, DONEINPROC) with the cancel flag set.
** a that point the connetion should be ready to handle a new query.
*/
int tds_process_cancel(TDSSOCKET *tds)
{
int marker, cancelled=0;

	/* FIXME we must wait for cancel packet first, then wait for done */
	do {
		marker=tds_get_byte(tds);
		if (marker==TDS_DONE_TOKEN) {
			tds_process_end(tds, marker, NULL, &cancelled);
		} else if (marker==0) {
			cancelled = 1;
		} else {
			tds_process_default_tokens(tds,marker);
		}
	} while (!cancelled);
	tds->state = TDS_COMPLETED;

	return 0;
}
/* =========================== tds_is_result_row() ===========================
 * 
 * Def:  does the next token in stream signify a result row?
 * 
 * Ret:  true if stream is positioned at a row, false otherwise
 * 
 * ===========================================================================
 */
int tds_is_result_row(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   if (marker==TDS_ROW_TOKEN)
   {
      result = 1;
   }
   return result;
} /* tds_is_result_row()  */


int tds_is_result_set(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = (marker==TDS_COL_NAME_TOKEN 
             || marker==TDS_COL_INFO_TOKEN 
             || marker==TDS_RESULT_TOKEN
             || marker==TDS7_RESULT_TOKEN);
   return result;
} /* tds_is_result_set()  */


int tds_is_end_of_results(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = marker==TDS_DONE_TOKEN || marker==TDS_DONEPROC_TOKEN;
   return result;
} /* tds_is_end_of_results()  */


int tds_is_doneinproc(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = marker==TDS_DONEINPROC_TOKEN;
   return result;
} /* tds_is_end_of_results()  */


int tds_is_error(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = marker==TDS_ERR_TOKEN;
   return result;
} /* tds_is_error()  */

int tds_is_message(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = marker==TDS_MSG_TOKEN;
   return result;
} /* tds_is_message()  */
int tds_is_control(TDSSOCKET *tds)
{
   const int  marker = tds_peek(tds);
   int        result = 0;

   result = (marker==TDS_CONTROL_TOKEN);
   return result;
}
/**
 * set the null bit for the given column in the row buffer
 */
void tds_set_null(unsigned char *current_row, int column)
{
int bytenum = column  / 8;
int bit = column % 8;
unsigned char mask = 1 << bit;

	tdsdump_log(TDS_DBG_INFO1,"%L setting column %d NULL bit\n", column);
	current_row[bytenum] |= mask;
}
/*
** clear the null bit for the given column in the row buffer
*/
void tds_clr_null(unsigned char *current_row, int column)
{
int bytenum = column  / 8;
int bit = column % 8;
unsigned char mask = ~(1 << bit);

	tdsdump_log(TDS_DBG_INFO1, "%L clearing column %d NULL bit\n", column);
	current_row[bytenum] &= mask;
}
/*
** return the null bit for the given column in the row buffer
*/
int tds_get_null(unsigned char *current_row, int column)
{
int bytenum = column  / 8;
int bit = column % 8;
unsigned char mask = 1 << bit;

	return (current_row[bytenum] & mask) ? 1 : 0;
}

int tds_lookup_dynamic(TDSSOCKET *tds, char *id)
{
int i;

	for (i=0;i<tds->num_dyns;i++) {
		if (!strcmp(tds->dyns[i]->id, id)) {
			return i;
		}
	}
	return -1;
}
/*
** tds_process_dynamic()
** finds the element of the dyns array for the id
*/
static int tds_process_dynamic(TDSSOCKET *tds)
{
int token_sz;
char subtoken[2];
int id_len;
char id[TDS_MAX_DYNID_LEN+1];
int drain = 0;

	token_sz = tds_get_smallint(tds);
	subtoken[0] = tds_get_byte(tds);
	subtoken[1] = tds_get_byte(tds);
	if (subtoken[0]!=0x20 || subtoken[1]!=0x00) {
		tdsdump_log(TDS_DBG_ERROR,"Unrecognized TDS5_DYN subtoken %x,%x\n",
		        subtoken[0], subtoken[1]);
		tds_get_n(tds, NULL, token_sz-2);
		return -1;
	}
	id_len = tds_get_byte(tds);
	if (id_len > TDS_MAX_DYNID_LEN) {
		drain = id_len - TDS_MAX_DYNID_LEN;
		id_len = TDS_MAX_DYNID_LEN;
	}
	tds_get_string(tds, id, id_len);
	id[id_len]='\0';
	if (drain) { 
		tds_get_string(tds, NULL, drain);
	}
	return tds_lookup_dynamic(tds,id);
}

static void tds_process_dyn_result(TDSSOCKET *tds)
{
int hdrsize;
int col, num_cols;
TDSCOLINFO *curcol;
TDSRESULTINFO *info;
TDSDYNAMIC *dyn;

	hdrsize = tds_get_smallint(tds);
	num_cols = tds_get_smallint(tds);

	if (tds->cur_dyn_elem) {
		dyn = tds->dyns[tds->cur_dyn_elem];
		tds_free_results(dyn->res_info);
		/* read number of columns and allocate the columns structure */
		dyn->res_info = tds_alloc_results(num_cols);
		info = dyn->res_info;
	} else {
		tds_free_results(tds->res_info);
		tds->res_info = tds_alloc_results(num_cols);
		info = tds->res_info;
	}

	for (col=0;col<info->num_cols;col++) {
		curcol=info->columns[col];
		tds_get_n(tds,NULL,6);
		/* column type */
		curcol->column_type = tds_get_byte(tds);
		/* column size */
		if (!is_fixed_type(curcol->column_type)) {
			curcol->column_size = tds_get_byte(tds);
		} else { 
			curcol->column_size = tds_get_size_by_type(curcol->column_type);
		}
		tds_get_byte(tds);
		/* fprintf(stderr,"elem %d coltype %d size %d\n",tds->cur_dyn_elem, curcol->column_type, curcol->column_size); */
	}
}

/* 
** tds_is_fixed_token()
** some tokens are fixed length while others are variable.  This function is 
** used by tds_process_cancel() to determine how to read past a token
*/
int tds_is_fixed_token(int marker)
{
	switch (marker) {
		case TDS_DONE_TOKEN:
		case TDS_DONEPROC_TOKEN:
		case TDS_DONEINPROC_TOKEN:
		case TDS_RET_STAT_TOKEN:
			return 1;
		default:
			return 0;	
	}
}

/* 
** tds_get_token_size() returns the size of a fixed length token
** used by tds_process_cancel() to determine how to read past a token
*/
int tds_get_token_size(int marker)
{
	switch(marker) {
		case TDS_DONE_TOKEN:
		case TDS_DONEPROC_TOKEN:
		case TDS_DONEINPROC_TOKEN:
			return 8;
		case TDS_RET_STAT_TOKEN:
			return 4;
		case TDS_124_TOKEN:
			return 8;
		default:
			return 0;
	}
}
void tds_swap_datatype(int coltype, unsigned char *buf)
{
TDS_NUMERIC *num;

	switch(coltype) {
		case SYBINT2:
			tds_swap_bytes(buf,2); break;
		case SYBINT4:
			tds_swap_bytes(buf,4); break;
		case SYBINT8:
			tds_swap_bytes(buf,8); break;
		case SYBREAL:
			tds_swap_bytes(buf,4); break;
		case SYBFLT8:
			tds_swap_bytes(buf,8); break;
		case SYBMONEY4:
			tds_swap_bytes(buf,4); break;
		case SYBMONEY:
			tds_swap_bytes(buf,4);
			tds_swap_bytes(&buf[4],4); break;
		case SYBDATETIME4:
			tds_swap_bytes(buf,2);
			tds_swap_bytes(&buf[2],2); break;
		case SYBDATETIME:
			tds_swap_bytes(buf,4);
			tds_swap_bytes(&buf[4],4); break;
		case SYBNUMERIC:
		case SYBDECIMAL:
			num = (TDS_NUMERIC *) buf;
            /* swap the sign */
            num->array[0] = (num->array[0] == 0) ? 1 : 0;
            /* swap the data */
            tds_swap_bytes(&(num->array[1]),
                           g__numeric_bytes_per_prec[num->precision] - 1); break;

        case SYBUNIQUE:
			tds_swap_bytes(buf,4);
			tds_swap_bytes(&buf[4],2); 
			tds_swap_bytes(&buf[6],2); break;

	}
}
/*
** tds_get_varint_size() returns the size of a variable length integer
** returned in a TDS 7.0 result string
*/
static int tds_get_varint_size(int datatype)
{
	switch(datatype) {
		case SYBTEXT:
		case SYBNTEXT:
		case SYBIMAGE:
		case SYBVARIANT:
			return 4;
		case SYBVOID:
		case SYBINT1:
		case SYBBIT:
		case SYBINT2:
		case SYBINT4:
		case SYBDATETIME4:
		case SYBREAL:
		case SYBMONEY:
		case SYBDATETIME:
		case SYBFLT8:
		case SYBMONEY4:
		case SYBINT8:
			return 0;
		case XSYBNCHAR:
		case XSYBNVARCHAR:
		case XSYBCHAR:
		case XSYBVARCHAR:
		case XSYBBINARY:
		case XSYBVARBINARY:
			return 2;
		default:
			return 1;
	}
}

static int tds_get_cardinal_type(int datatype)
{
	switch(datatype) {
		case XSYBVARBINARY: 
			return SYBVARBINARY;
		case XSYBBINARY: 
			return SYBBINARY;
		case SYBNTEXT:
			return SYBTEXT;
		case XSYBNVARCHAR: 
		case XSYBVARCHAR: 
			return SYBVARCHAR;
		case XSYBNCHAR: 
		case XSYBCHAR: 
			return SYBCHAR;
	}
	return datatype;
}

/*
** tds_process_compute_names() processes compute result sets.  
*/
static int tds_process_compute_names(TDSSOCKET *tds)
{
int hdrsize;
int remainder;
int num_cols = 0; 
int col;
TDS_SMALLINT compute_id = 0;
TDS_TINYINT  namelen;
TDSCOMPUTEINFO *info;
TDSCOLINFO     *curcol;

struct namelist {
char name[256];
int  namelen;
struct namelist *nextptr;
};

struct namelist *topptr = NULL;
struct namelist *curptr = NULL;
struct namelist *freeptr = NULL;

	hdrsize = tds_get_smallint(tds);
    remainder = hdrsize;
    tdsdump_log(TDS_DBG_INFO1, "%L processing tds5 compute names. remainder = %d\n", remainder);

    /* compute statement id which this relates */
    /* to. You can have more than one compute  */
    /* statement in a SQL statement            */

    compute_id = tds_get_smallint(tds);   
    remainder -= 2;

    while (remainder) {
      namelen = tds_get_byte(tds);
      remainder--;
      if (topptr == (struct namelist *) NULL) {
         topptr = malloc(sizeof(struct namelist));
         curptr = topptr;
         curptr->nextptr = NULL;
      } else {
         curptr->nextptr = malloc(sizeof(struct namelist));
         curptr = curptr->nextptr;
         curptr->nextptr = NULL;
      }
      if ( namelen == 0)
         strcpy(curptr->name, "");
      else {
		 tds_get_string(tds,curptr->name, namelen);
         remainder -= namelen;
      }
      curptr->namelen = namelen;
      num_cols++;
      tdsdump_log(TDS_DBG_INFO1, "%L processing tds5 compute names. remainder = %d\n", remainder);
    }

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds5 compute names. num_cols = %d\n", num_cols);

	tds->comp_info = tds_alloc_compute_results(&(tds->num_comp_info), tds->comp_info,  num_cols, 0);

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds5 compute names. num_comp_info = %d\n", tds->num_comp_info);

	info = tds->comp_info[tds->num_comp_info - 1];
    tds->curr_resinfo = info;

    info->computeid = compute_id;

    curptr = topptr;

	for (col = 0; col < num_cols; col++) 
	{
		curcol = info->columns[col];

		strcpy(curcol->column_name, curptr->name);
        curcol->column_namelen = curptr->namelen;

        freeptr = curptr;
        curptr  = curptr->nextptr;
        free (freeptr);
        
	}

	return TDS_SUCCEED;
}
/*
** tds7_process_compute_result() processes compute result sets for TDS 7/8.
** They is are very  similar to normal result sets.
*/
static int tds7_process_compute_result(TDSSOCKET *tds)
{
int col, num_cols; 
TDS_TINYINT by_cols;
TDS_TINYINT *cur_by_col;
TDS_SMALLINT compute_id;
TDSCOLINFO *curcol;
TDSCOMPUTEINFO *info;
int remainder;
TDS_SMALLINT collate_type;
TDS_SMALLINT tabnamelen;
int colnamelen;

    /* number of compute columns returned - so */
    /* COMPUTE SUM(x), AVG(x)... would return  */
    /* num_cols = 2                            */

	num_cols   = tds_get_smallint(tds);   

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. num_cols = %d\n", num_cols);

    /* compute statement id which this relates */
    /* to. You can have more than one compute  */
    /* statement in a SQL statement            */

    compute_id = tds_get_smallint(tds);   

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. compute_id = %d\n", compute_id);
    /* number of "by" columns in compute - so  */
    /* COMPUTE SUM(x) BY a, b, c would return  */
    /* by_cols = 3                             */

    by_cols    = tds_get_byte(tds);       
    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. by_cols = %d\n", by_cols);

	tds->comp_info = tds_alloc_compute_results(&(tds->num_comp_info), tds->comp_info,  num_cols, by_cols);

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. num_comp_info = %d\n", tds->num_comp_info);

	info = tds->comp_info[tds->num_comp_info - 1];
    tds->curr_resinfo = info;

    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 0\n");

    info->computeid = compute_id;

    /* the by columns are a list of the column */
    /* numbers in the select statement         */ 

    cur_by_col = info->bycolumns;
	for (col = 0; col < by_cols; col++) 
	{
        *cur_by_col = tds_get_smallint(tds);
        cur_by_col++;
    }
    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 1\n");

	for (col = 0; col < num_cols; col++) 
	{
        tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 2\n");
		curcol = info->columns[col];

        curcol->column_operator = tds_get_byte(tds);
		curcol->column_operand  = tds_get_smallint(tds);  

		/*  User defined data type of the column */
		curcol->column_usertype = tds_get_smallint(tds);  

        curcol->column_flags = tds_get_smallint(tds);     /*  Flags */

        curcol->column_nullable  =  curcol->column_flags & 0x01;
        curcol->column_writeable = (curcol->column_flags & 0x08) > 0;
		curcol->column_identity  = (curcol->column_flags & 0x10) > 0;

		curcol->column_type = tds_get_byte(tds); 
		
        curcol->column_type_save = curcol->column_type;
		collate_type = is_collate_type(curcol->column_type);
		curcol->column_varint_size  = tds_get_varint_size(curcol->column_type);

		switch(curcol->column_varint_size) {
			case 4: 
				curcol->column_size = tds_get_int(tds);
				break;
			case 2: 
				curcol->column_size = tds_get_smallint(tds);
				break;
			case 1: 
				curcol->column_size = tds_get_byte(tds);
				break;
			case 0: 
				curcol->column_size = tds_get_size_by_type(curcol->column_type);
				break;
		}

        tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 3\n");
		curcol->column_unicodedata = 0;

		if (is_unicode(curcol->column_type))
			curcol->column_unicodedata = 1;

		curcol->column_type = tds_get_cardinal_type(curcol->column_type);

		/* numeric and decimal have extra info */
		if (is_numeric_type(curcol->column_type)) {
			curcol->column_prec = tds_get_byte(tds); /* precision */
			curcol->column_scale = tds_get_byte(tds); /* scale */
		}

		if (IS_TDS80(tds)) {
			if (collate_type) 
				tds_get_n(tds, curcol->column_collation, 5);
		}

		if (is_blob_type(curcol->column_type)) {
			tabnamelen = tds_get_smallint(tds);
			tds_get_string(tds, NULL, tabnamelen);
		}

		colnamelen = tds_get_byte(tds);
        tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 4 namelen = %d\n", colnamelen);

        if (colnamelen) {
		   /* under 7.0 lengths are number of characters not 
    		** number of bytes...tds_get_string handles this */
		   tds_get_string(tds,curcol->column_name, colnamelen);
           curcol->column_namelen = colnamelen;
        }
        else {
           strcpy(curcol->column_name, tds_prtype(curcol->column_operator));
           curcol->column_namelen = strlen(curcol->column_name);
        }

		/* the column_offset is the offset into the row buffer
		** where this column begins, text types are no longer
		** stored in the row buffer because the max size can
		** be too large (2gig) to allocate */
		curcol->column_offset = info->row_size;
		if (!is_blob_type(curcol->column_type)) {
			info->row_size += curcol->column_size + 1;
		}
		if (is_numeric_type(curcol->column_type)) {
                       info->row_size += sizeof(TDS_NUMERIC) + 1;
		}
		
		/* actually this 4 should be a machine dependent #define */
		remainder = info->row_size % 4;
		if (remainder) info->row_size += (4 - remainder);
	}

	/* all done now allocate a row for tds_process_row to use */
    tdsdump_log(TDS_DBG_INFO1, "%L processing tds7 compute result. point 5 \n");
	info->current_row = tds_alloc_compute_row(info);

	return TDS_SUCCEED;
}

const char *
tds_prtype(int token) {

	switch (token) {
	case SYBAOPAVG:
		return "avg";
		break;
	case SYBAOPCNT:
		return "count";
		break;
	case SYBAOPMAX:
		return "max";
		break;
	case SYBAOPMIN:
		return "min";
		break;
	case SYBAOPSUM:
		return "sum";
		break;
	case SYBBINARY:
		return "binary";
		break;
	case SYBBIT:
		return "bit";
		break;
	case SYBBITN:
		return "bit-null";
		break;
	case SYBCHAR:
		return "char";
                break;
	case SYBDATETIME4:
		return "smalldatetime";
		break;
	case SYBDATETIME:
		return "datetime";
		break;
	case SYBDATETIMN:
		return "datetime-null";
		break;
	case SYBDECIMAL:
		return "decimal";
		break;
	case SYBFLT8:
		return "float";
		break;
	case SYBFLTN:
		return "float-null";
		break;
	case SYBIMAGE:
		return "image";
		break;
	case SYBINT1:
		return "tinyint";
		break;
	case SYBINT2:
		return "smallint";
		break;
	case SYBINT4:
		return "int";
		break;
	case SYBINT8:
		return "long long";
		break;
	case SYBINTN:
		return "integer-null";
		break;
	case SYBMONEY4:
		return "smallmoney";
		break;
	case SYBMONEY:
		return "money";
		break;
	case SYBMONEYN:
		return "money-null";
		break;
	case SYBNTEXT:
		return "UCS-2 text";
		break;
	case SYBNVARCHAR:
		return "UCS-2 varchar";
		break;
	case SYBNUMERIC:
		return "numeric";
		break;
	case SYBREAL:
		return "real";
                break;
	case SYBTEXT:
		return "text";
                break;
	case SYBUNIQUE:
		return "uniqueidentifier";
		break;
	case SYBVARBINARY:
		return "varbinary";
		break;
	case SYBVARCHAR:
		return "varchar";
		break;
	case SYBVARIANT:
		return "variant";
                break;
	case SYBVOID:
		return "void";
                break;
	case XSYBBINARY:
		return "xbinary";
		break;
	case XSYBCHAR:
		return "xchar";
		break;
	case XSYBNCHAR:
		return "x UCS-2 char";
		break;
	case XSYBNVARCHAR:
		return "x UCS-2 varchar";
		break;
	case XSYBVARBINARY:
		return "xvarbinary";
		break;
	case XSYBVARCHAR:
		return "xvarchar";
		break;
	default:
		break;
	}
	return "";
}
/** \@} */
