
EXTRA_DIST = RAD_FILES radios \
	FT-60R.rad FT-60R.empty \
	FT-857D.rad FT-857D.empty \
	FT-7800R.rad FT-7800R.empty \
	VX-7R.rad VX-7R.empty \
	US.rpt

install-data-am:
	echo "$(mkinstalldirs) \"$(DESTDIR)$(YAESU_CONFIGDIR)\""
	$(mkinstalldirs) "$(DESTDIR)$(YAESU_CONFIGDIR)"
	for p in $(EXTRA_DIST); do \
		echo " $(install_sh_DATA) $$p \"$(DESTDIR)$(YAESU_CONFIGDIR)\""; \
		$(install_sh_DATA) $$p "$(DESTDIR)$(YAESU_CONFIGDIR)"; \
	done
