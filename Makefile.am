bin_PROGRAMS = yaesu_rw kenwood_rw
bin_SCRIPTS = yaesu_edit
AM_CFLAGS=-Wall -Wsign-compare -DYAESU_CONFIGDIR="\"$(YAESU_CONFIGDIR)\""
yaesu_rw_SOURCES = yaesu_rw.c
kenwood_rw_SOURCES = kenwood_rw.c
kenwood_rw_LDADD = -lgensio
man_MANS = yaesu_rw.1 yaesu_edit.1
EXTRA_DIST = $(man_MANS) FAQ yaesu_edit.py.in TODO
SUBDIRS = yaesuconf layouts
CLEANFILES = yaesu_edit

ACLOCAL_AMFLAGS = -I m4

# This exists because I couldn't figure out a way to force autoconf/automake
# to set yaseu_edit executable nor to properly set YAESU_CONFIGDIR in the
# python script :(
yaesu_edit: yaesu_edit.py.in
	sub=`echo $(YAESU_CONFIGDIR) | sed 's/\//\\\\\//g'` && \
	sed "s/@YCONF@/$${sub}/" <$< >$@; \
	if [ $$? -eq 0 ]; then \
		chmod +x $@; \
	else \
		rm $@; \
		exit 1; \
	fi
