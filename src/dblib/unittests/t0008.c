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



static char  software_version[]   = "$Id: t0008.c,v 1.2 2002/08/29 09:54:54 freddy77 Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};



int main()
{
   RETCODE     rc;
   const int   rows_to_add = 50;
   LOGINREC   *login;
   DBPROCESS   *dbproc;
   int         i;
   char        teststr[1024];
   DBINT       testint;
   DBINT       last_read = -1;

#ifdef __FreeBSD__
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
   DBSETLAPP(login,"t0008");
   DBSETLHOST(login,"ntbox.dntis.ro");

fprintf(stdout, "About to open\n");
   
   add_bread_crumb();
   dbproc = dbopen(login, SERVER);
   if (strlen(DATABASE)) dbuse(dbproc,DATABASE);
   add_bread_crumb();

#ifdef MICROSOFT_DBLIB
   dbsetopt(dbproc, DBBUFFER, "25");
#else
   dbsetopt(dbproc, DBBUFFER, "25", 0);
#endif
   add_bread_crumb();
   
   fprintf(stdout, "Dropping table\n");
   add_bread_crumb();
   dbcmd(dbproc, "drop table #dblib0008");
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
         "create table #dblib0008 (i int not null, s char(10) not null)");
   dbsqlexec(dbproc);
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   
   fprintf(stdout, "insert\n");
   for(i=1; i<rows_to_add; i++)
   {
      char   cmd[1024];

      sprintf(cmd, "insert into #dblib0008 values (%d, 'row %03d')", i, i);
      dbcmd(dbproc, cmd);
      dbsqlexec(dbproc);
      while (dbresults(dbproc)!=NO_MORE_RESULTS)
      {
         /* nop */
      }
   }

   fprintf(stdout, "select\n");
   dbcmd(dbproc,"select * from #dblib0008 order by i");
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
   dbbind(dbproc,2,STRINGBIND,-1,(BYTE *) teststr);
   add_bread_crumb();
   
   add_bread_crumb();

   for(i=1; i<rows_to_add; i++)
   {
      char   expected[1024]; 
      sprintf(expected, "row %03d", i);

      add_bread_crumb();

      if (i%25==0)
      {
         dbclrbuf(dbproc, 25);
      }

      add_bread_crumb();
      if (REG_ROW != dbnextrow(dbproc))
      {
         fprintf(stderr, "dblib %s for %s\n", 
                 last_read==48 ? "okay" : "failed", __FILE__);
         return (last_read==48) ? 0 : 1;
      }
      add_bread_crumb();
      last_read = testint;
      if (testint==49)
      {
         fprintf(stderr, "dblib failed for %s\n", __FILE__);
         exit(1);
      }
      if (testint!=i)
      {
         fprintf(stderr, "Failed.  Expected i to be %d, was %d\n", i, 
                 (int)testint);
         abort();
      }
      if (0!= strncmp(teststr, expected, strlen(expected)))
      {
         fprintf(stdout, "Failed.  Expected s to be |%s|, was |%s|\n", 
                 expected, teststr);
         abort();
      }  
      printf("Read a row of data -> %d %s\n", (int)testint, teststr); 
   }

   fprintf(stderr, "dblib failed for %s\n", __FILE__);
   return 1;
}





