# Makefile.in

abs_top_srcdir		= /Users/nark/Development/Me/Cocoa/Wired/wired2.5/wired
datarootdir			= ${prefix}/share
exec_prefix			= ${prefix}
fake_prefix			= /Library
installdir			= $(prefix)/$(wireddir)
objdir				= obj
rundir				= run
mandir				= ${datarootdir}/man
prefix				= /usr/local
wireddir			= Wired

WD_VERSION			= 2.5
WD_MAINTAINER		= 0
WD_USER				= nark
WD_GROUP			= daemon

DISTFILES			= INSTALL LICENSE NEWS README Makefile Makefile.in \
					  config.guess config.status config.h.in config.sub configure \
					  configure.in install-sh libwired man run thirdparty wired
SUBDIRS				= libwired

WIREDOBJS			= $(addprefix $(objdir)/wired/,$(notdir $(patsubst %.c,%.o,$(shell find $(abs_top_srcdir)/wired -name "[a-z]*.c"))))
NATPMPOBJS			= $(addprefix $(objdir)/natpmp/,$(notdir $(patsubst %.c,%.o,$(shell find $(abs_top_srcdir)/thirdparty/natpmp -name "[a-z]*.c"))))
MINIUPNPCOBJS		= $(addprefix $(objdir)/miniupnpc/,$(notdir $(patsubst %.c,%.o,$(shell find $(abs_top_srcdir)/thirdparty/miniupnpc -name "[a-z]*.c"))))
TRANSFERTESTOBJS	= $(addprefix $(objdir)/transfertest/,$(notdir $(patsubst %.c,%.o,$(shell find $(abs_top_srcdir)/test/transfertest -name "[a-z]*.c"))))

DEFS				= -DHAVE_CONFIG_H -DENABLE_STRNATPMPERR -DMINIUPNPC_SET_SOCKET_TIMEOUT
CC					= gcc
CFLAGS				= -g -O2
CPPFLAGS			= -I/usr/local/opt/openssl/include -I/usr/local/include -DWI_PTHREADS -DWI_CORESERVICES -DWI_CARBON -DWI_DIGESTS -DWI_CIPHERS -DWI_SQLITE3 -DWI_RSA -DWI_LIBXML2 -DWI_PLIST -DWI_ZLIB -DWI_P7
LDFLAGS				= -L$(rundir)/libwired/lib -L/usr/local/opt/openssl/lib -L/usr/local/lib
LIBS				= -lwired -framework CoreServices -framework Carbon -lsqlite3 -lcrypto -lxml2 -lz
INCLUDES			= -I$(abs_top_srcdir) -I$(rundir)/libwired/include -I$(abs_top_srcdir)/thirdparty

INSTALL				= /usr/local/bin/ginstall -c
COMPILE				= $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS)
PREPROCESS			= $(CC) -E $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS)
DEPEND				= $(CC) -MM $(INCLUDES)
LINK				= $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@
ARCHIVE				= ar rcs $@

.PHONY: all all-recursive clean-recursive distclean-recursive install install-only install-wired install-man dist clean distclean scmclean
.NOTPARALLEL:

all: all-recursive $(rundir)/wired $(rundir)/wiredctl $(rundir)/etc/wired.conf

ifeq ($(WD_MAINTAINER), 1)
all: Makefile configure config.h.in $(rundir)/transfertest

Makefile: Makefile.in config.status
	./config.status
	        
configure: configure.in
	autoconf

config.h.in: configure.in
	autoheader
	touch $@
	rm -f $@~
endif

all-recursive clean-recursive distclean-recursive:
	@list='$(SUBDIRS)'; \
	for subdir in $$list; do \
		target=`echo $@ | sed s/-recursive//`; \
		(cd $$subdir && $(MAKE) -e $$target) || exit 1; \
	done

$(rundir)/wired: $(WIREDOBJS) $(NATPMPOBJS) $(MINIUPNPCOBJS) $(rundir)/libwired/lib/libwired.a
	@test -d $(@D) || mkdir -p $(@D)
	$(LINK) $(WIREDOBJS) $(NATPMPOBJS) $(MINIUPNPCOBJS) $(LIBS)

$(rundir)/wiredctl: $(abs_top_srcdir)/wired/wiredctl.in
	@test -d $(@D) || mkdir -p $(@D)
	sed -e 's,@wireddir\@,$(fake_prefix)/$(wireddir),g' $< > $@
	chmod +x $@

$(rundir)/etc/wired.conf: $(abs_top_srcdir)/wired/wired.conf.in
	@test -d $(@D) || mkdir -p $(@D)
	sed -e 's,@WD_USER\@,$(WD_USER),g' -e 's,@WD_GROUP\@,$(WD_GROUP),g' $< > $@

$(rundir)/transfertest: $(TRANSFERTESTOBJS) $(rundir)/libwired/lib/libwired.a
	@test -d $(@D) || mkdir -p $(@D)
	$(LINK) $(TRANSFERTESTOBJS) $(LIBS)

$(objdir)/wired/%.o: $(abs_top_srcdir)/wired/%.c
	@test -d $(@D) || mkdir -p $(@D)
	$(COMPILE) -I$(<D) -c $< -o $@

$(objdir)/wired/%.d: $(abs_top_srcdir)/wired/%.c
	@test -d $(@D) || mkdir -p $(@D)
	($(DEPEND) $< | sed 's,$*.o,$(@D)/&,g'; echo "$@: $<") > $@

$(objdir)/natpmp/%.o: $(abs_top_srcdir)/thirdparty/natpmp/%.c
	@test -d $(@D) || mkdir -p $(@D)
	$(COMPILE) -I$(<D) -c $< -o $@

