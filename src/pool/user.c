/* TDSPool - Connection pooling for TDS based databases
 * Copyright (C) 2001 Brian Bruns
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */

#include "pool.h"
#include "tdssrv.h"

static char software_version[] = "$Id: user.c,v 1.13 2003/04/03 09:10:41 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

extern int waiters;

void
pool_user_init(TDS_POOL * pool)
{
	/* allocate room for pool users */

	pool->users = (TDS_POOL_USER *)
		malloc(sizeof(TDS_POOL_USER) * MAX_POOL_USERS);
	memset(pool->users, '\0', sizeof(TDS_POOL_USER) * MAX_POOL_USERS);
}


/*
** pool_user_create
** accepts a client connection and adds it to the users list and returns it
*/
TDS_POOL_USER *
pool_user_create(TDS_POOL * pool, int s, struct sockaddr_in *sin)
{
	TDS_POOL_USER *puser;
	int fd;
	size_t len;

	/* FIX ME -- the accepted connections just grow until we run out */
	puser = (TDS_POOL_USER *) & pool->users[pool->max_users];
	fprintf(stderr, "accepting connection\n");
	len = sizeof(struct sockaddr);
	if ((fd = accept(s, (struct sockaddr *) sin, &len)) < 0) {
		perror("accept");
		return NULL;
	}
	puser->tds = tds_alloc_socket(NULL, BLOCKSIZ);
	tds_set_parent(puser->tds, NULL);
	/* FIX ME - little endian emulation should be config file driven */
	puser->tds->emul_little_endian = 1;
	puser->tds->in_buf = (unsigned char *) malloc(BLOCKSIZ);
	memset(puser->tds->in_buf, 0, BLOCKSIZ);
	puser->tds->s = fd;
	puser->tds->out_flag = 0x02;
	puser->user_state = TDS_SRV_LOGIN;
	pool->max_users++;
	return puser;
}

/* 
** pool_free_user
** close out a disconnected user.
*/
void
pool_free_user(TDS_POOL_USER * puser)
{
	/* make sure to decrement the waiters list if he is waiting */
	if (puser->user_state == TDS_SRV_WAIT)
		waiters--;
	tds_free_socket(puser->tds);
	memset(puser, '\0', sizeof(TDS_POOL_USER));
}

/* 
** pool_process_users
** check the fd_set for user input, allocate a pool member to it, and forward
** the query to that member.
*/
int
pool_process_users(TDS_POOL * pool, fd_set * fds)
{
	TDS_POOL_USER *puser;
	int i;
	int cnt = 0;

	for (i = 0; i < pool->max_users; i++) {

		puser = (TDS_POOL_USER *) & pool->users[i];

		if (!puser->tds)
			continue;	/* dead connection */

		if (FD_ISSET(puser->tds->s, fds)) {
			cnt++;
			switch (puser->user_state) {
			case TDS_SRV_LOGIN:
				if (pool_user_login(pool, puser)) {
					/* login failed...free socket */
					pool_free_user(puser);
				}
				/* otherwise we have a good login */
				break;
			case TDS_SRV_IDLE:
				pool_user_read(pool, puser);
				break;
			case TDS_SRV_QUERY:
				/* what is this? a cancel perhaps */
				pool_user_read(pool, puser);
				break;
			}	/* switch */
		}		/* if */
	}			/* for */
	return cnt;
}

/*
** pool_user_login
** Reads clients login packet and forges a login acknowledgement sequence 
*/
int
pool_user_login(TDS_POOL * pool, TDS_POOL_USER * puser)
{
	TDSSOCKET *tds;
	TDSLOGIN login;

/* FIX ME */
	char msg[256];

	tds = puser->tds;
	tds_read_login(tds, &login);
	dump_login(&login);
	if (!strcmp(login.user_name, pool->user) && !strcmp(login.password, pool->password)) {
		tds->out_flag = 4;
		tds_env_change(tds, 1, "master", pool->database);
		sprintf(msg, "Changed database context to '%s'.", pool->database);
		tds_send_msg(tds, 5701, 2, 10, msg, "JDBC", "ZZZZZ", 1);
		if (!login.suppress_language) {
			tds_env_change(tds, 2, NULL, "us_english");
			tds_send_msg(tds, 5703, 1, 10, "Changed language setting to 'us_english'.", "JDBC", "ZZZZZ", 1);
		}
		tds_env_change(tds, 4, NULL, "512");
		tds_send_login_ack(tds, "sql server");
		/* tds_send_capabilities_token(tds); */
		tds_send_253_token(tds, 0, 1);
		puser->user_state = TDS_SRV_IDLE;

		/* send it! */
		tds_flush_packet(tds);

		return 0;
	} else {
		/* send nack before exiting */
		return 1;
	}
}

/*
** pool_user_read
** checks the packet type of data coming from the client and allocates a 
** pool member if necessary.
*/
void
pool_user_read(TDS_POOL * pool, TDS_POOL_USER * puser)
{
	TDSSOCKET *tds;

	tds = puser->tds;
	tds->in_len = read(tds->s, tds->in_buf, BLOCKSIZ);
	if (tds->in_len == 0) {
		fprintf(stderr, "user disconnected\n");
		pool_free_user(puser);
		return;
	} else if (tds->in_len == -1) {
		perror("read");
	} else {
		dump_buf(tds->in_buf, tds->in_len);
		if (tds->in_buf[0] == 0x01) {
			pool_user_query(pool, puser);
		} else if (tds->in_buf[0] == 0x06) {
			/* cancel */
		} else {
			fprintf(stderr, "Unrecognized packet type, closing user\n");
			pool_free_user(puser);
		}
	}
	/* fprintf(stderr,"read %d bytes from conn %d\n",len,i); */
}
void
pool_user_query(TDS_POOL * pool, TDS_POOL_USER * puser)
{
	TDS_POOL_MEMBER *pmbr;

	puser->user_state = TDS_SRV_QUERY;
	pmbr = pool_find_idle_member(pool);
	if (!pmbr) {
		/* 
		 * ** put into wait state 
		 * ** check when member is deallocated
		 */
		fprintf(stderr, "Not enough free members...placing user in WAIT\n");
		puser->user_state = TDS_SRV_WAIT;
		waiters++;
	} else {
		pmbr->state = TDS_QUERYING;
		pmbr->current_user = puser;
		write(pmbr->tds->s, puser->tds->in_buf, puser->tds->in_len);
	}
}
