bin_PROGRAMS = yaesu_rw
bin_SCRIPTS = yaesu_edit
AM_CFLAGS=-Wall -Wsign-compare -DYAESU_CONFIGDIR="\"@YAESU_CONFIGDIR@\""
yaesu_rw_SOURCES = yaesu_rw.c
man_MANS = yaesu_rw.1 yaesu_edit.1
EXTRA_DIST = $(man_MANS) FT-60R.layout FT-857D.layout \
	FAQ yaesu_edit.py.in
SUBDIRS = yaesuconf

# This exists because I couldn't figure out a way to force autoconf/automake
# to set yaseu_edit executable :(
yaesu_edit: yaesu_edit.py
	cp $< $@
	chmod +x $@