$(objdir)/natpmp/%.d: $(abs_top_srcdir)/thirdparty/natpmp/%.c
	@test -d $(@D) || mkdir -p $(@D)
	($(DEPEND) $< | sed 's,$*.o,$(@D)/&,g'; echo "$@: $<") > $@

$(objdir)/miniupnpc/%.o: $(abs_top_srcdir)/thirdparty/miniupnpc/%.c
	@test -d $(@D) || mkdir -p $(@D)
	$(COMPILE) -I$(<D) -c $< -o $@

$(objdir)/miniupnpc/%.d: $(abs_top_srcdir)/thirdparty/miniupnpc/%.c
	@test -d $(@D) || mkdir -p $(@D)
	($(DEPEND) $< | sed 's,$*.o,$(@D)/&,g'; echo "$@: $<") > $@

$(objdir)/transfertest/%.o: $(abs_top_srcdir)/test/transfertest/%.c
	@test -d $(@D) || mkdir -p $(@D)
	$(COMPILE) -I$(<D) -c $< -o $@

$(objdir)/transfertest/%.d: $(abs_top_srcdir)/test/transfertest/%.c
	@test -d $(@D) || mkdir -p $(@D)
	($(DEPEND) $< | sed 's,$*.o,$(@D)/&,g'; echo "$@: $<") > $@

install: all install-man install-wired

install-only: install-man install-wired

install-wired:
	@if [ -e $(installdir)/wired ]; then \
		touch .update; \
	fi

	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/

	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/etc/

	if [ ! -f $(installdir)/etc/wired.conf ]; then \
		$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/etc/wired.conf $(installdir)/etc/; \
	fi
	
	if [ ! -d $(installdir)/files ]; then \
		$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/files/; \
		$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/files/Uploads/; \
		$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/files/Uploads/.wired/; \
		$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/files/Uploads/.wired/type $(installdir)/files/Uploads/.wired/; \
		$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/files/Drop\ Box/; \
		$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(installdir)/files/Drop\ Box/.wired/; \
		$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/files/Drop\ Box/.wired/type $(installdir)/files/Drop\ Box/.wired/; \
		$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/files/Drop\ Box/.wired/permissions $(installdir)/files/Drop\ Box/.wired/; \
	fi

	if [ ! -f $(installdir)/banner.png ]; then \
		$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/banner.png $(installdir)/; \
	fi

	$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/wired.xml $(installdir)/
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/wired $(installdir)/
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) $(rundir)/wiredctl $(installdir)/

	@if [ -f .update ]; then \
		echo ""; \
		echo "Update complete!"; \
		echo ""; \
		echo "You should now run:"; \
		echo "    $(installdir)/wiredctl restart"; \
		echo "to restart a running server."; \
	else \
		echo ""; \
		echo "Installation complete!"; \
		echo ""; \
		echo "An administrator account with login \"admin\" and no password has been created."; \
		echo ""; \
		echo "Remember to edit $(installdir)/etc/wired.conf if you want to make any changes before starting the server."; \
		echo ""; \
		echo "When you are done, run:"; \
		echo "    $(installdir)/wiredctl start"; \
		echo "to start the server."; \
	fi

	@rm -f .update

install-man:
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(mandir)/
	
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(mandir)/man1/
	$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(abs_top_srcdir)/man/wiredctl.1 $(mandir)/man1/
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(mandir)/man5/
	$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(abs_top_srcdir)/man/wired.conf.5 $(mandir)/man5/
	$(INSTALL) -m 755 -o $(WD_USER) -g $(WD_GROUP) -d $(mandir)/man8/
	$(INSTALL) -m 644 -o $(WD_USER) -g $(WD_GROUP) $(abs_top_srcdir)/man/wired.8 $(mandir)/man8/

dist:
	rm -rf wired-$(WD_VERSION)
	rm -f wired-$(WD_VERSION).tar.gz
	mkdir wired-$(WD_VERSION)

	@for i in $(DISTFILES); do \
		if [ -e $$i ]; then \
			echo cp -LRp $$i wired-$(WD_VERSION)/$$i; \
			cp -LRp $$i wired-$(WD_VERSION)/$$i; \
		fi \
	done

	$(SHELL) -ec "cd wired-$(WD_VERSION) && WD_MAINTAINER=0 WI_MAINTAINER=0 $(MAKE) -e distclean scmclean"

	tar -czf wired-$(WD_VERSION).tar.gz wired-$(WD_VERSION)
	rm -rf wired-$(WD_VERSION)

clean: clean-recursive
	rm -f $(objdir)/wired/*.o
	rm -f $(objdir)/wired/*.d
	rm -f $(objdir)/transfertest/*.o
	rm -f $(objdir)/transfertest/*.d
	rm -f $(objdir)/natpmp/*.o
	rm -f $(objdir)/natpmp/*.d
	rm -f $(objdir)/miniupnpc/*.o
	rm -f $(objdir)/miniupnpc/*.d
	rm -f $(rundir)/wired
	rm -f $(rundir)/wiredctl
	rm -f $(rundir)/etc/wired.conf

distclean: clean distclean-recursive
	rm -rf $(objdir)
	rm -f Makefile config.h config.log config.status config.cache
	rm -f wired-$(WD_VERSION).tar.gz

scmclean:
	find . -name .DS_Store -print0 | xargs -0 rm -f
	find . -name CVS -print0 | xargs -0 rm -rf
	find . -name .svn -print0 | xargs -0 rm -rf

ifeq ($(WD_MAINTAINER), 1)
-include $(WIREDOBJS:.o=.d)
-include $(NATPMPOBJS:.o=.d)
-include $(MINIUPNPSOBJS:.o=.d)
-include $(TRANSFERTESTOBJS:.o=.d)
endif
