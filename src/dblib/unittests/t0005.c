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

#ifndef _WIN32 
#include <tdsutil.h>
#endif

#include "common.h"



static char  software_version[]   = "$Id: t0005.c,v 1.4 2002/10/03 18:46:08 castellano Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};



int
main(int argc, char **argv)
{
   const int   rows_to_add = 50;
   LOGINREC   *login;
   DBPROCESS   *dbproc;
   int         i;
   char        teststr[1024];
   DBINT       testint;
   char        cmd[1024];
   int         failed = 0;

#ifdef __FreeBSD__
   /*
    * Options for malloc   A- all warnings are fatal, J- init memory to 0xD0,
    * R- always move memory block on a realloc.
    */
   extern char *malloc_options;
   malloc_options = "AJR";
#endif

#ifndef _WIN32
   tdsdump_open("");
#endif

   read_login_info();
   fprintf(stdout, "Start\n");
   add_bread_crumb();

   
   dbinit();
   
   add_bread_crumb();
   dberrhandle( syb_err_handler );
   dbmsghandle( syb_msg_handler );

   fprintf(stdout, "About to logon\n");
   
   add_bread_crumb();
   login = dblogin();
   DBSETLPWD(login,PASSWORD);
   DBSETLUSER(login,USER);
   DBSETLAPP(login,"t0005");
   DBSETLHOST(login,"ntbox.dntis.ro");

fprintf(stdout, "About to open\n");
   
   add_bread_crumb();
   dbproc = dbopen(login, SERVER);
   if (strlen(DATABASE)) dbuse(dbproc,DATABASE);
   add_bread_crumb();

   add_bread_crumb();
   
   fprintf(stdout, "Dropping table\n");
   add_bread_crumb();
   dbcmd(dbproc, "drop table #dblib0005");
   add_bread_crumb();
   dbsqlexec(dbproc);
   add_bread_crumb();
   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   add_bread_crumb();
   
   fprintf(stdout, "creating table\n");
   if (SUCCEED != dbcmd(dbproc,
                        "create table #dblib0005 "
                        "(i int not null, s char(10) not null)"))
   {
      fprintf(stderr, "%s:%d: dbcmd failed\n", __FILE__, __LINE__);
      failed = 1;
   }
      
   if (SUCCEED != dbsqlexec(dbproc))
   {
      fprintf(stderr, "%s:%d: dbcmd failed\n", __FILE__, __LINE__);
      failed = 1;
   }

   while (dbresults(dbproc)!=NO_MORE_RESULTS)
   {
      /* nop */
   }
   
   fprintf(stdout, "insert\n");
   for(i=1; i<rows_to_add; i++)
   {
      sprintf(cmd, "insert into #dblib0005 values (%d, 'row %04d')", i, i);
      dbcmd(dbproc, cmd);
      dbsqlexec(dbproc);
      while (dbresults(dbproc)!=NO_MORE_RESULTS)
      {
         /* nop */
      }
   }


   sprintf(cmd, "select * from #dblib0005 where i<50 order by i");
   fprintf(stdout, "%s\n", cmd);
   dbcmd(dbproc, cmd);
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

   for(i=1; i<50; i++)
   {
      char   expected[1024]; 
      sprintf(expected, "row %04d", i);

      add_bread_crumb();


      testint = -1;
      strcpy(teststr, "bogus");
      
      add_bread_crumb();
      if (REG_ROW != dbnextrow(dbproc))
      {
         fprintf(stderr, "Failed.  Expected a row\n");
         exit(1);
      }
      add_bread_crumb();
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

#if 0
   add_bread_crumb();
   rc = dbnextrow(dbproc);
   if (rc != NO_MORE_ROWS) 
   {
      fprintf(stderr, "Was expecting no more rows.  (rc=%d)\n", rc);
      exit(1);
   }
   add_bread_crumb();
#endif

   sprintf(cmd, "select * from #dblib0005 where i>950 order by i");
   fprintf(stdout, "%s\n", cmd);
   if (SUCCEED != dbcmd(dbproc, cmd))
   {
      fprintf(stderr, "%s:%d: dbcmd failed\n", __FILE__, __LINE__);
      failed = 1;
   }
   if (SUCCEED == dbsqlexec(dbproc))
   {
      fprintf(stderr, "%s:%d: dbsqlexec should have failed but didn't\n", 
              __FILE__, __LINE__);
      failed = 1;
   }
   add_bread_crumb();

   dbexit();
   add_bread_crumb();

   fprintf(stdout, "dblib %s on %s\n", 
           (failed?"failed!":"okay"),
           __FILE__);
   return failed ? 1 : 0; 
}





