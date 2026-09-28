AUTOMAKE_OPTIONS = foreign
SUBDIRS = m4 po src icons
EXTRA_DIST = config.rpath  AUTHORS COPYING ChangeLog INSTALL \
README TODO touchall ABOUT-NLS po/tr.po_full po/pt.po_full \
po/nonascii.pl ltris48.png ltris.desktop \
CROSSCOMPILE ltris.appdata.xml

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps
	$(mkinstalldirs) $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) ltris48.png $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/ltris.png
	$(INSTALL_DATA) ltris.desktop $(DESTDIR)$(datadir)/applications
	

uninstall-local:
	rm $(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/ltris.png
	rm $(DESTDIR)$(datadir)/applications/ltris.desktop

ACLOCAL_AMFLAGS = -I m4
