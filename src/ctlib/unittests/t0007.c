#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <stdio.h>
#include <ctpublic.h>
#include "common.h"

static char  software_version[]   = "$Id: t0007.c,v 1.4 2002/10/13 23:28:12 castellano Exp $";
static void *no_unused_var_warn[] = {software_version, no_unused_var_warn};

/* Testing: Retrieve CS_TEXT_TYPE using ct_bind() */
int
main(int argc, char **argv)
{
   CS_CONTEXT *ctx; 
   CS_CONNECTION *conn; 
   CS_COMMAND *cmd; 
   int verbose = 0;

   CS_RETCODE ret;
   CS_RETCODE results_ret;
   CS_INT result_type;
   CS_INT num_cols;

   CS_DATAFMT datafmt;
   CS_INT datalength;
   CS_SMALLINT ind;
   CS_INT count, row_count = 0;
 
   CS_CHAR name[1024];
   name[0] = 0;
   
   fprintf(stdout, "%s: Retrieve CS_CHAR_TYPE using ct_bind()\n", __FILE__);
   if (verbose)         { fprintf(stdout, "Trying login\n"); }
   ret = try_ctlogin(&ctx, &conn, &cmd, verbose);
   if (ret != CS_SUCCEED) {
     fprintf(stderr, "Login failed\n");
     return 1;
   }

   ret = ct_command(cmd, CS_LANG_CMD,
         "SELECT CONVERT(VARCHAR(7),'1234') AS test", CS_NULLTERM, CS_UNUSED);
   if (ret != CS_SUCCEED) {
     fprintf(stderr, "ct_command() failed\n");
     return 1;
   }
   ret = ct_send(cmd);
   if (ret != CS_SUCCEED) {
     fprintf(stderr, "ct_send() failed\n");
     return 1;
   }
   while ((results_ret = ct_results(cmd, &result_type)) == CS_SUCCEED) {
      switch ((int)result_type) {
         case CS_CMD_SUCCEED:
            break;
         case CS_CMD_DONE:
            break;
         case CS_CMD_FAIL:
            fprintf(stderr,"ct_results() result_type CS_CMD_FAIL.\n");
            return 1;
         case CS_ROW_RESULT:
            ret = ct_res_info(cmd, CS_NUMDATA, &num_cols, CS_UNUSED, NULL);
            if (ret != CS_SUCCEED) {
               fprintf(stderr, "ct_res_info() failed");
               return 1;
            }
            if (num_cols != 1) {
               fprintf(stderr, "num_cols %d != 1", num_cols);
               return 1;
            }
            ret = ct_describe(cmd, 1, &datafmt);
            if (ret != CS_SUCCEED) {
               fprintf(stderr, "ct_describe() failed");
               return 1;
            }
            datafmt.format = CS_FMT_NULLTERM;
	    ++datafmt.maxlength;
            if (datafmt.maxlength > 1024) {
               datafmt.maxlength = 1024;
            }
            ret = ct_bind(cmd, 1, &datafmt, name, &datalength, &ind);
            if (ret != CS_SUCCEED) {
              fprintf(stderr, "ct_bind() failed\n");
              return 1;
            }

            while (((ret = ct_fetch(cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED,
             &count)) == CS_SUCCEED)
             || (ret == CS_ROW_FAIL)) {
               row_count += count;
               if (ret == CS_ROW_FAIL) {
                  fprintf(stderr, "ct_fetch() CS_ROW_FAIL on row %d.\n",
                     row_count); 
                  return 1;
               }
               else {  /* ret == CS_SUCCEED */
                  if (verbose) { fprintf(stdout, "name = '%s'\n", name); }
                  if (strcmp(name, "1234")) {
                     fprintf(stderr, "Bad return:\n'%s'\n! =\n'%s'\n", name, "1234");
                     return 1;
                  }
                  if (datalength != strlen(name)+1) {
                     fprintf(stderr, "Bad count:\n'%d'\n! =\n'%d'\n", strlen(name)+1, count);
                     return 1;
                  }
               }
            }
            switch ((int)ret) {
               case CS_END_DATA:
                  break;
               case CS_FAIL:
                  fprintf(stderr, "ct_fetch() returned CS_FAIL.\n"); 
                  return 1;
               default:
                  fprintf(stderr, "ct_fetch() unexpected return.\n"); 
                  return 1;
            }
            break;
         case CS_COMPUTE_RESULT:
            fprintf(stderr,"ct_results() unexpected CS_COMPUTE_RESULT.\n");
            return 1;
         default:
            fprintf(stderr,"ct_results() unexpected result_type.\n");
            return 1;
      }
   }
   switch ((int) results_ret) {
      case CS_END_RESULTS:
         break;
      case CS_FAIL:
         fprintf(stderr,"ct_results() failed.\n");
         return 1;
         break;
      default:
         fprintf(stderr,"ct_results() unexpected return.\n");
         return 1;
   }
   
   if (verbose)         { fprintf(stdout, "Trying logout\n"); }
   ret = try_ctlogout(ctx, conn, cmd, verbose);
   if (ret != CS_SUCCEED) {
     fprintf(stderr, "Logout failed\n");
     return 1;
   }

   return 0;
}

