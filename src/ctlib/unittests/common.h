
#ifndef COMMON_h
#define COMMON_h

static char  rcsid_common_h [ ] =
         "$Id: common.h,v 1.5 2002/11/04 19:49:18 castellano Exp $";
static void *no_unused_common_h_warn[]={rcsid_common_h, no_unused_common_h_warn};

extern char PASSWORD[512];
extern char USER[512];
extern char SERVER[512];
extern char DATABASE[512];
extern int cslibmsg_cb_invoked;
extern int clientmsg_cb_invoked;
extern int servermsg_cb_invoked;

CS_RETCODE try_ctlogin(CS_CONTEXT **ctx, CS_CONNECTION **conn, CS_COMMAND **cmd, int verbose);
CS_RETCODE try_ctlogout(CS_CONTEXT *ctx, CS_CONNECTION *conn, CS_COMMAND *cmd, int verbose);
CS_RETCODE run_command(CS_COMMAND *cmd, const char *sql);
CS_RETCODE cslibmsg_cb(CS_CONTEXT *context, CS_CLIENTMSG *errmsg);
CS_RETCODE clientmsg_cb(CS_CONTEXT *context, CS_CONNECTION *connection, CS_CLIENTMSG *errmsg);
CS_RETCODE servermsg_cb(CS_CONNECTION *connection, CS_COMMAND *cmd, CS_SERVERMSG *srvmsg);

#endif
