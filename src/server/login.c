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

#include <config.h>
#include "tds.h"
#include "tdsutil.h"
#include <unistd.h>

static char  software_version[]   = "$Id: login.c,v 1.5 2002/07/05 03:43:38 brianb Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};

unsigned char *
tds7_decrypt_pass (const unsigned char *crypt_pass, int len,unsigned char *clear_pass) 
{
int i;
const unsigned char xormask=0x5A;
unsigned char hi_nibble,lo_nibble ;
	for(i=0;i<len;i++) {
		lo_nibble=(crypt_pass[i] << 4) ^ (xormask & 0xF0);
		hi_nibble=(crypt_pass[i] >> 4) ^ (xormask & 0x0F);
		clear_pass[i]=hi_nibble | lo_nibble;
	}
	return clear_pass;
}

TDSSOCKET *tds_listen(int ip_port) 
{
TDSSOCKET	*tds;
struct sockaddr_in      sin;
unsigned char buf[BUFSIZ];
int	fd, s;
size_t	len;

        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons((short)ip_port);
        sin.sin_family = AF_INET;

        if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
                perror ("socket");
                exit (1);
        }
        if (bind (s, (struct sockaddr *) &sin, sizeof (sin)) < 0) {
                perror("bind");
                exit (1);
        }
        listen (s, 5);
        if ((fd = accept (s, (struct sockaddr *) &sin, &len)) < 0) {
        	perror("accept");
        	exit(1);
        }
	tds = tds_alloc_socket(BUFSIZ);
	tds->s = fd;
	tds->out_flag=0x02;
	/* get_incoming(tds->s); */
	return tds;
}
int tds_read_login(TDSSOCKET *tds, TDSLOGIN *login)
{
int len,i;
char blockstr[7];
/*
	while (len = tds_read_packet(tds)) {
		for (i=0;i<len;i++)
			printf("%d %d %c\n",i, tds->in_buf[i], (tds->in_buf[i]>=' ' && tds->in_buf[i]<='z') ? tds->in_buf[i] : ' ');
	}	
*/
	tds_read_string(tds, login->host_name, 30);
	tds_read_string(tds, login->user_name, 30);
	tds_read_string(tds, login->password, 30);
	tds_read_string(tds, NULL, 30); /* host process, junk for now */
	tds_read_string(tds, NULL, 15); /* magic */
	tds_read_string(tds, login->app_name, 30); 
	tds_read_string(tds, login->server_name, 30); 
	tds_read_string(tds, NULL, 255); /* secondary passwd...encryption? */
	login->major_version = tds_get_byte(tds);
	login->minor_version = tds_get_byte(tds);
	tds_get_smallint(tds); /* unused part of protocol field */
	tds_read_string(tds, login->library, 10); 
	tds_get_byte(tds); /* program version, junk it */
	tds_get_byte(tds);
	tds_get_smallint(tds); 
	tds_get_n(tds, NULL, 3); /* magic */
	tds_read_string(tds, login->language, 30); 
	tds_get_n(tds, NULL, 14); /* magic */
	tds_read_string(tds, login->char_set, 30); 
	tds_get_n(tds, NULL, 1); /* magic */
	tds_read_string(tds, blockstr, 6); 
	printf("block size %s\n",blockstr);
	login->block_size=atoi(blockstr);
	tds_get_n(tds, NULL, tds->in_len - tds->in_pos); /* read junk at end */
}
int tds7_read_login(TDSSOCKET *tds,TDSLOGIN *login)
{
int a; 
int host_name_len,user_name_len,password_len,app_name_len,server_name_len;
int library_name_len,language_name_len;
unsigned char *unicode_string;
char *buf;

	a=tds_get_smallint(tds); /*total packet size*/
	tds_get_n(tds,NULL,5);
	a=tds_get_byte(tds);     /*TDS version*/
	login->major_version=a>>4;
	login->minor_version=a<<4;
	tds_get_n(tds,NULL,3);   /*rest of TDS Version which is a 4 byte field*/
	tds_get_n(tds,NULL,4);   /*desired packet size being requested by client*/
	tds_get_n(tds,NULL,21);  /*magic1*/
	a=tds_get_smallint(tds); /*current position*/
	host_name_len=tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	user_name_len=tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	password_len=tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	app_name_len=tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	server_name_len=tds_get_smallint(tds);
	tds_get_smallint(tds);
	tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	library_name_len=tds_get_smallint(tds);
	a=tds_get_smallint(tds); /*current position*/
	language_name_len=tds_get_smallint(tds);
	tds_get_smallint(tds);
	tds_get_smallint(tds);
	tds_get_n(tds,NULL,6);  /*magic2*/
	a=tds_get_smallint(tds); /*partial packet size*/
	a=tds_get_smallint(tds); /*0x30*/
	a=tds_get_smallint(tds); /*total packet size*/
	tds_get_smallint(tds);

	unicode_string = (unsigned char *) malloc(host_name_len*2);
	buf = (unsigned char *) malloc(host_name_len*2);
	tds_get_n(tds,unicode_string,host_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,host_name_len);
	strncpy(login->host_name,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(user_name_len*2);
	buf = (unsigned char *) malloc(user_name_len*2);
	tds_get_n(tds,unicode_string,user_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,user_name_len);
	strncpy(login->user_name,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(password_len*2);
	buf = (unsigned char *) malloc(password_len*2);
	tds_get_n(tds,unicode_string,password_len*2);
	tds7_decrypt_pass(unicode_string,password_len*2,unicode_string);
	tds7_unicode2ascii(tds,unicode_string,buf,password_len);
	strncpy(login->password,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(app_name_len*2);
	buf = (unsigned char *) malloc(app_name_len*2);
	tds_get_n(tds,unicode_string,app_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,app_name_len);
	strncpy(login->app_name,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(server_name_len*2);
	buf = (unsigned char *) malloc(server_name_len*2);
	tds_get_n(tds,unicode_string,server_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,server_name_len);
	strncpy(login->server_name,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(library_name_len*2);
	buf = (unsigned char *) malloc(library_name_len*2);
	tds_get_n(tds,unicode_string,library_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,library_name_len);
	strncpy(login->library,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	unicode_string = (unsigned char *) malloc(language_name_len*2);
	buf = (unsigned char *) malloc(language_name_len*2);
	tds_get_n(tds,unicode_string,language_name_len*2);
	tds7_unicode2ascii(tds,unicode_string,buf,language_name_len);
	strncpy(login->language,buf,TDS_MAX_LOGIN_STR_SZ);
	free(unicode_string);
	free(buf);

	tds_get_n(tds,NULL,7);  /*magic3*/
	tds_get_byte(tds);
	tds_get_byte(tds);
	tds_get_n(tds,NULL,3);
	tds_get_byte(tds);
	a=tds_get_byte(tds);    /*0x82*/
	tds_get_n(tds,NULL,22);
	tds_get_byte(tds);      /*0x30*/
	tds_get_n(tds,NULL,7);
	a=tds_get_byte(tds);    /*0x30*/
	tds_get_n(tds,NULL,3);
	strcpy(login->char_set,""); /*empty char_set for TDS 7.0*/
	login->block_size=0;        /*0 block size for TDS 7.0*/
	login->encrypted=1;
	return(0);

}
int tds_read_string(TDSSOCKET *tds, char *dest, int size)
{
char *tempbuf;
int len;

	tempbuf = (char *) malloc(size+1);
	tds_get_n(tds,tempbuf,size);
	len=tds_get_byte(tds);
	if (dest) {
		memcpy(dest,tempbuf,len);
		dest[len]='\0';
	}
	free(tempbuf);

	return len;
}
