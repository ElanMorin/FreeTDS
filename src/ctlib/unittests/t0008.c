#include <stdio.h>
#include <cspublic.h>
#include <ctpublic.h>
#include "common.h"

static char software_version[] = "$Id: t0008.c,v 1.1 2002/09/23 23:45:29 castellano Exp $";
static void *no_unused_var_warn[] = {software_version, no_unused_var_warn};

/*
 * ct_send SQL |select name = @@servername|
 * ct_bind variable
 * ct_fetch and print results
 */
int
main()
{
int verbose = 1;
CS_CONTEXT *ctx; 
CS_CONNECTION *conn; 
CS_COMMAND *cmd; 
CS_RETCODE ret;
CS_DATAFMT srcfmt;
CS_INT src = 32768;
CS_DATAFMT dstfmt;
CS_SMALLINT dst;
CS_INT result_type;

	fprintf(stdout, "%s: Testing context callbacks\n", __FILE__);
	srcfmt.datatype = CS_INT_TYPE;
	srcfmt.maxlength = sizeof(CS_INT);
	srcfmt.locale = NULL;
	dstfmt.datatype = CS_SMALLINT_TYPE;
	dstfmt.maxlength = sizeof(CS_SMALLINT);
	dstfmt.locale = NULL;

	if (verbose) {
		fprintf(stdout, "Trying clientmsg_cb with context\n");
	}
	if (cs_ctx_alloc(CS_VERSION_100, &ctx) != CS_SUCCEED) {
		fprintf(stderr, "cs_ctx_alloc() failed\n");
	}
	if (ct_init(ctx, CS_VERSION_100) != CS_SUCCEED) {
		fprintf(stderr, "ct_init() failed\n");
	}

	if (ct_callback(ctx, NULL, CS_SET, CS_CLIENTMSG_CB, clientmsg_cb)
		!= CS_SUCCEED) {
		fprintf(stderr, "ct_callback() failed\n");
		return 1;
	}
	clientmsg_cb_invoked = 0;
	cs_convert(ctx, &srcfmt, &src, &dstfmt, &dst, NULL);
	if (clientmsg_cb_invoked != 1) {
		fprintf(stderr, "clientmsg_cb was not invoked!\n");
		return 1;
	}
	if (cs_ctx_drop(ctx) != CS_SUCCEED) {
		fprintf(stderr, "cx_ctx_drop() failed\n");
	}

	if (verbose) {
		fprintf(stdout, "Trying login\n");
	}
	ret = try_ctlogin(&ctx, &conn, &cmd, verbose);
	if (ret != CS_SUCCEED) {
	fprintf(stderr, "Login failed\n");
		return 1;
	}

	if (verbose) {
		fprintf(stdout, "Trying clientmsg_cb with connection\n");
	}
	ret = ct_callback(NULL, conn, CS_SET, CS_CLIENTMSG_CB,
		(CS_VOID *) clientmsg_cb);
	if (ret != CS_SUCCEED) {
		fprintf(stderr, "ct_callback() failed\n");
		return 1;
	}
	clientmsg_cb_invoked = 0;
	ret = run_command(cmd, ".");
	if (ret != CS_SUCCEED) {
		fprintf(stderr, "run_command() failed\n");
		return 1;
	}
	if (clientmsg_cb_invoked != 1) {
		fprintf(stderr, "clientmsg_cb was not invoked!\n");
		return 1;
	}

#if 1
	if (verbose) {
		fprintf(stdout, "Trying servermsg_cb with connection\n");
	}
	ret = ct_callback(NULL, conn, CS_SET, CS_SERVERMSG_CB,
		(CS_VOID *) servermsg_cb);
	if (ret != CS_SUCCEED) {
		fprintf(stderr, "ct_callback() failed\n");
		return 1;
	}
	servermsg_cb_invoked = 0;
#if 0
	ret = run_command(cmd, "raiserror 99999 'This is a test'");
#else
	ret = run_command(cmd, "print 'This is a test'");
#endif
	if (ret != CS_SUCCEED) {
		fprintf(stderr, "run_command() failed\n");
		return 1;
	}
	if (servermsg_cb_invoked == 0) {
		fprintf(stderr, "servermsg_cb was not invoked!\n");
		return 1;
	}
#endif

	if (verbose) {
		fprintf(stdout, "Trying logout\n");
	}
	ret = try_ctlogout(ctx, conn, cmd, verbose);
	if (ret != CS_SUCCEED) {
		fprintf(stderr, "Logout failed\n");
		return 1;
	}

	return 0;
}
