#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <cspublic.h>
#include <ctpublic.h>
#include "common.h"

static char software_version[] = "$Id: cs_config.c,v 1.2 2003/11/01 23:02:16 jklowden Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

int
main(int argc, char **argv)
{
	int verbose = 1;
	CS_CONTEXT *ctx;

    CS_CHAR string_in[16], string_out[16];
    CS_INT  int_in,        int_out;
	CS_INT len, len1, ret_len, ret_len2;
	CS_CHAR return_name[16], return_name2[16], return_name_1[16], return_name_2[16], return_name_3[16];

	if (verbose) {
		fprintf(stdout, "Trying cs_config with CS_USERDATA\n\n");
	}

	if (cs_ctx_alloc(CS_VERSION_100, &ctx) != CS_SUCCEED) {
		fprintf(stderr, "cs_ctx_alloc() for first context failed\n");
	}
	if (ct_init(ctx, CS_VERSION_100) != CS_SUCCEED) {
		fprintf(stderr, "ct_init() for first context failed\n");
	}

	fprintf(stdout, "Testing CS_SET/GET USERDATA with char array\n");

	strcpy(string_in,"FreeTDS");

	if (cs_config(ctx, CS_SET, CS_USERDATA, (CS_VOID *)string_in,  CS_NULLTERM, NULL)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() set failed\n");
		return 1;
	}
	if (cs_config(ctx, CS_GET, CS_USERDATA, (CS_VOID *)string_out, 16, &ret_len)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() get failed\n");
		return 1;
	}

	if (strcmp(string_in, string_out)) {
		fprintf(stdout, "returned value >%s< not as stored >%s<\n", (char *)string_out, (char *)string_in);
		return 1;
	}
    if (ret_len != (strlen(string_in) + 1)) {
		fprintf(stdout, "returned length >%d< not as expected >%d<\n", ret_len, (strlen(string_in) + 1));
		return 1;
	}

	fprintf(stdout, "Testing CS_SET/GET USERDATA with char array\n");

	strcpy(string_in,"FreeTDS");

	if (cs_config(ctx, CS_SET, CS_USERDATA, (CS_VOID *)string_in,  CS_NULLTERM, NULL)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() set failed\n");
		return 1;
	}

    strcpy(string_out,"XXXXXXXXXXXXXXX");

	if (cs_config(ctx, CS_GET, CS_USERDATA, (CS_VOID *)string_out, 4, &ret_len)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() get failed\n");
		return 1;
	}

	if (strcmp(string_out, "FreeXXXXXXXXXXX")) {
		fprintf(stdout, "returned value >%s< not as expected >%s<\n", (char *)string_out, "FreeXXXXXXXXXXX");
		return 1;
	}
    if (ret_len != (strlen(string_in) + 1)) {
		fprintf(stdout, "returned length >%d< not as expected >%d<\n", ret_len, (strlen(string_in) + 1));
		return 1;
	}

	fprintf(stdout, "Testing CS_SET/GET USERDATA with int\n");

    int_in = 255;

	if (cs_config(ctx, CS_SET, CS_USERDATA, (CS_VOID *)&int_in,  sizeof(int), NULL)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() set failed\n");
		return 1;
	}
	if (cs_config(ctx, CS_GET, CS_USERDATA, (CS_VOID *)&int_out, sizeof(int), &ret_len)
	    != CS_SUCCEED) {
		fprintf(stderr, "cs_config() get failed\n");
		return 1;
	}

	if (int_in != int_out) {
		fprintf(stdout, "returned value >%d< not as stored >%d<\n", int_out, int_in);
		return 1;
	}
    if (ret_len != (sizeof(int))) {
		fprintf(stdout, "returned length >%d< not as expected >%d<\n", ret_len, sizeof(int));
		return 1;
	}



    return 0;
}
