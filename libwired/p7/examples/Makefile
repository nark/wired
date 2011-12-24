# Makefile.in

abs_top_srcdir	= /Users/nark/Development/Me/Cocoa/Wired/wired/wired/libwired
rundir			= ../../../run/libwired

DEFS			= -DHAVE_CONFIG_H
CC				= gcc
CFLAGS			= -g -O2
CPPFLAGS		= -DWI_CORESERVICES -DWI_CARBON -DWI_DIGESTS -DWI_CIPHERS -DWI_PTHREADS -I/usr/include/libxml2 -DWI_LIBXML2 -DWI_PLIST -DWI_SQLITE3 -DWI_RSA -DWI_ZLIB -DWI_P7
LDFLAGS			= -L$(rundir)/lib 
LIBS			= -lwired -framework CoreServices -framework Carbon -lxml2 -lsqlite3 -lcrypto -lz
INCLUDES		= -I$(abs_top_srcdir) -I$(rundir)/include

COMPILEANDLINK	= $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

wiredclient: wiredclient.c
	$(COMPILEANDLINK) $(LIBS) $<
