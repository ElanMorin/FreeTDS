/* ================================= t0010.c =================================
 * 
 *  Def:   Test to see if dbbind can handle a varlen of 0 with a 
 *         column bound as STRINGBIND and a database column of VARCHAR.
 * 
 * ===========================================================================
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#define DBNTWIN32
#include <windows.h>
#endif
#include <sqlfront.h>
#include <sqldb.h>

#include "common.h"



static char  software_version[]   = "$Id: t0010.c,v 1.5 2002/10/04 14:56:54 castellano Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};



int
main(int argc, char **argv)
{
   LOGINREC   *login;
   DBPROCESS   *dbproc;
   int         i;
   char        teststr[1024];
   DBINT       testint;

#if HAVE_MALLOC_OPTIONS
   /*
    * Options for malloc   A- all warnings are fatal, J- init memory to 0xD0,
    * R- always move memory block on a realloc.
    */
   extern char *malloc_options;
   malloc_options = "AJR";
#endif

   read_login_info();

   fprintf(stdout, "Start\n");
   add_bread_crumb();

   /* Fortify_EnterScope(); */
   dbinit();
   
   add_bread_crumb();
   dberrhandle( syb_err_handler );
   dbmsghandle( syb_msg_handler );

   fprintf(stdout, "About to logon\n");
   
   add_bread_crumb();
   login = dblogin();
   DBSETLPWD(login,PASSWORD);
   DBSETLUSER(login,USER);
   DBSETLAPP(login,"t0010");
   DBSETLHOST(login,"ntbox.dntis.ro");

fprintf(stdout, "About to open\n");
   
   add_bread_crumb();
   dbproc = dbopen(login, SERVER);
   if (strlen(DATABASE)) dbuse(dbproc,DATABASE);
   add_bread_crumb();

#ifdef MICROSOFT_DBLIB
   dbsetopt(dbproc, DBBUFFER, "100");
#else
   dbsetopt(dbproc, DBBUFFER, "100", 0);
#endif
   add_bread_crumb();
   
   fprintf(stdout, "Dropping table\n");
   add_bread_crumb();
   dbcmd(dbproc, "drop table #dblib0010");
   add_bread_crumb();
   dbsqlexec(dbproc);
   add_bread_crumb();
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   add_bread_crumb();
   
   fprintf(stdout, "creating table\n");
   dbcmd(dbproc,
         "create table #dblib0010 (i int not null, s char(10) not null)");
   dbsqlexec(dbproc);
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   
   fprintf(stdout, "insert\n");
   dbcmd(dbproc, "insert into #dblib0010 values (1, 'abcdef')"); 
   dbsqlexec(dbproc);
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   dbcmd(dbproc, "insert into #dblib0010 values (2, 'abc')"); 
   dbsqlexec(dbproc);
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }


   fprintf(stdout, "select\n");
   dbcmd(dbproc,"select * from #dblib0010 order by i");
   dbsqlexec(dbproc);
   add_bread_crumb();

   
   if (dbresults(dbproc)!=SUCCEED) 
   {
      add_bread_crumb();
      fprintf(stdout, "Was expecting a result set.");
      exit(1);
   }
   add_bread_crumb();

   for (i=1;i<=dbnumcols(dbproc);i++)
   {
      add_bread_crumb();
      printf ("col %d is %s\n",i,dbcolname(dbproc,i));
      add_bread_crumb();
   }
   
   add_bread_crumb();
   dbbind(dbproc,1,INTBIND,-1,(BYTE *) &testint); 
   add_bread_crumb();
   dbbind(dbproc,2,STRINGBIND, 0,(BYTE *) teststr);
   add_bread_crumb();
   
   add_bread_crumb();


   if (REG_ROW != dbnextrow(dbproc))
   {
      fprintf(stderr, "dblib failed for %s\n", __FILE__);
      exit(1);
   }
   if (0 != strcmp("abcdef    ", teststr))
   {
      fprintf(stderr, "Expected |%s|, found |%s|\n", "abcdef", teststr);
      fprintf(stderr, "dblib failed for %s\n", __FILE__);
      exit(1);
   }

   if (REG_ROW != dbnextrow(dbproc))
   {
      fprintf(stderr, "dblib failed for %s\n", __FILE__);
      exit(1);
   }
   if (0 != strcmp("abc       ", teststr))
   {
      fprintf(stderr, "Expected |%s|, found |%s|\n", "abc", teststr);
      fprintf(stderr, "dblib failed for %s\n", __FILE__);
      exit(1);
   }

   fprintf(stderr, "dblib passed for %s\n", __FILE__);
   return 0;
}





