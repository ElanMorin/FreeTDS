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

static char  software_version[]   = "$Id: common.c,v 1.1 2001/10/12 23:29:11 brianb Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};

typedef struct _tag_memcheck_t
{
   int                      item_number;
   int                      special;
   struct _tag_memcheck_t  *next;
} memcheck_t;


static memcheck_t *breadcrumbs      = NULL;
static int         num_breadcrumbs  = 0;
static const int   BREADCRUMB       = 0xABCD7890;


void check_crumbs()
{
   int           i;
   memcheck_t   *ptr = breadcrumbs;

   i = num_breadcrumbs;
   while(ptr != NULL)
   {
      if (ptr->special!=BREADCRUMB || ptr->item_number!=i)
      {
         fprintf(stderr, "Somebody overwrote one of the bread crumbs!!!\n");
         abort();
      }
         
      i--;
      ptr = ptr->next;
   }
}


   
void add_bread_crumb()
{
   memcheck_t   *tmp;

   check_crumbs();

   tmp = (memcheck_t *)calloc(sizeof(memcheck_t), 1);
   if (tmp==NULL)
   {
      fprintf(stderr, "Out of memory");
      abort();
      exit(1);
   }

   num_breadcrumbs++;
   
   tmp->item_number = num_breadcrumbs;
   tmp->special     = BREADCRUMB;
   tmp->next        = breadcrumbs;
   
   breadcrumbs = tmp;
}
      
int syb_msg_handler(
   DBPROCESS   *dbproc,
   DBINT        msgno,
   int          msgstate,
   int          severity,
   char        *msgtext,
   char        *srvname,
   char        *procname,
   int          line)
{
      char    var_value[31] ;
      int     i ;
      char   *c ;

      /*
       * Check for "database changed", or "language changed" messages from
       * the client.  If we get one of these, then we need to pull the
       * name of the database or charset from the message and set the
       * appropriate variable.
       */
      if( msgno == 5701 ||    /* database context change */
               msgno == 5703 ||    /* language changed */
               msgno == 5704 ) {   /* charset changed */

              /* fprintf( stderr, "msgno = %d: %s\n", msgno, msgtext ) ; */

              if( msgtext != NULL && (c = strchr( msgtext, '\'' )) != NULL ) {
		      i = 0 ;
                      for( ++c; i <= 30 && *c != '\0' && *c != '\''; ++c )
                              var_value[i++] = *c ;
                      var_value[i] = '\0' ;

#if 0
                      switch(msgno) {
                              case 5701 :
                                      env_set( g_env, "database", var_value )
;
                                      break ;
                              case 5703 :
                                      env_set( g_env, "language", var_value )
;
                                      break ;
                              case 5704 :
                                      env_set( g_env, "charset", var_value ) ;-                                       break ;
                              default :
                                      break ;
                      }
#endif
              }
              return 0 ;
      }

      /*
       * If the severity is something other than 0 or the msg number is
       * 0 (user informational messages).
       */
      if( severity >= 0 || msgno == 0 ) {
              /*
               * If the message was something other than informational, and
               * the severity was greater than 0, then print information to
               * stderr with a little pre-amble information.
               */
              if( msgno > 0 && severity > 0 ) {
                      fprintf( stderr, "Msg %d, Level %d, State %d\n",
                                              (int)msgno, (int)severity, (int)msgstate ) ;
                      fprintf( stderr, "Server '%s'", srvname ) ;
                      if( procname != NULL && *procname != '\0' )
                              fprintf( stderr, ", Procedure '%s'", procname )
;
                      if( line > 0 )
                              fprintf( stderr, ", Line %d", line ) ;
                      fprintf( stderr, "\n" ) ;
                      fprintf( stderr,"%s\n", msgtext ) ;
                      fflush( stderr ) ;
              } else {
                      /*
                       * Otherwise, it is just an informational (e.g. print) message
                       * from the server, so send it to stdout.
                       */
                      fprintf( stdout, "%s\n", msgtext ) ;
                      fflush( stdout ) ;
              }
      }

      return 0 ;
}

int syb_err_handler( 
   DBPROCESS    *dbproc,
   int           severity,
   int           dberr,
   int           oserr,
   char         *dberrstr,
   char         *oserrstr)
{

#if 0
      /*
       * For fatal server messages, cancel the query and rely on the
       * message handler to spew the appropriate error messages out.
       */
      if (severity == EXSERVER )
              return INT_CANCEL ;

      /*
       * For any other type of severity (that is not a server
       * message), we increment the batch_failcount.
       */
      env_set( g_env, "batch_failcount", "1" ) ;
#endif

      fprintf( stderr, "DB-LIBRARY error (severity %d):\n", severity ) ;
      fprintf( stderr, "   %s\n", dberrstr ) ;
      fflush( stderr ) ;

      /*
       * If the dbprocess is dead or the dbproc is a NULL pointer and
       * we are not in the middle of logging in, then we need to exit.
       * We can't do anything from here on out anyway.
       */
      if( (dbproc == NULL || DBDEAD(dbproc)) )
              exit(255) ;

      return INT_CANCEL ;
}
