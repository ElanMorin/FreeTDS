/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-2002 Brian Bruns
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

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <stdio.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif /* HAVE_READLINE */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "tds.h"
#include "tdsconvert.h"

static char  software_version[]   = "$Id: tsql.c,v 1.39 2002/11/01 20:55:46 castellano Exp $";
static void *no_unused_var_warn[] = {software_version, no_unused_var_warn};

enum {
	OPT_VERSION   = 0x01,
	OPT_TIMER     = 0x02,
	OPT_NOFOOTER  = 0x04,
	OPT_NOHEADER  = 0x08
};

int do_query(TDSSOCKET *tds, char *buf, int opt_flags);
void print_usage(char *progname);
int get_opt_flags(char *s, int *opt_flags);
void populate_login(TDSLOGIN *login, int argc, char **argv);
int tsql_handle_message(TDSCONTEXT *context, TDSSOCKET *tds, TDSMSGINFO *msg);

#ifndef HAVE_READLINE
char *readline(char *prompt);
void add_history(const char *s);

char *
readline(char *prompt)
{
char *buf, line[1000];
int i = 0;

	printf("%s",prompt);
	if (fgets(line, 1000, stdin) == NULL) {
		return NULL;
	}
	for (i=strlen(line);i>0;i--) {
		if (line[i]=='\n') {
			line[i]='\0';
			break;
		}
	}
	buf = (char *) malloc(strlen(line)+1);
	strcpy(buf,line);

	return buf;
}

void
add_history(const char *s)
{
}
#endif

int
do_query(TDSSOCKET *tds, char *buf, int opt_flags)
{
int rows;
int rc, i;
TDSCOLINFO *col;
int ctype;
CONV_RESULT dres;
unsigned char *src;
TDS_INT srclen;
TDS_INT rowtype;
TDS_INT resulttype;
TDS_INT computeid;
struct timeval start, stop;
int print_rows=1;
char message[128];

	rc = tds_submit_query(tds,buf);
	if (rc != TDS_SUCCEED) {
		fprintf(stderr, "tds_submit_query() failed\n");
		return 1;
	}

    while ((rc=tds_process_result_tokens(tds, &resulttype))==TDS_SUCCEED) {
		if (opt_flags & OPT_TIMER) {
			gettimeofday(&start,NULL);
			print_rows = 0;
		}
    switch (resulttype) {
    case TDS_ROWFMT_RESULT:
		if ((!(opt_flags & OPT_NOHEADER)) && tds->res_info) {
			for (i=0; i<tds->res_info->num_cols; i++) {
				fprintf(stdout, "%s\t", tds->res_info->columns[i]->column_name);
			}
			fprintf(stdout,"\n");
		}
      break;
    case TDS_ROW_RESULT:
		rows = 0;
        while ((rc = tds_process_row_tokens(tds, &rowtype, &computeid)) == TDS_SUCCEED) {
			rows++;

			if (!tds->res_info) 
				continue;

			for (i=0; i<tds->res_info->num_cols; i++) {
				if (tds_get_null(tds->res_info->current_row, i)) {
					if (print_rows) fprintf(stdout,"NULL\t");
					continue;
				}
				col = tds->res_info->columns[i];
				ctype = tds_get_conversion_type(col->column_type, col->column_size);

				if (is_blob_type(col->column_type)) {
					src = (unsigned char *)col->column_textvalue;
				} else {
					src = &(tds->res_info->current_row[col->column_offset]);
				}
				srclen = col->column_cur_size;

	 
                    if(tds_convert(tds->tds_ctx,
 					ctype,
					src,
 					srclen,
					SYBVARCHAR,
					&dres) < 0)
			    continue;
				if (print_rows) fprintf(stdout,"%s\t",dres.c);
				free(dres.c);
			}
			if (print_rows) fprintf(stdout,"\n");
    
      }
      break;
    case TDS_STATUS_RESULT:
      printf("(return status = %d)\n", tds->ret_status);
      break;
    default:
      break;
         }
		 if (opt_flags & OPT_VERSION) {
			char version[64];
			int line = 0;
			line = tds_version( tds, version );
			if (line) {
				sprintf(message, "using TDS version %s", version);
				tds_client_msg(tds->tds_ctx, tds, line, line, line, line, message);
			}
		 }
		 if (opt_flags & OPT_TIMER) {
			gettimeofday(&stop,NULL);
			sprintf(message,"Total time for processing %d rows: %ld msecs\n", 
				rows, 
				(long) ((stop.tv_sec - start.tv_sec) * 1000) + 
				((stop.tv_usec -  start.tv_usec) / 1000));
			tds_client_msg(tds->tds_ctx, tds, 1, 1, 1, 1, message);
		 }
	}
	return 0;
}

