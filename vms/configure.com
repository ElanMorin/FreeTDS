$! FreeTDS - Library of routines accessing Sybase and Microsoft databases
$! Copyright (C) 2003  Craig A. Berry   craigberry@mac.com      1-FEB-2003
$! 
$! This library is free software; you can redistribute it and/or
$! modify it under the terms of the GNU Library General Public
$! License as published by the Free Software Foundation; either
$! version 2 of the License, or (at your option) any later version.
$! 
$! This library is distributed in the hope that it will be useful,
$! but WITHOUT ANY WARRANTY; without even the implied warranty of
$! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
$! Library General Public License for more details.
$! 
$! You should have received a copy of the GNU Library General Public
$! License along with this library; if not, write to the
$! Free Software Foundation, Inc., 59 Temple Place - Suite 330,
$! Boston, MA 02111-1307, USA.
$!
$! $Id: configure.com,v 1.2 2003/05/20 11:34:41 freddy77 Exp $
$!
$! CONFIGURE.COM -- run from top level source directory as @[.vms]configure
$!
$! Checks for C library functions and applies its findings to 
$! description file template and config.h.  Much of this cribbed
$! from Perl's configure.com, largely the work of Peter Prymmer.
$!
$ SAY := "write sys$output"
$!
$ SEARCH/KEY=(POS:2,SIZE:8) SYS$DISK:[]Configure. "VERSION="/EXACT/OUTPUT=version.tmp
$ open/read version version.tmp
$ read version versionline
$ close version
$ delete/noconfirm/nolog version.tmp;*
$ versionstring = f$element(1, "=", f$edit(versionline, "COLLAPSE"))
$ gosub check_crtl
$!
$! Generate config.h
$!
$ open/write vmsconfigtmp vmsconfigtmp.com
$ write vmsconfigtmp "$ define/user_mode/nolog SYS$OUTPUT _NLA0:"
$ write vmsconfigtmp "$ edit/tpu/nodisplay/noinitialization -"
$ write vmsconfigtmp "/section=sys$library:eve$section.tpu$section -"
$ write vmsconfigtmp "/command=sys$input/output=[.include]config.h [.vms]config_h.vms"
$ write vmsconfigtmp "input_file := GET_INFO (COMMAND_LINE, ""file_name"");"
$ write vmsconfigtmp "main_buffer:= CREATE_BUFFER (""main"", input_file);"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@D_ASPRINTF@"",""''d_asprintf'"");"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@D_VASPRINTF@"",""''d_vasprintf'"");"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@D_STRTOK_R@"",""''d_strtok_r'"");"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@VERSION@"",""""""''versionstring'"""""");"
$ write vmsconfigtmp "out_file := GET_INFO (COMMAND_LINE, ""output_file"");"
$ write vmsconfigtmp "WRITE_FILE (main_buffer, out_file);"
$ write vmsconfigtmp "quit;"
$ write vmsconfigtmp "$ exit"
$ close vmsconfigtmp
$ @vmsconfigtmp.com
$ delete/noconfirm/nolog vmsconfigtmp.com;
$!
$! Generate descrip.mms from template
$!
$ if d_asprintf .eqs. "1" 
$ then
$   asprintfobj = " "
$ else
$   asprintfobj = "[.src.replacements]asprintf$(OBJ),"
$ endif
$
$ if d_vasprintf .eqs. "1" 
$ then
$   vasprintfobj = " "
$ else
$   vasprintfobj = "[.src.replacements]vasprintf$(OBJ),"
$ endif
$
$ if d_strtok_r .eqs. "1" 
$ then
$   strtok_robj = " "
$ else
$   strtok_robj = "[.src.replacements]strtok_r$(OBJ),"
$ endif
$
$ open/write vmsconfigtmp vmsconfigtmp.com
$ write vmsconfigtmp "$ define/user_mode/nolog SYS$OUTPUT _NLA0:"
$ write vmsconfigtmp "$ edit/tpu/nodisplay/noinitialization -"
$ write vmsconfigtmp "/section=sys$library:eve$section.tpu$section -"
$ write vmsconfigtmp "/command=sys$input/output=[]descrip.mms [.vms]descrip_mms.template"
$ write vmsconfigtmp "input_file := GET_INFO (COMMAND_LINE, ""file_name"");"
$ write vmsconfigtmp "main_buffer:= CREATE_BUFFER (""main"", input_file);"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@ASPRINTFOBJ@"",""''asprintfobj'"");"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@VASPRINTFOBJ@"",""''vasprintfobj'"");"
$ write vmsconfigtmp "POSITION (BEGINNING_OF (main_buffer));"
$ write vmsconfigtmp "eve_global_replace(""@STRTOK_ROBJ@"",""''strtok_robj'"");"
$ write vmsconfigtmp "out_file := GET_INFO (COMMAND_LINE, ""output_file"");"
$ write vmsconfigtmp "WRITE_FILE (main_buffer, out_file);"
$ write vmsconfigtmp "quit;"
$ write vmsconfigtmp "$ exit"
$ close vmsconfigtmp
$ @vmsconfigtmp.com
$ delete/noconfirm/nolog vmsconfigtmp.com;
$!
$ Say ""
$ Say "Configuration complete; run MMK or MMS to build."
$ EXIT
$!
$ CHECK_CRTL:
$!
$ OS := "open/write CONFIG []try.c"
$ WS := "write CONFIG"
$ CS := "close CONFIG"
$ DS := "delete/nolog/noconfirm []try.*;*"
$ good_compile = %X10B90001
$ good_link = %X10000001
$ tmp = "" ! null string default
$!
$! Check for asprintf
$!
$ OS
$ WS "#include <stdio.h>"
$ WS "#include <stdlib.h>"
$ WS "int main()"
$ WS "{"
$ WS "char *ptr;
$ WS "asprintf(&ptr,""%d"",1);"
$ WS "exit(0);"
$ WS "}"
$ CS
$ tmp = "asprintf"
$ GOSUB inlibc
$ d_asprintf == tmp
$!
$!
$! Check for vasprintf
$!
$ OS
$ WS "#include <stdarg.h>"
$ WS "#include <stdio.h>"
$ WS "#include <stdlib.h>"
$ WS "int main()"
$ WS "{"
$ WS "char *ptr;
$ WS "vasprintf(&ptr,""%d,%d"",1,2);"
$ WS "exit(0);"
$ WS "}"
$ CS
$ tmp = "vasprintf"
$ GOSUB inlibc
$ d_vasprintf == tmp
$!
$!
$!
$! Check for strtok_r
$!
$ OS
$ WS "#include <stdlib.h>"
$ WS "#include <string.h>"
$ WS "int main()"
$ WS "{"
$ WS "char *word, *brkt, mystr[4];"
$ WS "strcpy(mystr,""1^2"");"
$ WS "word = strtok_r(mystr, ""^"", &brkt);"
$ WS "exit(0);"
$ WS "}"
$ CS
$ tmp = "strtok_r"
$ GOSUB inlibc
$ d_strtok_r == tmp
$!
$ DS
$ RETURN
$!
$!********************
$inlibc: 
$ GOSUB link_ok
$ IF compile_status .EQ. good_compile .AND. link_status .EQ. good_link
$ THEN
$   say "''tmp'() found."
$   tmp = "1"
$ ELSE
$   say "''tmp'() NOT found."
$   tmp = "0"
$ ENDIF
$ RETURN
$!
$!: define a shorthand compile call
$compile:
$ GOSUB link_ok
$just_mcr_it:
$ IF compile_status .EQ. good_compile .AND. link_status .EQ. good_link
$ THEN
$   OPEN/WRITE CONFIG []try.out
$   DEFINE/USER_MODE SYS$ERROR CONFIG
$   DEFINE/USER_MODE  SYS$OUTPUT CONFIG
$   MCR []try.exe
$   CLOSE CONFIG
$   OPEN/READ CONFIG []try.out
$   READ CONFIG tmp
$   CLOSE CONFIG
$   DELETE/NOLOG/NOCONFIRM []try.out;
$   DELETE/NOLOG/NOCONFIRM []try.exe;
$ ELSE
$   tmp = "" ! null string default
$ ENDIF
$ RETURN
$!
$link_ok:
$ GOSUB compile_ok
$ DEFINE/USER_MODE SYS$ERROR _NLA0:
$ DEFINE/USER_MODE SYS$OUTPUT _NLA0:
$ SET NOON
$ LINK try.obj
$ link_status = $status
$ SET ON
$ IF F$SEARCH("try.obj") .NES. "" THEN DELETE/NOLOG/NOCONFIRM try.obj;
$ RETURN
$!
$!: define a shorthand compile call for compilations that should be ok.
$compile_ok:
$ DEFINE/USER_MODE SYS$ERROR _NLA0:
$ DEFINE/USER_MODE SYS$OUTPUT _NLA0:
$ SET NOON
$ CC try.c
$ compile_status = $status
$ SET ON
$ DELETE/NOLOG/NOCONFIRM try.c;
$ RETURN
$!
$beyond_compile_ok:
$!
