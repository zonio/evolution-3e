INCLUDES = \
	$(EVO3E_CFLAGS)

plugin_DATA = org-gnome-evolution-eee.eplug
plugin_LTLIBRARIES = liborg-gnome-evolution-eee.la

liborg_gnome_evolution_eee_la_SOURCES = \
	eee-source.c

liborg_gnome_evolution_eee_la_LIBADD = \
	$(EVO3E_LIBS)

liborg_gnome_evolution_eee_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = \
	org-gnome-evolution-eee.eplug.in