void print_usage(char *progname)
{
			fprintf(stderr,"Usage: %s [-S <server> | -H <hostname> -p <port>] -U <username> [ -P <password> ] [ -I <config file> ]\n",progname);
}

int get_opt_flags(char *s, int *opt_flags) 
{
char **argv;
int argc=0;
int opt;

	/* make sure we have enough elements */
	argv = malloc(sizeof(char *) * strlen(s));
	if (!argv) return 0;

	/* parse the command line and assign to argv */
	argv[argc++] = strtok(s," ");
	if (argv[0])
		while ((argv[argc++] = strtok(NULL, " ")) != NULL);

	argv[argc]=NULL;

	optind = 0; /* reset getopt */
	while ((opt=getopt(argc - 1, argv, "fhtv"))!=-1) {
          switch (opt) {
          case 'f':
               *opt_flags |= OPT_NOFOOTER;
          break;
          case 'h':
               *opt_flags |= OPT_NOHEADER;
          break;
          case 't':
               *opt_flags |= OPT_TIMER;
          break;
          case 'v':
               *opt_flags |= OPT_VERSION;
          break;
		}
	}
	return *opt_flags;
}
void populate_login(TDSLOGIN *login, int argc, char **argv)
{
char *hostname = NULL;
char *servername = NULL;
char *username = NULL;
char *password = NULL;
char *confile = NULL;
int  port = 0;
int  opt;

     while ((opt=getopt(argc, argv, "H:S:I:V::P:U:p:v"))!=-1) {
          switch (opt) {
          case 'H':
               hostname = (char *) malloc(strlen(optarg)+1);
               strcpy(hostname, optarg);
          break;
          case 'S':
               servername = (char *) malloc(strlen(optarg)+1);
               strcpy(servername, optarg);
          break;
          case 'U':
               username = (char *) malloc(strlen(optarg)+1);
               strcpy(username, optarg);
          break;
          case 'P':
               password = (char *) malloc(strlen(optarg)+1);
               strcpy(password, optarg);
          break;
          case 'I':
               confile = (char *) malloc(strlen(optarg)+1);
               strcpy(confile, optarg);
          break;
          case 'p':
			port = atoi(optarg);
		break;
          default:
			print_usage(argv[0]);
			exit(1);
          break;
          }
     }

	/* validate parameters */
	if (!servername && !hostname) {
			fprintf(stderr,"Missing argument -S or -H\n");
			print_usage(argv[0]);
			exit(1);
	}
	if (hostname && !port) {
			fprintf(stderr,"Missing argument -p \n");
			print_usage(argv[0]);
			exit(1);
	}
	if (!username) { 
			fprintf(stderr,"Missing argument -U \n");
			print_usage(argv[0]);
			exit(1);
	}
	if (!servername && !hostname) {
			print_usage(argv[0]);
			exit(1);
	}
	if (!password) {
			char *tmp = getpass("Password: ");
			password = strdup(tmp);
	}

	/* all validated, let's do it */

	/* if it's a servername */
	if (servername) {
		tds_set_user(login, username);
		tds_set_app(login, "TSQL");
		tds_set_library(login, "TDS-Library");
		tds_set_server(login, servername);
		tds_set_charset(login, "iso_1");
		tds_set_language(login, "us_english");
		tds_set_packet(login, 512);
		tds_set_passwd(login, password);
		if (confile) {
			tds_set_interfaces_file_loc(confile);
		}
	/* else we specified hostname/port */
	} else {
		tds_set_user(login, username);
		tds_set_app(login, "TSQL");
		tds_set_library(login, "TDS-Library");
		tds_set_server(login, hostname);
		tds_set_port(login, port);
		tds_set_charset(login, "iso_1");
		tds_set_language(login, "us_english");
		tds_set_packet(login, 512);
		tds_set_passwd(login, password);
	}

	/* free up all the memory */
	if (hostname) free(hostname);
	if (username) free(username);
	if (password) free(password);
	if (servername) free(servername);
}
int tsql_handle_message(TDSCONTEXT *context, TDSSOCKET *tds, TDSMSGINFO *msg)
{
     if (msg->msg_number > 0
	 && msg->msg_number != 5701
	 && msg->msg_number != 20018) {
		fprintf (stderr, "Msg %d, Level %d, State %d, Server %s, Line %d\n%s\n",
                         msg->msg_number,
                         msg->msg_level,
                         msg->msg_state,
                         msg->server,
                         msg->line_number,
                         msg->message);
	}

	return 0;
}

