AM_CFLAGS = \
  $(EVO3E_CFLAGS) \
  -DPLUGINDIR=\""$(plugindir)"\" \
  $(LIBXR_CFLAGS)

plugin_DATA = \
  org-gnome-evolution-eee.eplug \
  org-gnome-evolution-eee.glade \
  org-gnome-eee-cal-subscription.xml

plugin_LTLIBRARIES = \
	liborg-gnome-evolution-eee.la

liborg_gnome_evolution_eee_la_SOURCES = \
  eee-calendar-config.c \
  eee-settings.c \
  eee-calendar.c \
  eee-account.c \
  eee-accounts-manager.c \
  dns-txt-search.c \
  subscribe.c \
  acl.c

liborg_gnome_evolution_eee_la_LIBADD = \
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EVO3E_LIBS)

liborg_gnome_evolution_eee_la_LDFLAGS = \
  -module -avoid-version

EXTRA_DIST = \
  org-gnome-evolution-eee.eplug.in \
  org-gnome-evolution-eee.glade \
  org-gnome-eee-cal-subscription.xml.in \
  eee-accounts-manager.h \
  dns-txt-search.h
