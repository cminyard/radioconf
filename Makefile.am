bin_PROGRAMS = yaesu_rw kenwood_rw
bin_SCRIPTS = radio_edit
AM_CFLAGS=-Wall -Wsign-compare -DRADIO_CONFIGDIR="\"$(RADIO_CONFIGDIR)\""
yaesu_rw_SOURCES = yaesu_rw.c
kenwood_rw_SOURCES = kenwood_rw.c
kenwood_rw_LDADD = -lgensio
man_MANS = yaesu_rw.1 kenwood_rw.1 radio_edit.1
EXTRA_DIST = $(man_MANS) FAQ radio_edit.py.in TODO
SUBDIRS = radioconf layouts
CLEANFILES = radio_edit

ACLOCAL_AMFLAGS = -I m4

# This exists because I couldn't figure out a way to force autoconf/automake
# to set yaseu_edit executable nor to properly set RADIO_CONFIGDIR in the
# python script :(
radio_edit: radio_edit.py.in
	sub=`echo $(RADIO_CONFIGDIR) | sed 's/\//\\\\\//g'` && \
	sed "s/@YCONF@/$${sub}/" <$< >$@; \
	if [ $$? -eq 0 ]; then \
		chmod +x $@; \
	else \
		rm $@; \
		exit 1; \
	fi