int 
main(int argc, char **argv)
{
char *s = NULL;
char prompt[20];
int line = 0;
char *mybuf;
int bufsz = 4096;
TDSSOCKET *tds;
TDSLOGIN *login;
TDSCONTEXT *context;
TDSCONNECTINFO *connect_info;
int opt_flags = 0;

	/* grab a login structure */
	login = (void *) tds_alloc_login();

	context = tds_alloc_context();
	if( context->locale && !context->locale->date_fmt ) {
		/* set default in case there's no locale file */
		context->locale->date_fmt = strdup("%b %e %Y %l:%M%p");
	}

	context->msg_handler = tsql_handle_message;
	context->err_handler = tsql_handle_message;

	/* process all the command line args into the login structure */
	populate_login(login, argc, argv);

	/* Try to open a connection*/
	tds = tds_alloc_socket(context, 512);
	tds_set_parent(tds, NULL);
	connect_info = tds_read_config_info(NULL, login, context->locale);
	if (!connect_info || tds_connect(tds, connect_info) == TDS_FAIL) {
		tds_free_connect(connect_info);
		/* FIX ME -- need to hook up message/error handlers */
		fprintf(stderr, "There was a problem connecting to the server\n");
		exit(1);
	}
	tds_free_connect(connect_info);
	/* give the buffer an initial size */
	bufsz = 4096;
	mybuf = (char *) malloc(bufsz);
	mybuf[0]='\0';

	for (;;) {
		sprintf(prompt,"%d> ",++line);
		if (s)
			free(s);
		s = readline(prompt);
		if (!s || !strcmp(s, "exit") || !strcmp(s, "quit") || !strcmp(s, "bye")) {
			break;
		}
		if (!strcmp(s, "version")) {
			tds_version(tds, mybuf);
			printf("using TDS version %s\n", mybuf);
			line = 0;
			mybuf[0] = '\0';
			continue;
		}
		if (! strncmp(s,"go",2)) {
			line = 0;
			get_opt_flags(s, &opt_flags);
			do_query(tds, mybuf, opt_flags);
			mybuf[0]='\0';
		} else if (!strcmp(s, "reset")) {
			line = 0;
			mybuf[0]='\0';
		} else {
			while (strlen(mybuf) + strlen(s) > bufsz) {
				bufsz *= 2;
				mybuf = (char *) realloc(mybuf, bufsz);
			}
			add_history(s);
			strcat(mybuf,s);
			/* preserve line numbering for the parser */
			strcat(mybuf,"\n");
		}
	}

	/* close up shop */
	tds_free_socket(tds);
	tds_free_login(login);
	tds_free_context(context);

	return 0;
}


