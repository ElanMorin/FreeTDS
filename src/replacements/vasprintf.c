/*
 * vasprintf(3)
 * 20020809 entropy@tappedin.com
 * public domain.  no warranty.  use at your own risk.  have a nice day.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include "replacements.h"

static char  software_version[]   = "$Id: vasprintf.c,v 1.5 2002/10/07 15:44:58 freddy77 Exp $";
static void *no_unused_var_warn[] = {software_version,
                                     no_unused_var_warn};


#define CHUNKSIZE 512
int
vasprintf(char **ret, const char *fmt, va_list ap)
{
#if HAVE_VSNPRINTF
  int chunks;
  size_t buflen;
  char *buf;
  int len;

  chunks = ((strlen(fmt) + 1) / CHUNKSIZE) + 1;
  buflen = chunks * CHUNKSIZE;
  for (;;) {
    if ((buf = malloc(buflen)) == NULL) {
      *ret = NULL;
      return -1;
    }
    len = vsnprintf(buf, buflen, fmt, ap);
    if (len >=0 && len < buflen) {
      break;
    free(buf);
    buflen = (++chunks) * CHUNKSIZE;
    if (len >= buflen) {
      buflen = len + 1;
    }
  }
  *ret = buf;
  return len;
#else /* HAVE_VSNPRINTF */
#ifdef _REENTRANT
  FILE *fp;
#else
  static FILE *fp = NULL;
#endif
  int len;
  char *buf;

  *ret = NULL;

#ifdef _REENTRANT
  if ((fp = fopen("/dev/null", "w")) == NULL)
    return -1;
#else
  if ((fp == NULL) && ((fp = fopen("/dev/null", "w")) == NULL))
    return -1;
#endif

  len = vfprintf(fp, fmt, ap);

#ifdef _REENTRANT
  if (fclose(fp) != 0)
    return -1;
#endif

  if (len < 0)
    return len;
  if ((buf = malloc(len + 1)) == NULL)
    return -1;
  if (vsprintf(buf, fmt, ap) != len)
    return -1;
  *ret = buf;
  return len;
#endif /* HAVE_VSNPRINTF */
}
