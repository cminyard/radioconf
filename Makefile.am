bin_PROGRAMS = yaesu_rw
AM_CFLAGS=-Wall -Wsign-compare -DYAESU_RW_CONFIGFILE="\"@YAESU_RW_CONFIGFILE@\""
yaesu_rw_SOURCES = yaesu_rw.c
man_MANS = yaesu_rw.1
EXTRA_DIST = $(man_MANS) yaesu_rw.conf FT60-layout.txt FT857-layout.txt \
	FAQ

install-data-am:
	echo "$(mkinstalldirs) \"$(DESTDIR)$(sysconfdir)\""
	$(mkinstalldirs) "$(DESTDIR)$(sysconfdir)"
	echo " $(install_sh_DATA) yaesu_rw.conf \"$(DESTDIR)$(sysconfdir)\""
	$(install_sh_DATA) yaesu_rw.conf "$(DESTDIR)$(sysconfdir)"
