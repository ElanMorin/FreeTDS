/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-1999  Brian Bruns
 * Copyright (C) 2004 Frediano Ziglio
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

#ifndef CONVERT_TDS2SQL_h
#define CONVERT_TDS2SQL_h

/* $Id: convert_tds2sql.h,v 1.6 2004/12/08 20:30:05 freddy77 Exp $ */


TDS_INT convert_tds2sql(TDSCONTEXT * context, int srctype, TDS_CHAR * src, TDS_UINT srclen, int desttype, TDS_CHAR * dest,
			SQLULEN destlen, const struct _drecord *drec_ixd);

#endif
