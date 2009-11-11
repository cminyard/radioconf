
EXTRA_DIST = RAD_FILES radios FT-60R.rad FT-60R.empty FT-857D.rad

install-data-am:
	echo "$(mkinstalldirs) \"$(DESTDIR)$(YAESU_CONFIGDIR)\""
	$(mkinstalldirs) "$(DESTDIR)$(YAESU_CONFIGDIR)"
	for p in $(EXTRA_DIST); do \
		echo " $(install_sh_DATA) $$p \"$(DESTDIR)$(YAESU_CONFIGDIR)\""; \
		$(install_sh_DATA) $$p "$(DESTDIR)$(YAESU_CONFIGDIR)"; \
	done
