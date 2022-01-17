
EXTRA_DIST = RAD_FILES radios \
	FT-60R.rad FT-60R.empty \
	FT-857D.rad FT-857D.empty \
	FT-7800R.rad FT-7800R.empty \
	VX-7R.rad VX-7R.empty \
	TM-V71.rad TM-V71.empty \
	US.rpt

install-data-am:
	echo "$(mkinstalldirs) \"$(DESTDIR)$(RADIO_CONFIGDIR)\""
	$(mkinstalldirs) "$(DESTDIR)$(RADIO_CONFIGDIR)"
	for p in $(EXTRA_DIST); do \
		echo " $(install_sh_DATA) $$p \"$(DESTDIR)$(RADIO_CONFIGDIR)\""; \
		$(install_sh_DATA) $$p "$(DESTDIR)$(RADIO_CONFIGDIR)"; \
	done
