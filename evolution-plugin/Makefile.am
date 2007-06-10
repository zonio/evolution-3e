AM_CFLAGS = \
  $(EVO3E_CFLAGS) \
  -DPLUGINDIR=\""$(plugindir)"\" \
  $(LIBXR_CFLAGS) \
  -I$(top_srcdir)

plugin_DATA = \
  org-gnome-evolution-eee.eplug \
  org-gnome-evolution-eee.glade \
  org-gnome-eee-cal-subscription.xml

plugin_LTLIBRARIES = \
  liborg-gnome-evolution-eee.la

liborg_gnome_evolution_eee_la_SOURCES = \
  eee-calendar-config.c \
  eee-calendar-config.h \
  eee-settings.c \
  eee-settings.h \
  eee-calendar.c \
  eee-calendar.h \
  eee-account.c \
  eee-account.h \
  eee-accounts-manager.c \
  eee-accounts-manager.h \
  dns-txt-search.c \
  dns-txt-search.h \
  utils.c \
  utils.h \
  subscribe.c \
  subscribe.h \
  acl.c \
  acl.h

liborg_gnome_evolution_eee_la_LIBADD = \
  $(top_builddir)/interface/libinterface.la \
  $(LIBXR_LIBS) \
  $(EVO3E_LIBS)

liborg_gnome_evolution_eee_la_LDFLAGS = \
  -module -avoid-version

EXTRA_DIST = \
  org-gnome-evolution-eee.eplug.in \
  org-gnome-evolution-eee.glade \
  org-gnome-eee-cal-subscription.xml.in
